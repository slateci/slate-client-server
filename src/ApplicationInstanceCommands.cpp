#include "ApplicationInstanceCommands.h"

#include "Logging.h"
#include "Utilities.h"

crow::response listApplicationInstances(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list application instances");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list application instances
	
	auto instances=store.listApplicationInstances();
	
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	//TODO: compose actual result list
	std::vector<crow::json::wvalue> resultItems;
	resultItems.reserve(instances.size());
	for(const ApplicationInstance& instance : instances){
		crow::json::wvalue instanceResult;
		instanceResult["apiVersion"]="v1alpha1";
		instanceResult["kind"]="ApplicationInstance";
		crow::json::wvalue instanceData;
		instanceData["ID"]=instance.id;
		instanceData["name"]=instance.name;
		instanceData["application"]=instance.application;
		instanceData["vo"]=instance.owningVO;
		instanceData["cluster"]=instance.cluster;
		instanceData["created"]=instance.ctime;
		instanceResult["metadata"]=std::move(instanceData);
		resultItems.emplace_back(std::move(instanceResult));
		//TODO: query helm to get current status (helm list {instance.name})?
	}
	result["items"]=std::move(resultItems);
	return crow::response(result);
}

crow::response fetchApplicationInstanceInfo(PersistentStore& store, const crow::request& req, const std::string& instanceID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested fetch information about " << instanceID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	auto instance=store.getApplicationInstance(instanceID);
	if(!instance)
		return crow::response(404,generateError("Application instance not found"));
	
	//only admins or member of the VO which owns an instance may query it
	if(!user.admin && !store.userInVO(user.id,instance.owningVO))
		return crow::response(403,generateError("Not authorized"));
	
	//fetch the full configuration for the instance
	instance.config=store.getApplicationInstanceConfig(instanceID);
	
	//TODO: query helm to get current status (helm list {instance.name}) ?
	
	//TODO: serialize the instance configuration as JSON
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	result["kind"]="ApplicationInstance";
	crow::json::wvalue instanceData;
	instanceData["ID"]=instance.id;
	instanceData["name"]=instance.name;
	instanceData["application"]=instance.application;
	instanceData["vo"]=instance.owningVO;
	instanceData["cluster"]=instance.cluster;
	instanceData["created"]=instance.ctime;
	instanceData["config"]=instance.config;
	result["metadata"]=std::move(instanceData);
	return crow::json::wvalue();
}

crow::response deleteApplicationInstance(PersistentStore& store, const crow::request& req, const std::string& instanceID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested delete " << instanceID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	auto instance=store.getApplicationInstance(instanceID);
	if(!instance)
		return crow::response(404,generateError("Application instance not found"));
	//only admins or member of the VO which owns an instance may delete it
	if(!user.admin && !store.userInVO(user.id,instance.owningVO))
		return crow::response(403,generateError("Not authorized"));
	
	log_info("Deleting " << instance);
	//TODO: perform the delete with helm
	//if helm delete fails, log_error, and return error 500
	
	if(!store.removeApplicationInstance(instanceID)){
		log_error("Failed to delete " << instance << " from persistent store");
		return(crow::response(500,generateError("Instance deletion from database failed")));
	}
	
	return crow::json::wvalue();
}
