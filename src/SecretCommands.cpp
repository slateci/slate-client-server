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
	///The current amount of data curently in use, while data.dataSize is the 
	///total capacity
	std::size_t size;
};

crow::response listSecrets(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list secrets from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list clusters
	
	auto groupRaw = req.url_params.get("group");
	auto clusterRaw = req.url_params.get("cluster");
	
	if(!groupRaw)
		return crow::response(400,generateError("A Group must be spcified"));
	std::string cluster;
	if(clusterRaw)
		cluster=clusterRaw;
	
	//get information on the owning Group, needed to look up services, etc.
	const Group group=store.getGroup(groupRaw);
	if(!group)
		return crow::response(404,generateError("Group not found"));
	
	//only admins or members of a Group may list its secrets
	if(!user.admin && !store.userInGroup(user.id,group.id))
		return crow::response(403,generateError("Not authorized"));
	
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
	
	return crow::response(to_string(result));
}

crow::response createSecret(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to create a secret from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//unpack the target cluster info
	rapidjson::Document body;
	try{
		body.Parse(req.body);
	}catch(std::runtime_error& err){
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	
	if(body.IsNull()) {
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	if(!body.HasMember("metadata"))
		return crow::response(400,generateError("Missing user metadata in request"));
	if(!body["metadata"].IsObject())
		return crow::response(400,generateError("Incorrect type for metadata"));
	
	if(!body["metadata"].HasMember("name"))
		return crow::response(400,generateError("Missing secret name in request"));
	if(!body["metadata"]["name"].IsString())
		return crow::response(400,generateError("Incorrect type for secret name"));
	if(!body["metadata"].HasMember("group"))
		return crow::response(400,generateError("Missing Group ID in request"));
	if(!body["metadata"]["group"].IsString())
		return crow::response(400,generateError("Incorrect type for Group ID"));
	if(!body["metadata"].HasMember("cluster"))
		return crow::response(400,generateError("Missing cluster ID in request"));
	if(!body["metadata"]["cluster"].IsString())
		return crow::response(400,generateError("Incorrect type for cluster ID"));
	
	if(body.HasMember("contents") && body.HasMember("copyFrom"))
		return crow::response(400,generateError("Secret contents and copy source cannot both be specified"));
	if(!body.HasMember("contents") && !body.HasMember("copyFrom"))
		return crow::response(400,generateError("Missing secret contents or source in request"));
	if(body.HasMember("contents") && !body["contents"].IsObject())
		return crow::response(400,generateError("Incorrect type for contents"));
	if(body.HasMember("copyFrom") && !body["copyFrom"].IsString())
		return crow::response(400,generateError("Incorrect type for copyFrom"));
	
	//contents may not be completely arbitrary key-value pairs; 
	//the values need to be strings, the keys need to meet kubernetes requirements
	const static std::string allowedKeyCharacters="-._0123456789"
	"abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	if(body.HasMember("contents")){
		for(const auto& member : body["contents"].GetObject()){
			if(!member.value.IsString())
				return crow::response(400,generateError("Secret value is not a string"));
			if(member.name.GetStringLength()==0)
				return crow::response(400,generateError("Secret keys may not be empty"));
			if(member.name.GetStringLength()>253)
				return crow::response(400,generateError("Secret keys may be no more than 253 characters"));
			if(std::string(member.name.GetString())
			   .find_first_not_of(allowedKeyCharacters)!=std::string::npos)
				return crow::response(400,generateError("Secret key does not match [-._a-zA-Z0-9]+"));
			if(!sanityCheckBase64(member.value.GetString())){
				log_warn("Secret data appears not to be base64 encoded");
				return crow::response(400,generateError("Secret data items must be base64 encoded"));
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
	if(secret.name.size()>253)
		return crow::response(400,generateError("Secret name too long"));
	if(secret.name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-.")!=std::string::npos)
		return crow::response(400,generateError("Secret name contains an invalid character"));
	
	Group group=store.getGroup(secret.group);
	if(!group)
		return crow::response(404,generateError("Group not found"));
	//canonicalize group
	secret.group=group.id;
	
	//only members of a Group may install secrets for it
	if(!store.userInGroup(user.id,group.id))
		return crow::response(403,generateError("Not authorized"));
	
	Cluster cluster=store.getCluster(secret.cluster);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	//canonicalize cluster
	secret.cluster=cluster.id;
	
	//groups may only install secrets on clusters which they own or to which 
	//they've been granted access
	if(group.id!=cluster.owningGroup && !store.groupAllowedOnCluster(group.id,cluster.id))
		return crow::response(403,generateError("Not authorized"));
	
	//check that name is not in use
	Secret existing=store.findSecretByName(group.id,secret.cluster,secret.name);
	if(existing)
		return crow::response(400,generateError("A secret with the same name already exists"));
	
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
		if(!existing)
			return crow::response(404,generateError("The specified source secret does not exist"));
		//make sure that the requesting user has access to the source secret
		if(!store.userInGroup(user.id,existing.group))
			return crow::response(403,generateError("Not authorized"));
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
		if(!success)
			return crow::response(500,generateError("Failed to store secret to the persistent store"));
	}catch(std::runtime_error& err){
		log_error("Failed to store secret to the persistent store: " << err.what());
		return crow::response(500,generateError("Failed to store secret to the persistent store"));
	}
	
	//put secret into kubernetes
	{
		auto configPath=store.configPathForCluster(cluster.id);
		
		try{
			kubernetes::kubectl_create_namespace(*configPath, group);
		}
		catch(std::runtime_error& err){
			store.removeSecret(secret.id);
			log_error("Failed to create namespace: " << err.what());
			return crow::response(500,generateError(err.what()));
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
				if(!outFile)
					log_fatal("Failed to open " << outPath << " for writing");
				outFile.write(value.c_str(),value.size());
				if(outFile.fail())
					log_fatal("Failed while writing to " << outPath);
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
			return crow::response(500,generateError(errMsg));
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
	
	return crow::response(to_string(result));
}

crow::response deleteSecret(PersistentStore& store, const crow::request& req,
                            const std::string& secretID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to delete secret " << secretID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	Secret secret=store.getSecret(secretID);
	if(!secret)
		return crow::response(404,generateError("Secret not found"));
	
	//only members of a Group may delete its secrets
	if(!store.userInGroup(user.id,secret.group))
		return crow::response(403,generateError("Not authorized"));
	bool force=(req.url_params.get("force")!=nullptr);
	
	auto err=internal::deleteSecret(store,secret,force);
	if(!err.empty())
		return crow::response(500,generateError(err));
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
	return "";
}
std::string deleteSecretFromStore(PersistentStore& store, const Secret& secret){
	//remove from the database
	if(!store.removeSecret(secret.id)){
		log_error("Failed to delete " << secret << " from persistent store");
		return "Failed to delete secret from database";
	}
	return deleteSecretFromStore(store, secret);
}
}

crow::response getSecret(PersistentStore& store, const crow::request& req,
                         const std::string& secretID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to get secret " << secretID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	Secret secret=store.getSecret(secretID);
	if(!secret)
		return crow::response(404,generateError("Secret not found"));
	
	//only members of a Group may view its secrets
	if(!store.userInGroup(user.id,secret.group))
		return crow::response(403,generateError("Not authorized"));
	
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
		log_error("Secret decryption failed: " << err.what());
		return crow::response(500,generateError("Secret decryption failed"));
	}
	
	return crow::response(to_string(result));
}
