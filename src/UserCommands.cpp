#include "UserCommands.h"

#include "Logging.h"
#include "ServerUtilities.h"

crow::response listUsers(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list users from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//TODO: Are all users are allowed to list all users?

	std::vector<User> users;
	if (auto group = req.url_params.get("group"))
		users = store.listUsersByGroup(group);
	else
		users = store.listUsers();

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(users.size(), alloc);
	for(const User& user : users){
		rapidjson::Value userResult(rapidjson::kObjectType);
		userResult.AddMember("apiVersion", "v1alpha3", alloc);
		userResult.AddMember("kind", "User", alloc);
		rapidjson::Value userData(rapidjson::kObjectType);
		userData.AddMember("id", rapidjson::StringRef(user.id.c_str()), alloc);
		userData.AddMember("name", rapidjson::StringRef(user.name.c_str()), alloc);
		userData.AddMember("email", rapidjson::StringRef(user.email.c_str()), alloc);
		userData.AddMember("phone", rapidjson::StringRef(user.phone.c_str()), alloc);
		userData.AddMember("institution", rapidjson::StringRef(user.institution.c_str()), alloc);
		userResult.AddMember("metadata", userData, alloc);
		resultItems.PushBack(userResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);
	
	return crow::response(to_string(result));
}

crow::response createUser(PersistentStore& store, const crow::request& req){
	//important: user is the user issuing the command, not the user being modified
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to create a user from " << req.remote_endpoint);
	if(!user){
		log_warn(user << " is not authorized to create users");
		return crow::response(403,generateError("Not authorized"));
	}
	
	if(!user.admin){ //only administrators can create new users
		log_warn(user << " is not an admin and so is not allowed to create users");
		return crow::response(403,generateError("Not authorized"));
	}
	
	//unpack the target user info
	rapidjson::Document body;
	try{
		body.Parse(req.body.c_str());
	}catch(std::runtime_error& err){
		log_warn("User creation request body was not valid JSON");
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	
	if(body.IsNull()){
		log_warn("User creation request body was null");
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	if(!body.HasMember("metadata")){
		log_warn("User creation request body was missing metadata");
		return crow::response(400,generateError("Missing user metadata in request"));
	}
	if(!body["metadata"].IsObject()){
		log_warn("User creation request body was not an object");
		return crow::response(400,generateError("Incorrect type for configuration"));
	}
	
	if(!body["metadata"].HasMember("globusID")){
		log_warn("User creation request was missing globus ID");
		return crow::response(400,generateError("Missing user globus ID in request"));
	}
	if(!body["metadata"]["globusID"].IsString()){
		log_warn("Globus ID in user creation request was not a string");
		return crow::response(400,generateError("Incorrect type for user globus ID"));
	}
	if(!body["metadata"].HasMember("name")){
		log_warn("User creation request was missing user name");
		return crow::response(400,generateError("Missing user name in request"));
	}
	if(!body["metadata"]["name"].IsString()){
		log_warn("User name in user creation request was not a string");
		return crow::response(400,generateError("Incorrect type for user name"));
	}
	if(!body["metadata"].HasMember("email")){
		log_warn("User creation request was missing user email address");
		return crow::response(400,generateError("Missing user email in request"));
	}
	if(!body["metadata"]["email"].IsString()){
		log_warn("User email address in user creation request was not a string");
		return crow::response(400,generateError("Incorrect type for user email"));
	}
	if(!body["metadata"].HasMember("phone")){
		log_warn("User creation request was missing user phone number");
		return crow::response(400,generateError("Missing user phone in request"));
	}
	if(!body["metadata"]["phone"].IsString()){
		log_warn("User phone number in user creation request was not a string");
		return crow::response(400,generateError("Incorrect type for user phone"));
	}
	if(!body["metadata"].HasMember("institution")){
		log_warn("User creation request was missing user institution");
		return crow::response(400,generateError("Missing user institution in request"));
	}
	if(!body["metadata"]["institution"].IsString()){
		log_warn("User institution in user creation request was not a string");
		return crow::response(400,generateError("Incorrect type for user institution"));
	}
	if(!body["metadata"].HasMember("admin")){
		log_warn("User creation request was missing admin flag");
		return crow::response(400,generateError("Missing admin flag in request"));
	}
	if(!body["metadata"]["admin"].IsBool()){
		log_warn("Admin flag in user creation request was not a string");
		return crow::response(400,generateError("Incorrect type for user admin flag"));
	}
	
	User targetUser;
	targetUser.id=idGenerator.generateUserID();
	targetUser.token=idGenerator.generateUserToken();
	targetUser.globusID=body["metadata"]["globusID"].GetString();
	targetUser.name=body["metadata"]["name"].GetString();
	targetUser.email=body["metadata"]["email"].GetString();
	targetUser.phone=body["metadata"]["phone"].GetString();
	targetUser.institution=body["metadata"]["institution"].GetString();
	targetUser.admin=body["metadata"]["admin"].GetBool();
	targetUser.valid=true;
	
	if(store.findUserByGlobusID(targetUser.globusID)){
		log_warn("User Globus ID is already registered");
		return crow::response(400,generateError("Globus ID is already registered"));
	}
	
	log_info("Creating " << targetUser);
	bool created=store.addUser(targetUser);
	
	if(!created){
		log_error("Failed to create user account");
		return crow::response(500,generateError("User account creation failed"));
	}

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", rapidjson::StringRef(targetUser.id.c_str()), alloc);
	metadata.AddMember("name", rapidjson::StringRef(targetUser.name.c_str()), alloc);
	metadata.AddMember("email", rapidjson::StringRef(targetUser.email.c_str()), alloc);
	metadata.AddMember("phone", rapidjson::StringRef(targetUser.phone.c_str()), alloc);
	metadata.AddMember("institution", rapidjson::StringRef(targetUser.institution.c_str()), alloc);
	metadata.AddMember("access_token", rapidjson::StringRef(targetUser.token.c_str()), alloc);
	metadata.AddMember("admin", targetUser.admin, alloc);
	rapidjson::Value vos(rapidjson::kArrayType);
	metadata.AddMember("groups", vos, alloc);
	result.AddMember("metadata", metadata, alloc);
	
	return crow::response(to_string(result));
}

crow::response getUserInfo(PersistentStore& store, const crow::request& req, const std::string uID){
	//important: user is the user issuing the command, not the user being modified
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested information about " << uID << " from " << req.remote_endpoint);
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
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "User", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", rapidjson::StringRef(targetUser.id.c_str()), alloc);
	metadata.AddMember("name", rapidjson::StringRef(targetUser.name.c_str()), alloc);
	metadata.AddMember("email", rapidjson::StringRef(targetUser.email.c_str()), alloc);
	metadata.AddMember("phone", rapidjson::StringRef(targetUser.phone.c_str()), alloc);
	metadata.AddMember("institution", rapidjson::StringRef(targetUser.institution.c_str()), alloc);
	metadata.AddMember("access_token", rapidjson::StringRef(targetUser.token.c_str()), alloc);
	metadata.AddMember("admin", targetUser.admin, alloc);
	rapidjson::Value groupMemberships(rapidjson::kArrayType);
	std::vector<std::string> groupMembershipList = store.getUserGroupMemberships(uID,true);
	for (auto group : groupMembershipList) {
		rapidjson::Value entry(rapidjson::kStringType);
		entry.SetString(group, alloc);
		groupMemberships.PushBack(entry, alloc);
	}
	metadata.AddMember("groups", groupMemberships, alloc);
	result.AddMember("metadata", metadata, alloc);
	
	return crow::response(to_string(result));
}

crow::response updateUser(PersistentStore& store, const crow::request& req, const std::string uID){
	//important: user is the user issuing the command, not the user being modified
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to update information about " << uID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//users can only be altered by admins and themselves
	if(!user.admin && user.id!=uID)
		return crow::response(403,generateError("Not authorized"));
	
	User targetUser=store.getUser(uID);
	
	if(!targetUser)
		return crow::response(404,generateError("User not found"));
	
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
	if(body["metadata"].HasMember("phone")){
		if(!body["metadata"]["phone"].IsString())
			return crow::response(400,generateError("Incorrect type for user phone"));
		updatedUser.phone=body["metadata"]["phone"].GetString();
	}
	if(body["metadata"].HasMember("institution")){
		if(!body["metadata"]["institution"].IsString())
			return crow::response(400,generateError("Incorrect type for user institution"));
		updatedUser.institution=body["metadata"]["institution"].GetString();
	}
	if(body["metadata"].HasMember("admin")){
		if(!body["metadata"]["admin"].IsBool())
			return crow::response(400,generateError("Incorrect type for user admin flag"));
		if(!user.admin) //only admins can alter admin rights
			return crow::response(403,generateError("Not authorized"));
		updatedUser.admin=body["metadata"]["admin"].GetBool();
	}
	
	log_info("Updating " << targetUser << " info");
	bool updated=store.updateUser(updatedUser,targetUser);
	
	if(!updated)
		return crow::response(500,generateError("User account update failed"));
	return(crow::response(200));
}

crow::response deleteUser(PersistentStore& store, const crow::request& req, const std::string uID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " to delete " << uID << " from " << req.remote_endpoint);
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
	//Remove the user from any groups
	std::vector<std::string> groupMembershipList = store.getUserGroupMemberships(uID,false);
	for(auto& groupID : groupMembershipList)
		store.removeUserFromGroup(uID,groupID);
	bool deleted=store.removeUser(uID);
	
	if(!deleted)
		return crow::response(500,generateError("User account deletion failed"));
	return(crow::response(200));
}

crow::response listUsergroups(PersistentStore& store, const crow::request& req, const std::string uID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested Group listing for " << uID << " from " << req.remote_endpoint);
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
	//TODO: can anyone list anyone else's Group memberships?

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value groupMemberships(rapidjson::kArrayType);
	std::vector<std::string> groupMembershipList = store.getUserGroupMemberships(uID,true);
	for (auto groupName : groupMembershipList) {
		rapidjson::Value entry(rapidjson::kObjectType);
		entry.AddMember("apiVersion", "v1alpha3", alloc);
		entry.AddMember("kind", "Group", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("id", store.findGroupByName(groupName).id, alloc);
		entry.AddMember("metadata", metadata, alloc);
		groupMemberships.PushBack(entry, alloc);
	}
	result.AddMember("items", groupMemberships, alloc);

	return crow::response(to_string(result));
}

crow::response addUserToGroup(PersistentStore& store, const crow::request& req, 
						   const std::string uID, const std::string& groupID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to add " << uID << " to " << groupID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	User targetUser=store.getUser(uID);
	if(!targetUser)
		return crow::response(404,generateError("User not found"));
	
	Group group=store.getGroup(groupID);
	if(!group)
		return(crow::response(404,generateError("Group not found")));
	
	//Only allow admins and members of the Group to add other users to it
	if(!user.admin && !store.userInGroup(user.id,groupID))
		return crow::response(403,generateError("Not authorized"));
	
	log_info("Adding " << targetUser << " to " << groupID);
	bool success=store.addUserToGroup(uID,groupID);
	
	if(!success)
		return crow::response(500,generateError("User addition to Group failed"));
	return(crow::response(200));
}

crow::response removeUserFromGroup(PersistentStore& store, const crow::request& req, 
								const std::string uID, const std::string& groupID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to remove " << uID << " from " << groupID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	User targetUser=store.getUser(uID);
	if(!targetUser)
		return crow::response(404,generateError("User not found"));
	
	//Only allow admins and members of the Group to remove user from it
	if(!user.admin && !store.userInGroup(user.id,groupID))
		return crow::response(403,generateError("Not authorized"));
	
	log_info("Removing " << targetUser << " from " << groupID);
	bool success=store.removeUserFromGroup(uID,groupID);
	
	if(!success)
		return crow::response(500,generateError("User removal from Group failed"));
	return(crow::response(200));
}

crow::response findUser(PersistentStore& store, const crow::request& req){
	//this is the requesting user, not the requested user
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested user information for a globus ID from " << req.remote_endpoint);
	if(!user || !user.admin)
		return crow::response(403,generateError("Not authorized"));
	
	if(!req.url_params.get("globus_id"))
		return crow::response(400,generateError("Missing globus ID in request"));
	std::string globusID=req.url_params.get("globus_id");
	
	User targetUser=store.findUserByGlobusID(globusID);
	
	if(!targetUser)
		return crow::response(404,generateError("User not found"));

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "User", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", rapidjson::StringRef(targetUser.id.c_str()), alloc);
	metadata.AddMember("access_token", rapidjson::StringRef(targetUser.token.c_str()), alloc);
	result.AddMember("metadata", metadata, alloc);

	return crow::response(to_string(result));
}

crow::response replaceUserToken(PersistentStore& store, const crow::request& req, const std::string uID){
	//important: user is the user issuing the command, not the user being modified
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to replace access token for " << uID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//users can only be altered by admins and themselves
	if(!user.admin && user.id!=uID)
		return crow::response(403,generateError("Not authorized"));
	
	User targetUser=store.getUser(uID);
	
	if(!targetUser)
		return crow::response(404,generateError("User not found"));
	
	log_info("Updating " << targetUser << " access token");
	User updatedUser=targetUser;
	updatedUser.token=idGenerator.generateUserToken();
	bool updated=store.updateUser(updatedUser,targetUser);
	
	if(!updated)
		return crow::response(500,generateError("User account update failed"));
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "User", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", updatedUser.id, alloc);
	metadata.AddMember("access_token", updatedUser.token, alloc);
	result.AddMember("metadata", metadata, alloc);
	
	return crow::response(to_string(result));
}

crow::response whoAreThey(PersistentStore& store, const crow::request& req) {
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested information about themselves from " << req.remote_endpoint);
	// this check should never fail, better safe than sorry
	if(!user)
		return crow::response(403,generateError("Not authorized"));

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "User", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", rapidjson::StringRef(user.id.c_str()), alloc);
	metadata.AddMember("name", rapidjson::StringRef(user.name.c_str()), alloc);
	metadata.AddMember("email", rapidjson::StringRef(user.email.c_str()), alloc);
	metadata.AddMember("phone", rapidjson::StringRef(user.phone.c_str()), alloc);
	metadata.AddMember("institution", rapidjson::StringRef(user.institution.c_str()), alloc);
	metadata.AddMember("access_token", rapidjson::StringRef(user.token.c_str()), alloc);
	metadata.AddMember("admin", user.admin, alloc);
	rapidjson::Value groupMemberships(rapidjson::kArrayType);
	std::vector<std::string> groupMembershipList = store.getUserGroupMemberships(user.id,true);
	for (auto group : groupMembershipList) {
		rapidjson::Value entry(rapidjson::kStringType);
		entry.SetString(group, alloc);
		groupMemberships.PushBack(entry, alloc);
	}
	metadata.AddMember("groups", groupMemberships, alloc);
	result.AddMember("metadata", metadata, alloc);
	
	return crow::response(to_string(result));
}