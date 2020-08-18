#include "VolumeClaimCommands.h"

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
			groupFilter = group;
		if (clusterFilter)
			clusterFilter = cluster;
		
		volumes = store.listPersistentVolumeClaimsByClusterOrGroup(groupFilter, clusterFilter);
	} else
		volumes = store.listPersistentVolumeClaims()

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();

	result.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson:Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(volumes.size(), alloc);
	for(cons PersistentVolumeClaim volume : volumes){
		rapidjson::Value volumeResult(rapidjson::kObjectType);
		volumeResult.AddMember("apiVersion", "v1alpha3", alloc);
		volumeResult.AddMember("kind", "PersistentVolumeClaim", alloc);
		rapidjson::Value volumeData(rapidjson::kObjectType);
		volumeData.AddMember("id", volume.id, alloc);
		volumeData.AddMember("name", volume.name, alloc);
		volumeData.AddMember("group", volume.group, alloc);
		volumeData.AddMember("cluster", volume.cluster, alloc);
		volumeData.AddMember("storageRequest", volume.storageRequest, alloc);
		volumeData.AddMember("accessMode", volume.accessMode, alloc);
		volumeData.AddMember("volumeMode", volume.volumeMode, alloc);
		volumeData.AddMember("storageClass", volume.storageClass, alloc);
		volumeData.AddMember("selectorMatchLabel", volume.selectorMatchLabel, alloc);
		volumeData.AddMember("selectorLabelExpressions", volume.selectorLabelExpressions, alloc);
		volumeResult.AddMember("metadata", volumeData, alloc);
		resultItems.PushBack(volumeResult, alloc);
	}
	result.AddMember("items", resultItems, alloc)

	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	log_info("volume listing completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
	return crow::response(to_string(result));
}

crow::response fetchVolumeClaimInfo(PersistentStore& store, const crow::request& req, const std::string& claimID){
#warning TODO: implement this
	throw std::runtime_error("Not implemented");
}

crow::response createVolumeClaim(PersistentStore& store, const crow::request& req){
#warning TODO: implement this
	throw std::runtime_error("Not implemented");
}

crow::response deleteVolumeClaim(PersistentStore& store, const crow::request& req, const std::string& claimID){
#warning TODO: implement this
	throw std::runtime_error("Not implemented");
}
