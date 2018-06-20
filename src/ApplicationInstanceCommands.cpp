#include "ApplicationInstanceCommands.h"

#include "Utilities.h"

crow::response listApplicationInstances(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list application instances
	
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	//TODO: compose actual result list
	result["items"]=std::vector<std::string>{};
	return crow::response(result);
}

crow::response fetchApplicationInstanceInfo(PersistentStore& store, const crow::request& req, const std::string& instanceID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	VO vo=validateVO(req.url_params.get("vo"));
	if(!vo)
		return crow::response(400,generateError("Invalid VO"));
	Cluster cluster=validateCluster(req.url_params.get("cluster"));
	if(!cluster)
		return crow::response(400,generateError("Invalid cluster"));
	//TODO: check that user is allowed to delete on behalf of vo from cluster
	
	//TODO: do something useful
	return crow::json::wvalue();
}

crow::response deleteApplicationInstance(PersistentStore& store, const crow::request& req, const std::string& instanceID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	VO vo=validateVO(req.url_params.get("vo"));
	if(!vo)
		return crow::response(400,generateError("Invalid VO"));
	Cluster cluster=validateCluster(req.url_params.get("cluster"));
	if(!cluster)
		return crow::response(400,generateError("Invalid cluster"));
	//TODO: check that user is allowed to delete on behalf of vo from cluster
	
	//TODO: do something useful
	return crow::json::wvalue();
}
