#include "VolumeClaimCommands.h"

#include <chrono>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "Logging.h"
#include "Telemetry.h"
#include "ServerUtilities.h"
#include "KubeInterface.h"
#include "Archive.h"


crow::response listVolumeClaims(PersistentStore& store, const crow::request& req){
	using namespace std::chrono;
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	// Take the user token presented in the request and authenticate the user against the PersistentStore
	auto tracer = getTracer();
	auto span = tracer->StartSpan(req.url);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << "requested to list volumes from " << req.remote_endpoint);
	// If no user matches the presented token generate an error
	if (!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	// All users are allowed to list volumes

	std::vector<PersistentVolumeClaim> volumes;

	auto group = req.url_params.get("group");
	auto cluster = req.url_params.get("cluster");

	if (group || cluster) {
		 std::string groupFilter = "";
		 std::string clusterFilter = "";

		if (group)
		{
			groupFilter = group;
		}
		if (cluster)
		{
			clusterFilter = cluster;
		}
		
		volumes = store.listPersistentVolumeClaimsByClusterOrGroup(groupFilter, clusterFilter);
	} else {
		volumes = store.listPersistentVolumeClaims();
	}

	log_info("Volumes Length: " << volumes.size());

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();

	result.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(volumes.size(), alloc);
	for(const PersistentVolumeClaim volume : volumes){
		rapidjson::Value volumeResult(rapidjson::kObjectType);
		volumeResult.AddMember("apiVersion", "v1alpha3", alloc);
		volumeResult.AddMember("kind", "PersistentVolumeClaim", alloc);
		rapidjson::Value volumeData(rapidjson::kObjectType);
		volumeData.AddMember("id", volume.id, alloc);
		volumeData.AddMember("name", volume.name, alloc);
		volumeData.AddMember("group", store.getGroup(volume.group).name, alloc);
		volumeData.AddMember("cluster", store.getCluster(volume.cluster).name, alloc);
		volumeData.AddMember("storageRequest", volume.storageRequest, alloc);
		volumeData.AddMember("storageClass", volume.storageClass, alloc);
		volumeData.AddMember("accessMode", to_string(volume.accessMode), alloc);
		volumeData.AddMember("volumeMode", to_string(volume.volumeMode), alloc);
		volumeData.AddMember("created", volume.ctime, alloc);

		// Query Kubernetes for status info
		auto configPath=store.configPathForCluster(volume.cluster);
		const std::string nspace = store.getGroup(volume.group).namespaceName();
		auto volumeGetResult=kubernetes::kubectl(*configPath, {"get", "pvc", volume.name, "--namespace", nspace, "-o=json"});
		if (volumeGetResult.status) {
			std::ostringstream errMsg;
			errMsg << "kubectl get PVC " << volume.name << " --namespace "
			       << nspace << "failed :" << volumeGetResult.error;
			setWebSpanError(span, errMsg.str(), 500);
			span->End();
			log_error(errMsg.str());
		}

		rapidjson::Document volumeStatus;

		try {
			volumeStatus.Parse(volumeGetResult.output.c_str());
		}catch(std::runtime_error& err){
			std::ostringstream errMsg;
			errMsg << "Unable to parse kubectl get PVC JSON output for " << volume.name << ": " << err.what();
			setWebSpanError(span, errMsg.str(), 500);
			span->End();
			log_error(errMsg.str());
		}

		// Add volume status from K8s (Bound, Pending...)
		if((volumeStatus.IsObject() && volumeStatus.HasMember("status"))
	  	   && volumeStatus["status"].IsObject() && volumeStatus["status"].HasMember("phase")){
			volumeData.AddMember("status", volumeStatus["status"]["phase"], alloc);
		}
		else{
			volumeData.AddMember("status", "unknown", alloc);
		}
		volumeResult.AddMember("metadata", volumeData, alloc);
		resultItems.PushBack(volumeResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);

	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	log_info("volume listing completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
	span->End();
	return crow::response(to_string(result));
}

crow::response fetchVolumeClaimInfo(PersistentStore& store, const crow::request& req, const std::string& claimID){
	auto tracer = getTracer();
	auto span = tracer->StartSpan(req.url);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to get volume " << claimID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}

	PersistentVolumeClaim volume=store.getPersistentVolumeClaim(claimID);
	if(!volume) {
		const std::string& errMsg = "Volume not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}

	// Only admins or members of the Group which owns a volume may query it
	if(!user.admin && !store.userInGroup(user.id,volume.group)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}

	// Get cluster and kubeconfig
	const Cluster cluster=store.getCluster(volume.cluster);
	if(!cluster) {
		const std::string& errMsg = "Invalid Cluster";
		setWebSpanError(span, errMsg, 500);
		span->End();
		log_error(errMsg);
		return crow::response(500, generateError(errMsg));
	}
	auto configPath=store.configPathForCluster(cluster.id);
	const Group group=store.getGroup(volume.group);
	const std::string nspace=group.namespaceName();

	log_info("Sending info about " << volume << " to " << user);

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();

	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "PersistentVolumeClaim", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", volume.id, alloc);
	metadata.AddMember("name", volume.name, alloc);
	metadata.AddMember("group", store.getGroup(volume.group).name, alloc);
	metadata.AddMember("cluster", store.getCluster(volume.cluster).name, alloc);
	metadata.AddMember("storageRequest", volume.storageRequest, alloc);
	metadata.AddMember("accessMode", to_string(volume.accessMode), alloc);
	metadata.AddMember("volumeMode", to_string(volume.volumeMode), alloc);
	metadata.AddMember("storageClass", volume.storageClass, alloc);
	metadata.AddMember("created", volume.ctime, alloc);
	/*
	metadata.AddMember("selectorMatchLabel", volume.selectorMatchLabel, alloc);
	rapidjson::Value selectorLabelExpressions(rapidjson::kArrayType);
		for(const std::string selectorLabelExpression : volume.selectorLabelExpressions){
			rapidjson::Value expression(selectorLabelExpression.c_str(), alloc);
			selectorLabelExpressions.PushBack(expression, alloc);
		}
	metadata.AddMember("selectorLabelExpressions", selectorLabelExpressions, alloc);
	*/
	result.AddMember("metadata", metadata, alloc);

	// Query Kubernetes for details about this PVC	
	
	rapidjson::Document claimDetails;
	using namespace std::chrono;
	high_resolution_clock::time_point t1,t2;
	t1 = high_resolution_clock::now();
	auto kubectlQuery=kubernetes::kubectl(*configPath,{"get","pvc","-n",nspace,"-o=json",volume.name});
	t2 = high_resolution_clock::now();
	log_info("kubectl get pvc completed in " << duration_cast<duration<double>>(t2-t2).count() << " seconds");
	if(kubectlQuery.status){
		std::ostringstream errMsg;
		errMsg << std::string("Failed to get PVC information for ") << volume;
		setWebSpanError(span, errMsg.str(), 400);
		span->End();
		log_error(errMsg.str());
		rapidjson::Value claimInfo(rapidjson::kObjectType);
		claimInfo.AddMember("kind", "Error", alloc);
		claimInfo.AddMember("message", "Failed to get information for PVC", alloc);
		claimDetails.PushBack(claimInfo,alloc);
		result.AddMember("PersistentVolumeClaim",claimDetails,alloc);
		return crow::response(to_string(result));
	}

	rapidjson::Value claimInfo(rapidjson::kObjectType);

	try{
		claimDetails.Parse(kubectlQuery.output.c_str());
	}catch(std::runtime_error& err){
		std::ostringstream errMsg;
		errMsg << "Unable to parse kubectl get pvc JSON output for " << volume << ": " << err.what();
		setWebSpanError(span, errMsg.str(), 400);
		span->End();
		log_error(errMsg.str());
		return crow::response(to_string(result));
	}

	if(claimDetails["status"].HasMember("phase")) {
		claimInfo.AddMember("status", claimDetails["status"]["phase"], alloc);
		result.AddMember("details", claimInfo, alloc); //claimInfo, alloc);
	}
	span->End();
	return crow::response(to_string(result));
}

crow::response createVolumeClaim(PersistentStore& store, const crow::request& req){
	auto tracer = getTracer();
	auto span = tracer->StartSpan(req.url);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to create a new volume from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}

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
	
	if(!body["metadata"].HasMember("group")) {
		const std::string& errMsg = "Missing Group";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"]["group"].IsString()) {
		const std::string& errMsg = "Incorrect type for Group";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	const std::string groupID=body["metadata"]["group"].GetString();
	
	if(!body["metadata"].HasMember("cluster")) {
		const std::string& errMsg = "Missing cluster";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"]["cluster"].IsString()) {
		const std::string& errMsg = "Incorrect type for cluster";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	const std::string clusterID=body["metadata"]["cluster"].GetString();
	
	if(!body["metadata"].HasMember("name")) {
		const std::string& errMsg = "Missing volume name in request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"]["name"].IsString()) {
		const std::string& errMsg = "Incorrect type for volume name";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}

	if(!body["metadata"].HasMember("storageRequest")) {
		const std::string& errMsg = "Missing storage request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if (!body["metadata"]["storageRequest"].IsString()) {
		const std::string& errMsg = "Incorrect type for storage request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}

	if(!body["metadata"].HasMember("accessMode")) {
		const std::string& errMsg = "Missing access mode";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"]["accessMode"].IsString()) {
		const std::string& errMsg = "Incorrect type for access mode";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}

	if(!body["metadata"].HasMember("volumeMode")) {
		const std::string& errMsg = "Missing volume mode";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"]["volumeMode"].IsString()) {
		const std::string& errMsg = "Incorrect type for volume mode";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}

	if(!body["metadata"].HasMember("storageClass")) {
		const std::string& errMsg = "Missing StorageClass";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"]["storageClass"].IsString()) {
		const std::string& errMsg = "Incorrect type for StorageClass";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}

	/*
	if(!body["metadata"].HasMember("selectorMatchLabel"))
		return crow::response(400,generateError("Missing selector labels"));
	if(!body["metadata"]["selectorMatchLabel"].IsString())
		return crow::response(400,generateError("Incorrect type for selector labels"));

	if(!body["metadata"].HasMember("selectorLabelExpressions"))
		return crow::response(400,generateError("Missing selector label expressions"));
	if(!body["metadata"]["selectorLabelExpressions"].IsArray())
		return crow::response(400,generateError("Incorrect type for selector label expressions"));
	for(const auto& exp : body["metadata"]["selectorLabelExpressions"].GetArray()){
		if(!exp.IsString())
			return crow::response(400,generateError("Incorrect type for selector label expression"));
	}
	*/
	
	PersistentVolumeClaim volume;
	volume.id=idGenerator.generateVolumeID();
	volume.name=body["metadata"]["name"].GetString();
	volume.group=body["metadata"]["group"].GetString();
	volume.cluster=body["metadata"]["cluster"].GetString();
	volume.storageRequest=body["metadata"]["storageRequest"].GetString();
	volume.storageClass=body["metadata"]["storageClass"].GetString();
	try{
		volume.accessMode=accessModeFromString(body["metadata"]["accessMode"].GetString());
	}catch(std::runtime_error& err){
		const std::string& errMsg = std::string("Invalid value for access mode: ") + err.what();
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	try{
		volume.volumeMode=volumeModeFromString(body["metadata"]["volumeMode"].GetString());
	}catch(std::runtime_error& err){
		const std::string& errMsg = std::string("Invalid value for volume mode: ") + err.what();
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	/*
	volume.selectorMatchLabel=body["metadata"]["selectorMatchLabel"].GetString();
	for(const auto& exp : body["metadata"]["selectorLabelExpressions"].GetArray())
		volume.selectorLabelExpressions.push_back(exp.GetString());
	*/
	volume.ctime=timestamp();

	//https://kubernetes.io/docs/concepts/overview/working-with-objects/names/
	if(volume.name.size()>253) {
		const std::string& errMsg = "Volume name too long";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	// TODO: replace with regex
	if(volume.name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-.")!=std::string::npos) {
		const std::string& errMsg = "Volume name contains an invalid character";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}

	Group group=store.getGroup(volume.group);
	if(!group) {
		const std::string& errMsg = "Group not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}
	//canonicalize group
	volume.group=group.id;

	// Only members of a Group may create volumes for it
	if(!store.userInGroup(user.id,group.id)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}

	Cluster cluster=store.getCluster(volume.cluster);
	if(!cluster) {
		const std::string& errMsg = "Cluster not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}
	// Canonincalize cluster
	volume.cluster=cluster.id;

	// Groups may only install secrets on cluster which they own or to which 
	// they've been granted access
	if(group.id!=cluster.owningGroup && !store.groupAllowedOnCluster(group.id,cluster.id)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	// Check that volume name isn't already in use
	PersistentVolumeClaim existing=store.findPersistentVolumeClaimByName(group.id,volume.cluster,volume.name);
	if(existing) {
		const std::string& errMsg = "A volume with the same name already exists";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}

	volume.valid = true;

	log_info("Storing volume " << volume << " for " << group  << " on " << cluster);

	// Put volume into the DB
	try{
		bool success=store.addPersistentVolumeClaim(volume);
		if(!success)
			return crow::response(500,generateError("Failed to store volume to the persistent store"));
	}catch(std::runtime_error& err){
		const std::string& errMsg = "Failed to store volume to the persistent store";
		setWebSpanError(span, errMsg + ": " + err.what(), 500);
		span->End();
		log_error(errMsg);
		return crow::response(500, generateError(errMsg));
	}

	// Create PVC in Kubernetes
	{

		// Grab config for requested cluster from PersistentStore
		auto configPath=store.configPathForCluster(cluster.id);

		// Ensure that the group's namespace exists on the cluster
		try{
			kubernetes::kubectl_create_namespace(*configPath, group);
		} catch(std::runtime_error& err){
			store.removePersistentVolumeClaim(volume.id);
			const std::string& errMsg = std::string("Failed to create namespace: ") + err.what();
			setWebSpanError(span, errMsg, 500);
			span->End();
			log_error(errMsg);
			return crow::response(500, generateError(errMsg));
		}

		// Turn expression list into a comma separated string
		std::string labelExpressions = "";
		for(std::string expression : volume.selectorLabelExpressions)
			labelExpressions+=", "+expression;

		// Get selectorMatchLabels as vector
		//std::vector<std::string> matchLabelsVector = volume.getMatchLabelsAsVector();
		//std::vector<std::string> selectorLabelExpressions = volume.getSelectorLabelExpressions();
		// new section for json
		//Create PVC from JSON file with Kubectl
		FileHandle pvcFile=makeTemporaryFile(".pvc.json");

		rapidjson::Document doc(rapidjson::kObjectType);
		rapidjson::Document::AllocatorType& alloc = doc.GetAllocator();
		doc.AddMember("apiVersion", "v1", alloc);
		doc.AddMember("kind", "PersistentVolumeClaim", alloc);

		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", volume.name, alloc);
		metadata.AddMember("namespace", group.namespaceName(), alloc);
		doc.AddMember("metadata", metadata, alloc);

		rapidjson::Value spec(rapidjson::kObjectType);
		rapidjson::Value accessModes(rapidjson::kArrayType);
		rapidjson::Value accessModeValue;
		accessModeValue.SetString(to_string(volume.accessMode).c_str(), to_string(volume.accessMode).length(), alloc);
		accessModes.PushBack( accessModeValue, alloc);
		spec.AddMember("accessModes", accessModes, alloc);
		spec.AddMember("volumeMode", to_string(volume.volumeMode), alloc);

		rapidjson::Value resources(rapidjson::kObjectType);
		rapidjson::Value requests(rapidjson::kObjectType);
		requests.AddMember("storage", volume.storageRequest, alloc);
		resources.AddMember("requests", requests, alloc);
		spec.AddMember("resources", resources, alloc);

		spec.AddMember("storageClassName", volume.storageClass, alloc);
		
		/*
		rapidjson::Value selector(rapidjson::kObjectType);

		// setup select matchLabels
		rapidjson::Value matchLabels(rapidjson::kObjectType);

		std::string matchLabelKey;
		std::size_t i = 0;
		for(std::string s : matchLabelsVector) {
			if(i % 2 == 0) {
				matchLabelKey = s;
			} else {
				// https://github.com/Tencent/rapidjson/issues/261
				rapidjson::Value k(matchLabelKey.c_str(), alloc);
				matchLabels.AddMember(k, s, alloc);
			}
			i++;
		}
		selector.AddMember("matchLabels", matchLabels, alloc);

		// Selector Label Expressions
		rapidjson::Value matchExpressions(rapidjson::kArrayType);
		rapidjson::Value matchExpressionObj(rapidjson::kObjectType);
		rapidjson::Value leKey(selectorLabelExpressions[0].c_str(), alloc);
		matchExpressionObj.AddMember(leKey, selectorLabelExpressions[1], alloc);
		matchExpressionObj.AddMember("operator", selectorLabelExpressions[3], alloc);
		// values required when operator is In or NotIn
		rapidjson::Value values(rapidjson::kArrayType);
		for(size_t i = 4; i < selectorLabelExpressions.size(); i++) {
			rapidjson::Value val(selectorLabelExpressions[i].c_str(), alloc);
			values.PushBack(val, alloc);
		}
		matchExpressionObj.AddMember("values", values, alloc);
		matchExpressions.PushBack(matchExpressionObj, alloc);
		selector.AddMember("matchExpressions", matchExpressions, alloc);

		spec.AddMember("selector", selector, alloc);
		*/

		doc.AddMember("spec", spec, alloc);

		// write json to file
		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		doc.Accept(writer);
		std::ofstream pvcJson(pvcFile);
		pvcJson << buffer.GetString();

		pvcJson.close();

		std::vector<std::string> arguments={"create", "-f", pvcFile};

		auto result = kubernetes::kubectl(*configPath, arguments);

		if(result.status)
		{
			std::string errMsg = "Failed to create PVC on kubernetes: " + result.error;
			//if installation fails, remove from the database again
			store.removePersistentVolumeClaim(volume.id);
			setWebSpanError(span, errMsg, 500);
			span->End();
			log_error(errMsg);
			return crow::response(500, generateError(errMsg));
		}

	}

	log_info("Created " << volume << " on " << cluster << " owned by " << group 
	         << " on behalf of " << user);

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "PersistentVolumeClaim", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", volume.name, alloc);
	metadata.AddMember("id", volume.id, alloc);
	metadata.AddMember("group", store.getGroup(volume.group).name, alloc);
	metadata.AddMember("cluster", store.getCluster(volume.cluster).name, alloc);
	metadata.AddMember("storageRequest", volume.storageRequest, alloc);
	metadata.AddMember("accessMode", to_string(volume.accessMode), alloc);
	metadata.AddMember("volumeMode", to_string(volume.volumeMode), alloc);
	metadata.AddMember("storageClass", volume.storageClass, alloc);
	metadata.AddMember("created", volume.ctime, alloc);
	/*
	metadata.AddMember("selectorMatchLabel", volume.selectorMatchLabel, alloc);
	rapidjson::Value selectorLabelExpressions(rapidjson::kArrayType);
		for(const std::string selectorLabelExpression : volume.selectorLabelExpressions){
			rapidjson::Value expression(selectorLabelExpression.c_str(), alloc);
			selectorLabelExpressions.PushBack(expression, alloc);
		}
	metadata.AddMember("selectorLabelExpressions", selectorLabelExpressions, alloc);
	*/
	result.AddMember("metadata", metadata, alloc);
	span->End();
	return crow::response(to_string(result));
}	

crow::response deleteVolumeClaim(PersistentStore& store, const crow::request& req, const std::string& claimID){
	auto tracer = getTracer();
	auto span = tracer->StartSpan(req.url);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to delete " << claimID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}

	auto volume=store.getPersistentVolumeClaim(claimID);
	if(!volume) {
		const std::string& errMsg = "Volume not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}
	//only admins or member of the Group which owns an instance may delete it
	if(!user.admin && !store.userInGroup(user.id,volume.group)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	bool force=(req.url_params.get("force")!=nullptr);

	auto err=internal::deleteVolumeClaim(store,volume,force);
	if(!err.empty()) {
		setWebSpanError(span, err, 500);
		span->End();
		log_error(err);
		return crow::response(500, generateError(err));
	}
	span->End();
	return crow::response(200);
}

// Internal function which requires that initial authorization checks have already been performed
namespace internal{
	std::string deleteVolumeClaim(PersistentStore& store, const PersistentVolumeClaim& volume, bool force){
		log_info("Deleting " << volume);
		// Remove from Kubernetes
		{
			Group group=store.findGroupByID(volume.group);
			try{
				auto configPath=store.configPathForCluster(volume.cluster);
				const std::string nspace = group.namespaceName();

				// Find out if PVC is mounted by any pods
				std::vector<std::vector<std::string>> podsMountedBy;

				// Get all pods in the same namespace (This is the set of pods that are "eligible" to mount this volume)
				auto podResult=kubernetes::kubectl(*configPath, {"get", "pods", "--namespace", nspace, "-o=json"});
				if (podResult.status)
				{
					log_error("kubectl get pods failed: " << podResult.error);
				}

				rapidjson::Document podData;

				// For each pod in the namespace loop through each of the pod's volumes (pod.Spec.Volumes)
				podData.Parse(podResult.output.c_str());

				for(const auto& pod : podData["items"].GetArray()){

					if(!pod.IsObject()){
						log_warn("Pod result is not an object? Skipping");
						continue;
					}

					if(!pod.HasMember("metadata") || !pod.HasMember("spec") || !pod["metadata"].HasMember("generateName")
						|| !pod["metadata"]["generateName"].IsString() || !pod["spec"].IsObject() || !pod["spec"].HasMember("volumes") || !pod["spec"]["volumes"].IsArray())
							log_warn("Pod result does not have expected structure or does not contain any volumes. Skipping");

					// For volumes of "type" PersistentVolumeClaims check PersistentVolumeClaim.ClaimName
					// If ClaimName matches volume.name push PodName onto the list of podsMountedBy
					for (const auto& podVolume : pod["spec"]["volumes"].GetArray()){
						if(podVolume.HasMember("persistentVolumeClaim") && podVolume["persistentVolumeClaim"]["claimName"] == volume.name)
						{
							std::vector<std::string> podInfo = {};
							podInfo.push_back(pod["metadata"]["generateName"].GetString());
							if (pod.HasMember("metadata") && pod["metadata"].HasMember("labels") && pod["metadata"]["labels"].HasMember("instanceID")) { // add the instance ID if it is present
								podInfo.push_back(pod["metadata"]["labels"]["instanceID"].GetString());
							}
							podsMountedBy.push_back(podInfo);
						}
					}
				}				

				// If after checking all pods in the namespace the list of podsMountedBy is nonempty
				// Warn the user and abort the operation.
				if(!podsMountedBy.empty())
				{
					std::string err = "Cannot delete volume from Kubernetes which is currently mounted by one or more pods: \n";
					for(const auto& pod : podsMountedBy) {
						err+=pod.at(0);
						if (pod.size() > 1) {
							err += " (Slate ID: " + pod.at(1) + ")\n";
						}
					}
						
					log_info("Cannot delete volume from Kubernetes which is currently mounted by one or more pods.");
					return err;
				}

				auto deletionResult=kubernetes::kubectl(*configPath, 
				  {"delete","pvc",volume.name,"--namespace",nspace});
				if(deletionResult.status){
					log_error("kubectl delete pvc failed: " << deletionResult.error);
					if(!force)
						return "Failed to delete volume from kubernetes";
					else
						log_info("Forcing deletion of " << volume << " in spite of kubectl error");
				}
			}
			catch(std::runtime_error& e){
				if(!force)
					return "Failed to delete volume from kubernetes";
				else
					log_info("Forcing deletion of " << volume << " in spite of error");
			}
		}

		// Remove from the database
		bool success=store.removePersistentVolumeClaim(volume.id);
		if(!success){
			log_error("Failed to delete " << volume << " from persistent store");
			return "Failed to delete volume from database";
		} else {
			log_info("Successfully removed Volume Claim from database");
		}
		return "";
	}
}
