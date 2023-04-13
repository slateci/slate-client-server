#include "UserCommands.h"

#include "Logging.h"
#include "ServerUtilities.h"
#include "Telemetry.h"

crow::response listUsers(PersistentStore& store, const crow::request& req) {
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	span->AddEvent("Authenticating user");
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to list users from " << req.remote_endpoint);
	if (!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	//TODO: Are all users are allowed to list all users?

	std::vector<User> users;
	if (auto group = req.url_params.get("group")) {
		span->AddEvent("listUsersByGroup");
		users = store.listUsersByGroup(group);
	} else {
		span->AddEvent("listUsers");
		users = store.listUsers();
	}

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(users.size(), alloc);
	for(const User& u : users){
		rapidjson::Value userResult(rapidjson::kObjectType);
		userResult.AddMember("apiVersion", "v1alpha3", alloc);
		userResult.AddMember("kind", "User", alloc);
		rapidjson::Value userData(rapidjson::kObjectType);
		userData.AddMember("id", rapidjson::StringRef(u.id.c_str()), alloc);
		userData.AddMember("name", rapidjson::StringRef(u.name.c_str()), alloc);
		userData.AddMember("email", rapidjson::StringRef(u.email.c_str()), alloc);
		userData.AddMember("phone", rapidjson::StringRef(u.phone.c_str()), alloc);
		userData.AddMember("institution", rapidjson::StringRef(u.institution.c_str()), alloc);
		userResult.AddMember("metadata", userData, alloc);
		resultItems.PushBack(userResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);
	span->End();
	return crow::response(to_string(result));
}

crow::response createUser(PersistentStore& store, const crow::request& req){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	span->AddEvent("Authenticating user");
	//important: user is the user issuing the command, not the user being modified
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to create a user from " << req.remote_endpoint);
	if(!user){
		log_warn(user << " is not authorized to create users");
		setWebSpanError(span, "Not authorized to create users", 403);
		span->End();
		return crow::response(403,generateError("Not authorized"));
	}
	
	if(!user.admin){ //only administrators can create new users
		log_warn(user << " is not an admin and so is not allowed to create users");
		setWebSpanError(span, "Not an admin", 403);
		span->End();
		return crow::response(403,generateError("Not authorized"));
	}
	
	//unpack the target user info
	rapidjson::Document body;
	try{
		body.Parse(req.body.c_str());
	}catch(std::runtime_error& err){
		log_warn("User creation request body was not valid JSON");
		setWebSpanError(span, "Invalid JSON in request body", 400);
		span->End();
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	
	if(body.IsNull()){
		log_warn("User creation request body was null");
		setWebSpanError(span, "User creation request body was null", 400);
		span->End();
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	if(!body.HasMember("metadata")){
		log_warn("User creation request body was missing metadata field");
		setWebSpanError(span, "User creation request body was missing metadata field", 400);
		span->End();
		return crow::response(400,generateError("Missing user metadata in request"));
	}
	if(!body["metadata"].IsObject()){
		log_warn("User creation in request body, metadata field was not an object");
		setWebSpanError(span, "User creation in request body, metadata field was not an object", 400);
		span->End();
		return crow::response(400,generateError("Incorrect type for configuration"));
	}
	
	if(!body["metadata"].HasMember("globusID")){
		log_warn("User creation request was missing globus ID");
		setWebSpanError(span, "User creation request was missing globus ID", 400);
		span->End();
		return crow::response(400,generateError("Missing user globus ID in request"));
	}
	if(!body["metadata"]["globusID"].IsString()){
		log_warn("Globus ID in user creation request was not a string");
		setWebSpanError(span, "Globus ID in user creation request was not a string", 400);
		span->End();
		return crow::response(400,generateError("Incorrect type for user globus ID"));
	}
	if(!body["metadata"].HasMember("name")){
		log_warn("User creation request was missing user name");
		setWebSpanError(span, "Globus ID in user creation request was not a string", 400);
		span->End();
		return crow::response(400,generateError("Missing user name in request"));
	}
	if(!body["metadata"]["name"].IsString()){
		log_warn("User name in user creation request was not a string");
		setWebSpanError(span, "User name in user creation request was not a string", 400);
		span->End();
		return crow::response(400,generateError("Incorrect type for user name"));
	}
	if(!body["metadata"].HasMember("email")){
		log_warn("User creation request was missing user email address");
		setWebSpanError(span, "User creation request was missing user email address", 400);
		span->End();
		return crow::response(400,generateError("Missing user email in request"));
	}
	if(!body["metadata"]["email"].IsString()){
		log_warn("User email address in user creation request was not a string");
		setWebSpanError(span, "User email address in user creation request was not a string", 400);
		span->End();
		return crow::response(400,generateError("Incorrect type for user email"));
	}
	if(!body["metadata"].HasMember("phone")){
		log_warn("User creation request was missing user phone number");
		setWebSpanError(span, "User creation request was missing user phone number", 400);
		span->End();
		return crow::response(400,generateError("Missing user phone in request"));
	}
	if(!body["metadata"]["phone"].IsString()){
		log_warn("User phone number in user creation request was not a string");
		setWebSpanError(span, "User phone number in user creation request was not a string", 400);
		span->End();
		return crow::response(400,generateError("Incorrect type for user phone"));
	}
	if(!body["metadata"].HasMember("institution")){
		log_warn("User creation request was missing user institution");
		setWebSpanError(span, "User creation request was missing user institution", 400);
		span->End();
		return crow::response(400,generateError("Missing user institution in request"));
	}
	if(!body["metadata"]["institution"].IsString()){
		log_warn("User institution in user creation request was not a string");
		setWebSpanError(span, "User institution in user creation request was not a string", 400);
		span->End();
		return crow::response(400,generateError("Incorrect type for user institution"));
	}
	if(!body["metadata"].HasMember("admin")){
		log_warn("User creation request was missing admin flag");
		setWebSpanError(span, "User creation request was missing admin flag", 400);
		span->End();
		return crow::response(400,generateError("Missing admin flag in request"));
	}
	if(!body["metadata"]["admin"].IsBool()){
		log_warn("Admin flag in user creation request was not a string");
		setWebSpanError(span, "Admin flag in user creation request was not a string", 400);
		span->End();
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
		setWebSpanError(span, "User Globus ID is already registered", 400);
		span->End();
		return crow::response(400,generateError("Globus ID is already registered"));
	}
	log_info("Creating " << targetUser);
	span->AddEvent("Creating user");

	bool created=store.addUser(targetUser);

	if(!created){
		log_error("Failed to create user account");
		setWebSpanError(span, "Failed to create user account", 500);
		span->End();
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

	span->End();
	return crow::response(to_string(result));
}

crow::response getUserInfo(PersistentStore& store, const crow::request& req, const std::string uID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	span->AddEvent("Authenticating user");
	//important: user is the user issuing the command, not the user being modified
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " requested information about " << uID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	//users can only be examined by admins or themselves
	if(!user.admin && user.id!=uID) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}

	User targetUser=store.getUser(uID);

	if(!targetUser) {
		setWebSpanError(span, "User not found", 404);
		span->End();
		return crow::response(404, generateError("Not found"));
	}
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
	span->End();
	return crow::response(to_string(result));
}

crow::response whoAreThey(PersistentStore& store, const crow::request& req) {
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	span->AddEvent("Authenticating user");
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " requested information about themselves from " << req.remote_endpoint);
	// this check should never fail, better safe than sorry
	if(!user) {
		const std::string& errMsg = "User not found";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}

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

	span->End();
	return crow::response(to_string(result));
}

crow::response updateUser(PersistentStore& store, const crow::request& req, const std::string uID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	span->AddEvent("Authenticating user");
	//important: user is the user issuing the command, not the user being modified
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " requested to update information about " << uID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));

	}
	//users can only be altered by admins and themselves
	if(!user.admin && user.id!=uID) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	User targetUser=store.getUser(uID);
	
	if(!targetUser) {
		setWebSpanError(span, "User not found", 404);
		span->End();
		return crow::response(404, generateError("User not found"));
	}
	
	//unpack the target user info
	rapidjson::Document body;
	try{
		body.Parse(req.body.c_str());
	}catch(std::runtime_error& err){
		const std::string& errMsg = "Invalid JSON in request body";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400,generateError(errMsg));
	}
	if(body.IsNull()) {
		const std::string &errMsg = "Invalid JSON in request body";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	if(!body.HasMember("metadata")) {
		const std::string &errMsg = "Missing user metadata in request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"].IsObject()) {
		const std::string &errMsg = "Incorrect type for user metadata";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	
	User updatedUser=targetUser;
	
	if(body["metadata"].HasMember("name")){
		if(!body["metadata"]["name"].IsString()) {
			const std::string &errMsg = "Incorrect type for user name";
			setWebSpanError(span, errMsg, 400);
			span->End();
			return crow::response(400, generateError(errMsg));
		}
		updatedUser.name=body["metadata"]["name"].GetString();
	}
	if(body["metadata"].HasMember("email")){
		if(!body["metadata"]["email"].IsString()) {
			const std::string &errMsg = "Incorrect type for user email";
			setWebSpanError(span, errMsg, 400);
			span->End();
			return crow::response(400, generateError(errMsg));
		}
		updatedUser.email=body["metadata"]["email"].GetString();
	}
	if(body["metadata"].HasMember("phone")){
		if(!body["metadata"]["phone"].IsString()) {
			const std::string &errMsg = "Incorrect type for user phone";
			setWebSpanError(span, errMsg, 400);
			span->End();
			return crow::response(400, generateError(errMsg));
		}
		updatedUser.phone=body["metadata"]["phone"].GetString();
	}
	if(body["metadata"].HasMember("institution")){
		if(!body["metadata"]["institution"].IsString()) {
			const std::string &errMsg = "Incorrect type for user institution";
			setWebSpanError(span, errMsg, 400);
			span->End();
			return crow::response(400, generateError(errMsg));
		}
		updatedUser.institution=body["metadata"]["institution"].GetString();
	}
	if(body["metadata"].HasMember("admin")){
		if(!body["metadata"]["admin"].IsBool()) {
			const std::string &errMsg = "Incorrect type for user admin flag";
			setWebSpanError(span, errMsg, 400);
			span->End();
			return crow::response(400, generateError(errMsg));
		}
		if(!user.admin) { //only admins can alter admin rights
			const std::string& errMsg = "User not authorized";
			setWebSpanError(span, errMsg, 403);
			span->End();
			log_error(errMsg);
			return crow::response(403, generateError(errMsg));
		}
		updatedUser.admin=body["metadata"]["admin"].GetBool();
	}
	
	log_info("Updating " << targetUser << " info");
	bool updated=store.updateUser(updatedUser,targetUser);
	
	if(!updated) {
		setWebSpanError(span, "User account update failed", 500);
		span->End();
		return crow::response(500, generateError("User account update failed"));
	}
	span->End();
	return(crow::response(200));
}

crow::response deleteUser(PersistentStore& store, const crow::request& req, const std::string uID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	span->AddEvent("Authenticating user");
	//important: user is the user issuing the command, not the user being modified
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " to delete " << uID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string &errMsg = "Not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		return crow::response(403, generateError(errMsg));
	}
	//users can only be deleted by admins or themselves
	if(!user.admin && user.id!=uID) {
		const std::string &errMsg = "Not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		return crow::response(403, generateError(errMsg));
	}
	
	User targetUser;
	if (user.id == uID) {
		targetUser = user;
	} else {
		targetUser = store.getUser(uID);
		if (!targetUser) {
			const std::string &errMsg = "User not found";
			setWebSpanError(span, errMsg, 404);
			span->End();
			return crow::response(404, generateError(errMsg));
		}
	}
	
	log_info("Deleting " << targetUser);
	//Remove the user from any groups
	std::vector<std::string> groupMembershipList = store.getUserGroupMemberships(uID,false);
	for (auto &groupID: groupMembershipList) {
		store.removeUserFromGroup(uID, groupID);
	}
	bool deleted=store.removeUser(uID);
	
	if(!deleted) {
		const std::string &errMsg = "User account deletion failed";
		setWebSpanError(span, errMsg, 500);
		span->End();
		return crow::response(500, generateError(errMsg));
	}
	
	//send email notification
	EmailClient::Email message;
	message.fromAddress="noreply@slate.io";
	message.toAddresses={targetUser.email};
	message.subject="SLATE account deleted";
	message.body="This is an automatic notification that your SLATE user "
	"account ("+targetUser.name+", "+targetUser.id+") has been deleted.";
	store.getEmailClient().sendEmail(message);
	span->End();
	return(crow::response(200));
}

crow::response listUsergroups(PersistentStore& store, const crow::request& req, const std::string uID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	span->AddEvent("Authenticating user");
	//important: user is the user issuing the command, not the user being modified
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " requested Group listing for " << uID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string &errMsg = "Not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		return crow::response(403, generateError(errMsg));
	}


	if (user.id == uID) {
		User targetUser;
		targetUser = user;
	} else {
		User targetUser = store.getUser(uID);
		if (!targetUser) {
			const std::string &errMsg = "User not found";
			setWebSpanError(span, errMsg, 404);
			span->End();
			return crow::response(404, generateError(errMsg));
		}
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
	span->End();
	return crow::response(to_string(result));
}

crow::response addUserToGroup(PersistentStore& store, const crow::request& req, 
						   const std::string uID, const std::string& groupID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	span->AddEvent("Authenticating user");
	//important: user is the user issuing the command, not the user being modified
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " requested to add " << uID << " to " << groupID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string &errMsg = "Not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		return crow::response(403, generateError(errMsg));
	}
	
	User targetUser=store.getUser(uID);
	if(!targetUser) {
		const std::string &errMsg = "User not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		return crow::response(404, generateError(errMsg));
	}
	
	Group group=store.getGroup(groupID);
	if(!group) {
		const std::string &errMsg = "Group not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		return crow::response(404, generateError(errMsg));
	}
	
	//Only allow admins and members of the Group to add other users to it
	if(!user.admin && !store.userInGroup(user.id,groupID)) {
		const std::string &errMsg = "Not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		return crow::response(403, generateError(errMsg));
	}
	
	log_info("Adding " << targetUser << " to " << groupID);
	bool success=store.addUserToGroup(uID,groupID);
	
	if(!success) {
		const std::string &errMsg = "User addition to Group failed";
		setWebSpanError(span, errMsg, 500);
		span->End();
		return crow::response(500, generateError(errMsg));
	}
	span->End();
	return(crow::response(200));
}

crow::response removeUserFromGroup(PersistentStore& store, const crow::request& req, 
								const std::string uID, const std::string& groupID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	span->AddEvent("Authenticating user");
	//important: user is the user issuing the command, not the user being modified
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " requested to remove " << uID << " from " << groupID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string &errMsg = "Not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		return crow::response(403, generateError(errMsg));
	}
	
	User targetUser=store.getUser(uID);
	if(!targetUser) {
		const std::string &errMsg = "User not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		return crow::response(404, generateError(errMsg));
	}

	//Only allow admins and members of the Group to remove user from it
	if(!user.admin && !store.userInGroup(user.id,groupID)) {
		const std::string &errMsg = "Not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		return crow::response(403, generateError(errMsg));

	}

	log_info("Removing " << targetUser << " from " << groupID);
	bool success=store.removeUserFromGroup(uID,groupID);
	
	if(!success) {
		const std::string &errMsg = "User removal from Group failed";
		setWebSpanError(span, errMsg, 500);
		span->End();
		return crow::response(500, generateError(errMsg));
	}
	span->End();
	return(crow::response(200));
}

crow::response findUser(PersistentStore& store, const crow::request& req){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	span->AddEvent("Authenticating user");
	//this is the requesting user, not the requested user
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " requested user information for a globus ID from " << req.remote_endpoint);
	if(!user || !user.admin) {
		const std::string &errMsg = "Not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		return crow::response(403, generateError(errMsg));
	}
	
	if(!req.url_params.get("globus_id")) {
		const std::string &errMsg = "Missing globus ID in request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	std::string globusID=req.url_params.get("globus_id");
	
	User targetUser=store.findUserByGlobusID(globusID);
	
	if(!targetUser) {
		const std::string &errMsg = "User not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		return crow::response(404, generateError(errMsg));
	}

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "User", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", rapidjson::StringRef(targetUser.id.c_str()), alloc);
	metadata.AddMember("access_token", rapidjson::StringRef(targetUser.token.c_str()), alloc);
	result.AddMember("metadata", metadata, alloc);

	span->End();
	return crow::response(to_string(result));
}

crow::response replaceUserToken(PersistentStore& store, const crow::request& req, const std::string uID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	span->AddEvent("Authenticating user");
	//important: user is the user issuing the command, not the user being modified
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " requested to replace access token for " << uID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string &errMsg = "Not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		return crow::response(403, generateError(errMsg));
	}
	//users can only be altered by admins and themselves
	if(!user.admin && user.id!=uID) {
		const std::string &errMsg = "Not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		return crow::response(403, generateError(errMsg));
	}
	
	User targetUser=store.getUser(uID);
	
	if(!targetUser) {
		const std::string &errMsg = "User not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		return crow::response(404, generateError(errMsg));
	}
	
	log_info("Updating " << targetUser << " access token");
	User updatedUser=targetUser;
	updatedUser.token=idGenerator.generateUserToken();
	bool updated=store.updateUser(updatedUser,targetUser);
	
	if(!updated) {
		const std::string &errMsg = "User account update failed";
		setWebSpanError(span, errMsg, 500);
		span->End();
		return crow::response(500, generateError(errMsg));
	}
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "User", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", updatedUser.id, alloc);
	metadata.AddMember("access_token", updatedUser.token, alloc);
	result.AddMember("metadata", metadata, alloc);
	
	//send email notification
	EmailClient::Email message;
	message.fromAddress="noreply@slate.io";
	message.toAddresses={targetUser.email};
	message.subject="SLATE account credentials updated";
	message.body="This is an automatic notification that your SLATE user "
	"account ("+targetUser.name+", "+targetUser.id+") has had its access token "
	"replaced. This should not affect how you log into the SLATE web portal, "
	"but if you use the slate CLI tool you will need to download your updated "
	"token from https://portal.slateci.io/cli";
	store.getEmailClient().sendEmail(message);
	span->End();
	return crow::response(to_string(result));
}
