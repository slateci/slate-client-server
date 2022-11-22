#include "SecretCommands.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "Logging.h"
#include "ServerUtilities.h"
#include "KubeInterface.h"
#include "Archive.h"

//conforms to the interface of rapidjson::GenericStringBuffer<UTF8<char>> but
//tries to only keep data in buffers which will be automatically cleared
struct SecretStringBuffer{
	using Ch=rapidjson::UTF8<char>::Ch;
	
	SecretStringBuffer():data(SecretData(32)),size(0){}
	
	void Put(Ch c){
		if(size==data.dataSize)
			Reserve(2*size);
		*(data.data.get()+size)=c;
		size++;
	}
	void PutUnsafe(Ch c){
		//not necessarily optimally fast, but just call safe version
		Put(c);
	}
	void Flush(){ /*do nothing*/ }
	void Clear(){
		data=SecretData(32);
		size=0;
	}
	void ShrinkToFit(){
		SecretData newData(size);
		std::copy_n(data.data.get(),size,newData.data.get());
		data=std::move(newData);
	}
	void Reserve(std::size_t count){
		if(count>data.dataSize){
			SecretData newData(count);
			std::copy_n(data.data.get(),size,newData.data.get());
			data=std::move(newData);
		}
	}
	Ch* Push(std::size_t count){
		Reserve(size+count);
		Ch* ret=data.data.get()+size;
		size+=count;
		return ret;
	}
	Ch* PushUnsafe(std::size_t count){
		//not necessarily optimally fast, but just call safe version
		return PushUnsafe(count);
	}
	void Pop(std::size_t count){
		size-=count;
	}
	const Ch* GetString() const{
		return data.data.get();
	}
	std::size_t GetSize() const{
		return size;
	}
	std::size_t GetLength() const{
		return size;
	}
	
	SecretData data;
	///The current amount of data currently in use, while data.dataSize is the
	///total capacity
	std::size_t size;
};

crow::response listSecrets(PersistentStore& store, const crow::request& req){
	auto tracer = getTracer();
	auto span = tracer->StartSpan(req.url);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " requested to list secrets from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	//All users are allowed to list clusters
	
	auto groupRaw = req.url_params.get("group");
	auto clusterRaw = req.url_params.get("cluster");
	
	if(!groupRaw) {
		const std::string& errMsg = "Group not specified";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	std::string cluster;
	if(clusterRaw)
		cluster=clusterRaw;
	
	//get information on the owning Group, needed to look up services, etc.
	const Group group=store.getGroup(groupRaw);
	if(!group) {
		const std::string& errMsg = "Group not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}
	span->SetAttribute("group", group.name);

	//only admins or members of a Group may list its secrets
	if(!user.admin && !store.userInGroup(user.id,group.id)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	std::vector<Secret> secrets=store.listSecrets(group.id,cluster);
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(secrets.size(), alloc);
	
	for(const Secret& secret : secrets){
		rapidjson::Value secretResult(rapidjson::kObjectType);
		secretResult.AddMember("apiVersion", "v1alpha3", alloc);
		secretResult.AddMember("kind", "Secret", alloc);
		rapidjson::Value secretData(rapidjson::kObjectType);
		secretData.AddMember("id", secret.id, alloc);
		secretData.AddMember("name", secret.name, alloc);
		secretData.AddMember("group", store.getGroup(secret.group).name, alloc);
		secretData.AddMember("cluster", store.getCluster(secret.cluster).name, alloc);
		secretData.AddMember("created", secret.ctime, alloc);
		secretResult.AddMember("metadata", secretData, alloc);
		resultItems.PushBack(secretResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);
	span->End();
	return crow::response(to_string(result));
}

crow::response createSecret(PersistentStore& store, const crow::request& req){
	auto tracer = getTracer();
	auto span = tracer->StartSpan(req.url);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to create a secret from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	//unpack the target cluster info
	rapidjson::Document body;
	try{
		body.Parse(req.body);
	}catch(std::runtime_error& err){
		const std::string& errMsg = "Invalid JSON in request body";
		setWebSpanError(span, errMsg + " exception: " + err.what(), 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	
	if(body.IsNull()) {
		const std::string& errMsg = "Invalid JSON in request body";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body.HasMember("metadata")) {
		const std::string& errMsg = "Missing user metadata in request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"].IsObject()) {
		const std::string& errMsg = "Incorrect type for metadata";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	
	if(!body["metadata"].HasMember("name")) {
		const std::string& errMsg = "Missing secret name in request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"]["name"].IsString()) {
		const std::string& errMsg = "Incorrect type for secret name";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"].HasMember("group")) {
		const std::string& errMsg = "Missing Group ID in request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"]["group"].IsString()) {
		const std::string& errMsg = "Incorrect type for Group ID";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"].HasMember("cluster")) {
		const std::string& errMsg = "Missing cluster ID in request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"]["cluster"].IsString()) {
		const std::string& errMsg = "Incorrect type for cluster ID";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	
	if(body.HasMember("contents") && body.HasMember("copyFrom")) {
		const std::string& errMsg = "Secret contents and copy source cannot both be specified";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body.HasMember("contents") && !body.HasMember("copyFrom")) {
		const std::string& errMsg = "Missing secret contents or source in request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(body.HasMember("contents") && !body["contents"].IsObject()) {
		const std::string& errMsg = "Incorrect type for contents";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(body.HasMember("copyFrom") && !body["copyFrom"].IsString()) {
		const std::string& errMsg = "Incorrect type for copyFrom";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	
	//contents may not be completely arbitrary key-value pairs; 
	//the values need to be strings, the keys need to meet kubernetes requirements
	// TODO: convert to using regex
	const static std::string allowedKeyCharacters="-._0123456789"
	"abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	if(body.HasMember("contents")){
		for(const auto& member : body["contents"].GetObject()){
			if(!member.value.IsString()) {
				const std::string& errMsg = "Secret value is not a string";
				setWebSpanError(span, errMsg, 400);
				span->End();
				log_error(errMsg);
				return crow::response(400, generateError(errMsg));
			}
			if(member.name.GetStringLength()==0) {
				const std::string& errMsg = "Secret keys may not be empty";
				setWebSpanError(span, errMsg, 400);
				span->End();
				log_error(errMsg);
				return crow::response(400, generateError(errMsg));
			}
			if(member.name.GetStringLength()>253) {
				const std::string& errMsg = "Secret keys may be no more than 253 characters";
				setWebSpanError(span, errMsg, 400);
				span->End();
				log_error(errMsg);
				return crow::response(400, generateError(errMsg));
			}
			if(std::string(member.name.GetString())
			   .find_first_not_of(allowedKeyCharacters)!=std::string::npos) {
 				const std::string& errMsg = "Secret key does not match [-._a-zA-Z0-9]+";
				setWebSpanError(span, errMsg, 400);
				span->End();
				log_error(errMsg);
				return crow::response(400, generateError(errMsg));
			}
			if(!sanityCheckBase64(member.value.GetString())){
				const std::string& errMsg = "Secret data items must be base64 encoded";
				setWebSpanError(span, errMsg, 400);
				span->End();
				log_error(errMsg);
				return crow::response(400, generateError(errMsg));
			}
		}
	}
	
	Secret secret;
	secret.id=idGenerator.generateSecretID();
	secret.name=body["metadata"]["name"].GetString();
	secret.group=body["metadata"]["group"].GetString();
	secret.cluster=body["metadata"]["cluster"].GetString();
	secret.ctime=timestamp();
	
	//https://kubernetes.io/docs/concepts/overview/working-with-objects/names/
	if(secret.name.size()>253) {
		const std::string& errMsg = "Secret name too long";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(secret.name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-.")!=std::string::npos) {
		const std::string& errMsg = "Secret name contains an invalid character";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	
	Group group=store.getGroup(secret.group);
	if(!group) {
		const std::string& errMsg = "Group not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}
	span->SetAttribute("group", group.name);

	//canonicalize group
	secret.group=group.id;
	
	//only members of a Group may install secrets for it
	if(!store.userInGroup(user.id,group.id)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	Cluster cluster=store.getCluster(secret.cluster);
	if(!cluster) {
		const std::string& errMsg = "Cluster not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}
	span->SetAttribute("cluster", cluster.name);

	//canonicalize cluster
	secret.cluster=cluster.id;
	
	//groups may only install secrets on clusters which they own or to which 
	//they've been granted access
	if(group.id!=cluster.owningGroup && !store.groupAllowedOnCluster(group.id,cluster.id)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	//check that name is not in use
	Secret existing=store.findSecretByName(group.id,secret.cluster,secret.name);
	if(existing) {
		const std::string& errMsg = "A secret with the same name already exists";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	
	if(body.HasMember("contents")){ //Re-serialize the contents and encrypt
		SecretStringBuffer buf;
		rapidjson::Writer<SecretStringBuffer> writer(buf);
		rapidjson::Document tmp(rapidjson::kObjectType);
		body["contents"].Accept(writer);
		//swizzle size and capacity so we encrypt only the useful data, but put
		//things back when we're done
		const std::size_t capacity=buf.data.dataSize;
		struct fixCap{
			const std::size_t& cap;
			SecretData& data;
			fixCap(const std::size_t& cap, SecretData& data):cap(cap),data(data){}
			~fixCap(){ data.dataSize=cap; }
		} fix(capacity,buf.data);
		buf.data.dataSize=buf.size;
		secret.data=store.encryptSecret(buf.data);
	}
	else{ //try to copy contents from an existing secret
		std::string sourceID=body["copyFrom"].GetString();
		log_info("Request is to copy from secret " << sourceID);
		existing=store.getSecret(sourceID);
		if(!existing) {
			const std::string& errMsg = "The specified source secret does not exist";
			setWebSpanError(span, errMsg, 404);
			span->End();
			log_error(errMsg);
			return crow::response(404, generateError(errMsg));
		}
		//make sure that the requesting user has access to the source secret
		if(!store.userInGroup(user.id,existing.group)) {
			const std::string& errMsg = "User not authorized";
			setWebSpanError(span, errMsg, 403);
			span->End();
			log_error(errMsg);
			return crow::response(403, generateError(errMsg));
		}
		secret.data=existing.data;
		//Unfortunately, we _also_ need to decrypt the secret in order to pass
		//its data to Kubernetes. 
		SecretData secretData=store.decryptSecret(existing);
		rapidjson::Document contents(rapidjson::kObjectType,&body.GetAllocator());
		contents.Parse(secretData.data.get(),secretData.dataSize);
		body.AddMember("contents",contents,body.GetAllocator());
	}
	secret.valid=true;
	
	log_info("Storing secret " << secret << " for " << group  << " on " << cluster);
	
	//put secret into the DB
	try{
		bool success=store.addSecret(secret);
		if(!success) {
			const std::string& errMsg = "Failed to store secret to the persistent store";
			setWebSpanError(span, errMsg, 500);
			span->End();
			log_error(errMsg);
			return crow::response(500, generateError(errMsg));
		}
	}catch(std::runtime_error& err){
		const std::string& errMsg = "Failed to store secret to the persistent store";
		setWebSpanError(span, errMsg + ": " + err.what(), 500);
		span->End();
		log_error(errMsg);
		return crow::response(500, generateError(errMsg));
	}
	
	//put secret into kubernetes
	{
		auto configPath=store.configPathForCluster(cluster.id);
		
		try{
			kubernetes::kubectl_create_namespace(*configPath, group);
		}
		catch(std::runtime_error& err){
			store.removeSecret(secret.id);
			const std::string& errMsg = std::string("Failed to create namespace: ") + err.what();
			setWebSpanError(span, errMsg, 500);
			span->End();
			log_error(errMsg);
			return crow::response(500, generateError(errMsg));
		}
		
		//build up the kubectl command to create the secret. this involves 
		//writing each secret value to a temporary file to avoid losing data if 
		//there are NUL bytes. This is not very nice because it means that 
		//unencrypted secrets are temporarily on the local filesystem (and on 
		//an SSD) may continue to exist on the disk for a long time. The only 
		//alternative would be to compose a large YAML document specifying the 
		//secret and streaming it directly to kubectl, but input to child 
		//processes seems to be unreliable at the moment for reasons which are unclear. 
		std::vector<std::string> arguments={"create","secret","generic",
		                                    secret.name,"--namespace",group.namespaceName()};
		std::vector<FileHandle> valueFiles;
		for(const auto& member : body["contents"].GetObject()){
			const std::string value=decodeBase64(member.value.GetString());
			valueFiles.emplace_back(makeTemporaryFile("secret_"));
			std::string outPath=valueFiles.back();
			{
				std::ofstream outFile(outPath);
				if(!outFile) {
					const std::string& errMsg = "Failed to open " + outPath + " for writing";
					setWebSpanError(span, errMsg, 500);
					span->End();
					log_fatal(errMsg);
				}
				outFile.write(value.c_str(),value.size());
				if(outFile.fail()) {
					const std::string& errMsg = "Failed while writing to " + outPath;
					setWebSpanError(span, errMsg, 500);
					span->End();
					log_fatal(errMsg);
				}
			}
			arguments.push_back(std::string("--from-file=")+member.name.GetString()
			+std::string("=")+outPath);
		}
		auto result=kubernetes::kubectl(*configPath, arguments);
		
		if(result.status){
			std::string errMsg="Failed to store secret to kubernetes: "+result.error;
			log_error(errMsg);
			//if installation fails, remove from the database again
			store.removeSecret(secret.id);
			setWebSpanError(span, errMsg, 500);
			span->End();
			return crow::response(500, generateError(errMsg));
		}
	}
	
	log_info("Created " << secret << " on " << cluster << " owned by " << group 
	         << " on behalf of " << user);
	
	//compose response
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "Secret", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", secret.id, alloc);
	metadata.AddMember("name", secret.name, alloc);
	result.AddMember("metadata", metadata, alloc);

	span->End();
	return crow::response(to_string(result));
}

crow::response deleteSecret(PersistentStore& store, const crow::request& req,
                            const std::string& secretID){
	auto tracer = getTracer();
	auto span = tracer->StartSpan(req.url);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to delete secret " << secretID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	Secret secret=store.getSecret(secretID);
	if(!secret) {
		const std::string& errMsg = "Secret not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}
	
	//only members of a Group may delete its secrets
	if(!store.userInGroup(user.id,secret.group)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	bool force=(req.url_params.get("force")!=nullptr);
	
	auto err=internal::deleteSecret(store,secret,force);
	if(!err.empty()) {
		setWebSpanError(span, err, 500);
		span->End();
		log_error(err);
		return crow::response(500, generateError(err));
	}
	span->End();
	return crow::response(200);
}

namespace internal{
std::string deleteSecret(PersistentStore& store, const Secret& secret, bool force){
	log_info("Deleting " << secret);
	//remove from kubernetes
	{
		Group group=store.findGroupByID(secret.group);
		try{
			auto configPath=store.configPathForCluster(secret.cluster);
			auto result=kubernetes::kubectl(*configPath,
			  {"delete","secret",secret.name,"--namespace",group.namespaceName()});
			if(result.status){
				log_error("kubectl delete secret failed: " << result.error);
				if(!force)
					return "Failed to delete secret from kubernetes";
				else
					log_info("Forcing deletion of " << secret << " in spite of kubectl error");
			}
		}
		catch(std::runtime_error& e){
			if(!force)
				return "Failed to delete secret from kubernetes";
			else
				log_info("Forcing deletion of " << secret << " in spite of error");
		}
	}
	
	//remove from the database
	bool success=store.removeSecret(secret.id);
	if(!success){
		log_error("Failed to delete " << secret << " from persistent store");
		return "Failed to delete secret from database";
	}
	return "";
}
}

crow::response getSecret(PersistentStore& store, const crow::request& req,
                         const std::string& secretID){
	auto tracer = getTracer();
	auto span = tracer->StartSpan(req.url);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to get secret " << secretID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	Secret secret=store.getSecret(secretID);
	if(!secret) {
		const std::string& errMsg = "Secret not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}
	
	//only members of a Group may view its secrets
	if(!store.userInGroup(user.id,secret.group)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	log_info("Sending " << secret << " to " << user);
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "Secret", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", secret.id, alloc);
	metadata.AddMember("name", secret.name, alloc);
	metadata.AddMember("group", store.getGroup(secret.group).name, alloc);
	metadata.AddMember("cluster", store.getCluster(secret.cluster).name, alloc);
	metadata.AddMember("created", secret.ctime, alloc);
	result.AddMember("metadata", metadata, alloc);
	
	rapidjson::Document contents;
	try{
		auto secretData=store.decryptSecret(secret);
		contents.Parse(secretData.data.get(), secretData.dataSize);
		result.AddMember("contents",contents,alloc);
	} catch(std::runtime_error& err){
		const std::string& errMsg = std::string("Secret decryption failed: ") + err.what();
		setWebSpanError(span, errMsg, 500);
		span->End();
		log_error(errMsg);
		return crow::response(500, generateError(errMsg));
	}

	span->End();
	return crow::response(to_string(result));
}
