#include "ApplicationInstanceCommands.h"

#include "Logging.h"
#include "Utilities.h"

crow::response listApplicationInstances(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list application instances");
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
	log_info(user << " requested fetch information about " << instanceID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//TODO: determine which VO owns the instance
	//determine whether user is a member of that VO
	
	//TODO: get the instance configuration
	//TODO: serialize the instance configuration as JSON
	return crow::json::wvalue();
}

crow::response deleteApplicationInstance(PersistentStore& store, const crow::request& req, const std::string& instanceID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested delete " << instanceID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//TODO: determine which VO owns the instance
	//TODO: determine whether user is a member of that VO
	//TODO: determine on which cluster the instance exists
	
	//TODO: perform the delete with helm
	//TODO: perform the delete in the database
	return crow::json::wvalue();
}
