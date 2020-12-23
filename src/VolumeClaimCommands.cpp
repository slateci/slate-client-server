#include "VolumeClaimCommands.h"

#include <chrono>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "Logging.h"
#include "ServerUtilities.h"
#include "KubeInterface.h"
#include "Archive.h"

#include <chrono>

crow::response listVolumeClaims(PersistentStore& store, const crow::request& req){
	using namespace std::chrono;
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	// Take the user token presented in the request and authenticate the user against the PersistentStore
	const User user=authenticateUser(store, req.url_params.get("token")); 
	log_info(user << "requested to list volumes from " << req.remote_endpoint); 
	// If no user matches the presented token generate an error
	if (!user)
		return crow::response(403,generateError("Not authorized"));
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
		const std::string nspace = group.namespaceName();
		auto volumeResult=kubernetes::kubectl(*configPath, {"get", "pvc", volume.name, "--namespace", nspace, "-o=json"});
		if (volumeResult.status) {
			log_error("kubectl get PVC " << volume.name << " --namespace " 
				   << nspace << "failed :" << volumeResult.error);
		}

		// Add volume status from K8s (Bound, Pending...)
		volumeData.AddMember("status", volumeData["status"]["phase"].GetString(), alloc);
		volumeResult.AddMember("metadata", volumeData, alloc);
		resultItems.PushBack(volumeResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);

	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	log_info("volume listing completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
	return crow::response(to_string(result));
}

crow::response fetchVolumeClaimInfo(PersistentStore& store, const crow::request& req, const std::string& claimID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to get volume " << claimID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));

	PersistentVolumeClaim volume=store.getPersistentVolumeClaim(claimID);
	if(!volume)
		return crow::response(403,generateError("Volume not found"));

	// Only admins or members of the Group which owns a volume may query it
	if(!user.admin && !store.userInGroup(user.id,volume.group))
		return crow::response(403,generateError("Not authorized"));

	// Get cluster and kubeconfig
	const Cluster cluster=store.getCluster(volume.cluster);
	if(!cluster)
		return crow::response(500,generateError("Invalid Cluster"));
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
		log_error("Failed to get PVC information for " << volume);
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
		log_error("Unable to parse kubectl get pvc JSON output for " << volume << ": " << err.what());
		return crow::response(to_string(result));
	}

	if(claimDetails["status"].HasMember("phase")) {
		claimInfo.AddMember("status", claimDetails["status"]["phase"], alloc);
		result.AddMember("details", claimInfo, alloc); //claimInfo, alloc);
	}

	return crow::response(to_string(result));
}

crow::response createVolumeClaim(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to create a new volume from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));

	rapidjson::Document body;
	try{
		body.Parse(req.body);
	}catch(std::runtime_error& err){
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	if(body.IsNull())
		return crow::response(400,generateError("Invalid JSON in request body"));

	if(!body.HasMember("metadata"))
		return crow::response(400,generateError("Missing user metadata in request"));
	if(!body["metadata"].IsObject())
		return crow::response(400,generateError("Incorrect type for metadata"));
	
	if(!body["metadata"].HasMember("group"))
		return crow::response(400,generateError("Missing Group"));
	if(!body["metadata"]["group"].IsString())
		return crow::response(400,generateError("Incorrect type for Group"));
	const std::string groupID=body["metadata"]["group"].GetString();
	
	if(!body["metadata"].HasMember("cluster"))
		return crow::response(400,generateError("Missing cluster"));
	if(!body["metadata"]["cluster"].IsString())
		return crow::response(400,generateError("Incorrect type for cluster"));
	const std::string clusterID=body["metadata"]["cluster"].GetString();
	
	if(!body["metadata"].HasMember("name"))
		return crow::response(400,generateError("Missing volume name in request"));
	if(!body["metadata"]["name"].IsString())
		return crow::response(400,generateError("Incorrect type for volume name"));

	if(!body["metadata"].HasMember("storageRequest"))
		return crow::response(400,generateError("Missing storage request"));
	if (!body["metadata"]["storageRequest"].IsString())
		return crow::response(400,generateError("Incorrect type for storage request"));

	if(!body["metadata"].HasMember("accessMode"))
		return crow::response(400,generateError("Missing access mode"));
	if(!body["metadata"]["accessMode"].IsString())
		return crow::response(400,generateError("Incorrect type for access mode"));

	if(!body["metadata"].HasMember("volumeMode"))
		return crow::response(400,generateError("Missing volume mode"));
	if(!body["metadata"]["volumeMode"].IsString())
		return crow::response(400,generateError("Incorrect type for volume mode"));

	if(!body["metadata"].HasMember("storageClass"))
		return crow::response(400,generateError("Missing StorageClass"));
	if(!body["metadata"]["storageClass"].IsString())
		return crow::response(400,generateError("Incorrect type for StorageClass"));

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
		return crow::response(400,generateError(std::string("Invalid value for access mode: ")+err.what()));
	}
	try{
		volume.volumeMode=volumeModeFromString(body["metadata"]["volumeMode"].GetString());
	}catch(std::runtime_error& err){
		return crow::response(400,generateError(std::string("Invalid value for volume mode: ")+err.what()));
	}
	/*
	volume.selectorMatchLabel=body["metadata"]["selectorMatchLabel"].GetString();
	for(const auto& exp : body["metadata"]["selectorLabelExpressions"].GetArray())
		volume.selectorLabelExpressions.push_back(exp.GetString());
	*/
	volume.ctime=timestamp();

	//https://kubernetes.io/docs/concepts/overview/working-with-objects/names/
	if(volume.name.size()>253)
		return crow::response(400,generateError("Volume name too long"));
	if(volume.name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-.")!=std::string::npos)
		return crow::response(400,generateError("Volume name contains an invalid character"));

	Group group=store.getGroup(volume.group);
	if(!group)
		return crow::response(404,generateError("Group not found"));
	//canonicalize group
	volume.group=group.id;

	// Only members of a Group may create volumes for it
	if(!store.userInGroup(user.id,group.id))
		return crow::response(403,generateError("Not authorized"));

	Cluster cluster=store.getCluster(volume.cluster);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	// Canonincalize cluster
	volume.cluster=cluster.id;

	// Groups may only install secrets on cluster which they own or to which 
	// they've been granted access
	if(group.id!=cluster.owningGroup && !store.groupAllowedOnCluster(group.id,cluster.id))
		return crow::response(403,generateError("Not authorized"));
	
	// Check that volume name isn't already in use
	PersistentVolumeClaim existing=store.findPersistentVolumeClaimByName(group.id,volume.cluster,volume.name);
	if(existing)
		return crow::response(400,generateError("A volume with the same name already exists"));

	volume.valid = true;

	log_info("Storing volume " << volume << " for " << group  << " on " << cluster);

	// Put volume into the DB
	try{
		bool success=store.addPersistentVolumeClaim(volume);
		if(!success)
			return crow::response(500,generateError("Failed to store volume to the persistent store"));
	}catch(std::runtime_error& err){
		log_error("Failed to store volume to the persistent store: " << err.what());
		return crow::response(500,generateError("Failed to store volume to the persistent store"));
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
			log_error("Failed to create namespace: " << err.what());
			return crow::response(500,generateError(err.what()));
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
			std::string errMsg="Failed to create PVC on kubernetes: "+result.error;
			//if installation fails, remove from the database again
			store.removePersistentVolumeClaim(volume.id);
			return crow::response(500,generateError(errMsg));
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

	return crow::response(to_string(result));
}	

crow::response deleteVolumeClaim(PersistentStore& store, const crow::request& req, const std::string& claimID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to delete " << claimID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));

	auto volume=store.getPersistentVolumeClaim(claimID);
	if(!volume)
		return crow::response(404,generateError("Volume not found"));
	//only admins or member of the Group which owns an instance may delete it
	if(!user.admin && !store.userInGroup(user.id,volume.group))
		return crow::response(403,generateError("Not authorized"));
	bool force=(req.url_params.get("force")!=nullptr);

	auto err=internal::deleteVolumeClaim(store,volume,force);
	if(!err.empty())
		return crow::response(500,generateError(err));
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
				auto result=kubernetes::kubectl(*configPath, 
				  {"delete","pvc",volume.name,"--namespace",group.namespaceName()});
				if(result.status){
					log_error("kubectl delete pvc failed: " << result.error);
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
