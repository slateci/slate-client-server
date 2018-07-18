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

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(users.size(), alloc);
	for(const User& user : users){
		rapidjson::Value userResult(rapidjson::kObjectType);
		userResult.AddMember("apiVersion", "v1alpha1", alloc);
		userResult.AddMember("kind", "user", alloc);
		rapidjson::Value userData(rapidjson::kObjectType);
		userData.AddMember("id", rapidjson::StringRef(user.id.c_str()), alloc);
		userData.AddMember("name", rapidjson::StringRef(user.name.c_str()), alloc);
		userData.AddMember("email", rapidjson::StringRef(user.email.c_str()), alloc);
		userResult.AddMember("metadata", userData, alloc);
		resultItems.PushBack(userResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);
	
	return crow::response(to_string(result));
}

crow::response createUser(PersistentStore& store, const crow::request& req){
	//important: user is the user issuing the command, not the user being modified
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to create a user");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//unpack the target user info
	rapidjson::Document body;
	try{
		body.Parse(req.body.c_str());
	}catch(std::runtime_error& err){
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	
	if(body.IsNull())
		return crow::response(400,generateError("Invalid JSON in request body"));
	if(!body.HasMember("metadata"))
		return crow::response(400,generateError("Missing user metadata in request"));
	if(!body["metadata"].IsObject())
		return crow::response(400,generateError("Incorrect type for configuration"));
	
	if(!user.admin) //only administrators can create new users
		return crow::response(403,generateError("Not authorized"));
	
	if(!body["metadata"].HasMember("globusID"))
		return crow::response(400,generateError("Missing user globus ID in request"));
	if(!body["metadata"]["globusID"].IsString())
		return crow::response(400,generateError("Incorrect type for user globus ID"));
	if(!body["metadata"].HasMember("name"))
		return crow::response(400,generateError("Missing user name in request"));
	if(!body["metadata"]["name"].IsString())
		return crow::response(400,generateError("Incorrect type for user name"));
	if(!body["metadata"].HasMember("email"))
		return crow::response(400,generateError("Missing user email in request"));
	if(!body["metadata"]["email"].IsString())
		return crow::response(400,generateError("Incorrect type for user email"));
	if(!body["metadata"].HasMember("admin"))
		return crow::response(400,generateError("Missing user email in request"));
	if(!body["metadata"]["admin"].IsBool())
		return crow::response(400,generateError("Incorrect type for user admin flag"));
	
	User targetUser;
	targetUser.id=idGenerator.generateUserID();
	targetUser.token=idGenerator.generateUserToken();
	targetUser.globusID=body["metadata"]["globusID"].GetString();
	targetUser.name=body["metadata"]["name"].GetString();
	targetUser.email=body["metadata"]["email"].GetString();
	targetUser.admin=body["metadata"]["admin"].GetBool();
	targetUser.valid=true;
	
	if(store.findUserByGlobusID(targetUser.globusID))
		return crow::response(409,generateError("Globus ID is already registered"));
	
	log_info("Creating " << targetUser);
	bool created=store.addUser(targetUser);
	
	if(!created)
		return crow::response(500,generateError("User account creation failed"));

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", rapidjson::StringRef(targetUser.id.c_str()), alloc);
	metadata.AddMember("name", rapidjson::StringRef(targetUser.name.c_str()), alloc);
	metadata.AddMember("email", rapidjson::StringRef(targetUser.email.c_str()), alloc);
	metadata.AddMember("access_token", rapidjson::StringRef(targetUser.token.c_str()), alloc);
	metadata.AddMember("admin", targetUser.admin, alloc);
	rapidjson::Value vos(rapidjson::kArrayType);
        metadata.AddMember("VOs", vos, alloc);
	result.AddMember("metadata", metadata, alloc);
	
	return crow::response(to_string(result));
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

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	result.AddMember("kind", "User", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", rapidjson::StringRef(targetUser.id.c_str()), alloc);
	metadata.AddMember("name", rapidjson::StringRef(targetUser.name.c_str()), alloc);
	metadata.AddMember("email", rapidjson::StringRef(targetUser.email.c_str()), alloc);
	metadata.AddMember("access_token", rapidjson::StringRef(targetUser.token.c_str()), alloc);
	metadata.AddMember("admin", targetUser.admin, alloc);
	rapidjson::Value voMemberships(rapidjson::kArrayType);
	std::vector<std::string> voMembershipList = store.getUserVOMemberships(uID,true);
	for (auto vo : voMembershipList) {
		voMemberships.PushBack(rapidjson::StringRef(vo.c_str()), alloc);
	}
	metadata.AddMember("VOs", voMemberships, alloc);
	result.AddMember("metadata", metadata, alloc);
	
	return crow::response(to_string(result));
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
	rapidjson::Document body;
	try{
		body.Parse(req.body.c_str());
	}catch(std::runtime_error& err){
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	if(body.IsNull())
		return crow::response(400,generateError("Invalid JSON in request body"));
	if(!body.HasMember("metadata"))
		return crow::response(400,generateError("Missing user metadata in request"));
	if(!body["metadata"].IsObject())
		return crow::response(400,generateError("Incorrect type for user metadata"));
	
	if(!targetUser)
		return crow::response(403,generateError("User not found"));
	
	User updatedUser=targetUser;
	
	if(body["metadata"].HasMember("name")){
		if(!body["metadata"]["name"].IsString())
			return crow::response(400,generateError("Incorrect type for user name"));
		updatedUser.name=body["metadata"]["name"].GetString();
	}
	if(body["metadata"].HasMember("email")){
		if(!body["metadata"]["email"].IsString())
			return crow::response(400,generateError("Incorrect type for user email"));
		updatedUser.email=body["metadata"]["email"].GetString();
	}
	if(body["metadata"].HasMember("admin")){
		if(!body["metadata"]["admin"].IsBool())
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

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value voMemberships(rapidjson::kArrayType);
	std::vector<std::string> voMembershipList = store.getUserVOMemberships(uID,true);
	for (auto vo : voMembershipList) {
		voMemberships.PushBack(rapidjson::StringRef(vo.c_str()), alloc);
	}
	result.AddMember("items", voMemberships, alloc);

	return crow::response(to_string(result));
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

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	result.AddMember("kind", "User", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", rapidjson::StringRef(targetUser.id.c_str()), alloc);
	metadata.AddMember("access_token", rapidjson::StringRef(targetUser.token.c_str()), alloc);
	result.AddMember("metadata", metadata, alloc);

	return crow::response(to_string(result));
}
