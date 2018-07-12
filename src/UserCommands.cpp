#include "UserCommands.h"

#include "Logging.h"
#include "Utilities.h"

crow::response listUsers(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list users");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//TODO: Are all users are allowed to list all users?
	
	std::vector<User> users=store.listUsers();
	
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	std::vector<crow::json::wvalue> resultItems;
	resultItems.reserve(users.size());
	for(const User& user : users){
		crow::json::wvalue userResult;
		userResult["apiVersion"]="v1alpha1";
		userResult["kind"]="user";
		crow::json::wvalue userData;
		userData["ID"]=user.id;
		userData["name"]=user.name;
		userData["email"]=user.email;
		userResult["metadata"]=std::move(userData);
		resultItems.emplace_back(std::move(userResult));
	}
	result["items"]=std::move(resultItems);
	return result;
}

crow::response createUser(PersistentStore& store, const crow::request& req){
	//important: user is the user issuing the command, not the user being modified
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to create a user");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//unpack the target user info
	crow::json::rvalue body;
	try{
		body = crow::json::load(req.body);
	}catch(std::runtime_error& err){
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	if(!body)
		return crow::response(400,generateError("Invalid JSON in request body"));
	if(!body.has("metadata"))
		return crow::response(400,generateError("Missing user metadata in request"));
	if(body["metadata"].t()!=crow::json::type::Object)
		return crow::response(400,generateError("Incorrect type for configuration"));
	
	if(!user.admin) //only administrators can create new users
		return crow::response(403,generateError("Not authorized"));
	
	if(!body["metadata"].has("globusID"))
		return crow::response(400,generateError("Missing user globus ID in request"));
	if(body["metadata"]["globusID"].t()!=crow::json::type::String)
		return crow::response(400,generateError("Incorrect type for user globus ID"));
	if(!body["metadata"].has("name"))
		return crow::response(400,generateError("Missing user name in request"));
	if(body["metadata"]["name"].t()!=crow::json::type::String)
		return crow::response(400,generateError("Incorrect type for user name"));
	if(!body["metadata"].has("email"))
		return crow::response(400,generateError("Missing user email in request"));
	if(body["metadata"]["email"].t()!=crow::json::type::String)
		return crow::response(400,generateError("Incorrect type for user email"));
	if(!body["metadata"].has("admin"))
		return crow::response(400,generateError("Missing user email in request"));
	if(body["metadata"]["admin"].t()!=crow::json::type::False
	   && body["metadata"]["admin"].t()!=crow::json::type::True)
		return crow::response(400,generateError("Incorrect type for user admin flag"));
	
	User targetUser;
	targetUser.id=idGenerator.generateUserID();
	targetUser.token=idGenerator.generateUserToken();
	targetUser.globusID=body["metadata"]["globusID"].s();
	targetUser.name=body["metadata"]["name"].s();
	targetUser.email=body["metadata"]["email"].s();
	targetUser.admin=body["metadata"]["admin"].b();
	targetUser.valid=true;
	
	log_info("Creating " << targetUser);
	bool created=store.addUser(targetUser);
	
	if(!created)
		return crow::response(500,generateError("User account creation failed"));
	
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	result["kind"]="User";
	crow::json::wvalue metadata;
	metadata["id"]=targetUser.id;
	metadata["name"]=targetUser.name;
	metadata["email"]=targetUser.email;
	metadata["access_token"]=targetUser.token;
	metadata["admin"]=targetUser.admin;
	metadata["VOs"]=std::vector<std::string>{};
	result["metadata"]=std::move(metadata);
	return crow::response(result);
}

crow::response getUserInfo(PersistentStore& store, const crow::request& req, const std::string uID){
	//important: user is the user issuing the command, not the user being modified
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested information about " << uID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//users can only be examined by admins or themselves
	if(!user.admin && user.id!=uID)
		return crow::response(403,generateError("Not authorized"));
	
	User targetUser=store.getUser(uID);
	if(!targetUser)
		return crow::response(404,generateError("Not found"));
	
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	result["kind"]="User";
	crow::json::wvalue metadata;
	metadata["id"]=targetUser.id;
	metadata["name"]=targetUser.name;
	metadata["email"]=targetUser.email;
	metadata["access_token"]=targetUser.token;
	metadata["admin"]=targetUser.admin;
	metadata["VOs"]=store.getUserVOMemberships(uID,true);
	result["metadata"]=std::move(metadata);
	return crow::response(result);
}

crow::response updateUser(PersistentStore& store, const crow::request& req, const std::string uID){
	//important: user is the user issuing the command, not the user being modified
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to update information about " << uID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//users can only be altered by admins and themselves
	if(!user.admin && user.id!=uID)
		return crow::response(403,generateError("Not authorized"));
	
	User targetUser=store.getUser(uID);
	
	//unpack the target user info
	crow::json::rvalue body;
	try{
		body = crow::json::load(req.body);
	}catch(std::runtime_error& err){
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	if(!body)
		return crow::response(400,generateError("Invalid JSON in request body"));
	if(!body.has("metadata"))
		return crow::response(400,generateError("Missing user metadata in request"));
	if(body["metadata"].t()!=crow::json::type::Object)
		return crow::response(400,generateError("Incorrect type for user metadata"));
	
	if(!targetUser)
		return crow::response(403,generateError("User not found"));
	
	User updatedUser=targetUser;
	
	if(body["metadata"].has("name")){
		if(body["metadata"]["name"].t()!=crow::json::type::String)
			return crow::response(400,generateError("Incorrect type for user name"));
		updatedUser.name=body["metadata"]["name"].s();
	}
	if(body["metadata"].has("email")){
		if(body["metadata"]["email"].t()!=crow::json::type::String)
			return crow::response(400,generateError("Incorrect type for user email"));
		updatedUser.email=body["metadata"]["email"].s();
	}
	if(body["metadata"].has("admin")){
		if(body["metadata"]["admin"].t()!=crow::json::type::False
		   && body["metadata"]["admin"].t()!=crow::json::type::True)
			return crow::response(400,generateError("Incorrect type for user admin flag"));
		if(!user.admin) //only admins can alter admin rights
			return crow::response(403,generateError("Not authorized"));
	}
	
	log_info("Updating " << targetUser << " info");
	bool updated=store.updateUser(targetUser);
	
	if(!updated)
		return crow::response(500,generateError("User account update failed"));
	return(crow::response(200));
}

crow::response deleteUser(PersistentStore& store, const crow::request& req, const std::string uID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " to delete " << uID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//users can only be deleted by admins or themselves
	if(!user.admin && user.id!=uID)
		return crow::response(403,generateError("Not authorized"));
	
	User targetUser;
	if(user.id==uID)
		targetUser=user;
	else{
		targetUser=store.getUser(uID);
		if(!targetUser)
			return crow::response(404,generateError("Not found"));
	}
	
	log_info("Deleting " << targetUser);
	bool deleted=store.removeUser(uID);
	
	if(!deleted)
		return crow::response(500,generateError("User account deletion failed"));
	return(crow::response(200));
}

crow::response listUserVOs(PersistentStore& store, const crow::request& req, const std::string uID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested VO listing for " << uID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	User targetUser;
	if(user.id==uID)
		targetUser=user;
	else{
		User targetUser=store.getUser(uID);
		if(!targetUser)
			return crow::response(404,generateError("Not found"));
	}
	//TODO: can anyone list anyone else's VO memberships?
	
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	result["items"]=store.getUserVOMemberships(uID,true);
	return result;
}

crow::response addUserToVO(PersistentStore& store, const crow::request& req, 
						   const std::string uID, const std::string& voID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to add " << uID << " to " << voID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	User targetUser=store.getUser(uID);
	if(!targetUser)
		return crow::response(404,generateError("User not found"));
	
	//TODO: poperly look up the VO
	//if(!voExists(voID))
	//	return(crow::response(404,generateError("VO not found")));
	
	//TODO: what are the correct authorization requirements?
	
	log_info("Adding " << targetUser << " to " << voID);
	bool success=store.addUserToVO(uID,voID);
	
	if(!success)
		return crow::response(500,generateError("User adition to VO failed"));
	return(crow::response(200));
}

crow::response removeUserFromVO(PersistentStore& store, const crow::request& req, 
								const std::string uID, const std::string& voID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to remove " << uID << " from " << voID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//TODO: identify correct target user
	User targetUser=store.getUser(uID);
	if(!targetUser)
		return crow::response(404,generateError("User not found"));
	
	//TODO: check whether user is in VO
	//if(!voExists(voName))
	//	return(crow::response(404,generateError("VO not found")));
	
	//TODO: what are the correct authorization requirements?
	
	log_info("Removing " << targetUser << " from " << voID);
	bool success=store.removeUserFromVO(uID,voID);
	
	if(!success)
		return crow::response(500,generateError("User adition to VO failed"));
	return(crow::response(200));
}

crow::response findUser(PersistentStore& store, const crow::request& req){
	//this is the requeting user, not the requested user
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested user information for a globus ID");
	if(!user || !user.admin)
		return crow::response(403,generateError("Not authorized"));
	
	if(!req.url_params.get("globus_id"))
		return crow::response(400,generateError("Missing globus ID in request"));
	std::string globusID=req.url_params.get("globus_id");
	
	log_info("Looking up globus ID " << globusID);
	User targetUser=store.findUserByGlobusID(globusID);
	
	if(!targetUser)
		return crow::response(404,generateError("User not found"));
	
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	result["kind"]="User";
	crow::json::wvalue metadata;
	metadata["id"]=targetUser.id;
	metadata["access_token"]=targetUser.token;
	result["metadata"]=std::move(metadata);
	return crow::response(result);
}
