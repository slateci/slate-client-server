#include "GroupCommands.h"

#include <boost/lexical_cast.hpp>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"

#include "Logging.h"
#include "ServerUtilities.h"
#include "KubeInterface.h"
#include "ApplicationInstanceCommands.h"
#include "ClusterCommands.h"
#include "SecretCommands.h"
#include "server_version.h"

namespace{
	const char* scienceFields[]={
		"Resource Provider",
		"Astronomy",
		"Astrophysics",
		"Biology",
		"Biochemistry",
		"Bioinformatics",
		"Biomedical research",
		"Biophysics",
		"Botany",
		"Cellular Biology",
		"Ecology",
		"Evolutionary Biology",
		"Microbiology",
		"Molecular Biology",
		"Neuroscience",
		"Physiology",
		"Structural Biology",
		"Zoology",
		"Chemistry",
		"Biochemistry",
		"Physical Chemistry",
		"Earth Sciences",
		"Economics",
		"Education",
		"Educational Psychology",
		"Engineering",
		"Electronic Engineering",
		"Nanoelectronics",
		"Mathematics & Computer Science",
		"Computer Science",
		"Geographic Information Science",
		"Information Theory",
		"Mathematics",
		"Medicine",
		"Medical Imaging",
		"Neuroscience",
		"Physiology",
		"Logic",
		"Statistics",
		"Physics",
		"Accelerator Physics",
		"Astro-particle Physics",
		"Astrophysics",
		"Biophysics",
		"Computational Condensed Matter Physics",
		"Gravitational Physics",
		"High Energy Physics",
		"Neutrino Physics",
		"Nuclear Physics",
		"Physical Chemistry",
		"Psychology",
		"Child Psychology",
		"Educational Psychology",
		"Materials Science",
		"Multidisciplinary",
		"Network Science",
		"Technology",
	};
	
	///Normalizes a possible field of science string to the matching value in
	///the official list, or returns an empty string if matching failed. 
	std::string normalizeScienceField(const std::string& raw){
		//Use a dumb linear scan so we don't need top worry about the list being
		//ordered. This isn't very efficient, but also shouldn't be called very 
		//often.
		for(const std::string field : scienceFields){
			if(field.size()!=raw.size())
				continue;
			bool match=true;
			//simple-minded case-insensitive compare.
			//TODO: will this break horribly on non-ASCII UTF-8?
			for(std::size_t i=0; i<field.size(); i++){
				if(std::tolower(raw[i])!=std::tolower(field[i])){
					match=false;
					break;
				}
			}
			if(match)
				return field;
		}
		return "";
	}
}

crow::response listGroups(PersistentStore& store, const crow::request& req){
	using namespace std::chrono;
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " requested to list groups from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	//All users are allowed to list groups

	std::vector<Group> vos;

	if (req.url_params.get("user"))
		vos=store.listGroupsForUser(user.id);
	else
		vos=store.listGroups();

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(vos.size(), alloc);
	for (const Group& group : vos){
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("id", rapidjson::StringRef(group.id.c_str()), alloc);
		metadata.AddMember("name", rapidjson::StringRef(group.name.c_str()), alloc);
		metadata.AddMember("email", rapidjson::StringRef(group.email.c_str()), alloc);
		metadata.AddMember("phone", rapidjson::StringRef(group.phone.c_str()), alloc);
		metadata.AddMember("scienceField", rapidjson::StringRef(group.scienceField.c_str()), alloc);
		metadata.AddMember("description", rapidjson::StringRef(group.description.c_str()), alloc);

		rapidjson::Value groupResult(rapidjson::kObjectType);
		groupResult.AddMember("apiVersion", "v1alpha3", alloc);
		groupResult.AddMember("kind", "Group", alloc);
		groupResult.AddMember("metadata", metadata, alloc);
		resultItems.PushBack(groupResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);
	
	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	log_info("group listing completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
	span->End();
	return crow::response(to_string(result));
}

crow::response createGroup(PersistentStore& store, const crow::request& req){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to create a Group from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	//TODO: Are all users allowed to create/register groups?
	//TODO: What other information is required to register a Group?
	
	//unpack the target user info
	rapidjson::Document body;
	try{
		body.Parse(req.body.c_str());
	} catch(std::runtime_error& err) {
		setWebSpanError(span, std::string("Invalid JSON in body, exception: ") + err.what(), 400);
		span->End();
		return crow::response(400,generateError("Invalid JSON in request body"));
	}

	if(body.IsNull()) {
		const std::string& errMsg = "Invalid JSON in request body";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	if(!body.HasMember("metadata")) {
		const std::string& errMsg = "Missing user metadata in request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"].IsObject()) {
		const std::string& errMsg = "Incorrect type for configuration";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	
	if(!body["metadata"].HasMember("name")) {
		const std::string& errMsg = "Missing Group name in request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"]["name"].IsString()) {
		const std::string& errMsg = "Incorrect type for Group name";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	
	if(body["metadata"].HasMember("email") && !body["metadata"]["email"].IsString()) {
		const std::string& errMsg = "Incorrect type for Group email";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	if(body["metadata"].HasMember("phone") && !body["metadata"]["phone"].IsString()) {
		const std::string& errMsg = "Incorrect type for Group phone";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	
	if(!body["metadata"].HasMember("scienceField")) {
		const std::string &errMsg = "Missing Group scienceField in request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"]["scienceField"].IsString()) {
		const std::string& errMsg = "Incorrect type for Group scienceField";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
		
	if(body["metadata"].HasMember("description") && !body["metadata"]["description"].IsString()) {
		const std::string& errMsg = "Incorrect type for Group description";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	
	Group group;
	group.id=idGenerator.generateGroupID();
	
	group.name=body["metadata"]["name"].GetString();
	if(group.name.empty()) {
		const std::string& errMsg = "Group names may not be the empty string";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	if(!validTagGroupName(group.name)) {
		const std::string& errMsg = "Group names may only contain [a-z], [0-9] and -";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	if(group.name.back()=='-') {
		const std::string& errMsg = "Group names may not end with a dash";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	if(group.name.size()>54) {
		const std::string& errMsg = "Group names may not be more than 54 characters long";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	if(group.name.find(IDGenerator::groupIDPrefix)==0) {
		const std::string& errMsg = "Group names may not begin with " + IDGenerator::groupIDPrefix;
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	if(store.findGroupByName(group.name)) {
		const std::string& errMsg = "Group name is already in use";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	
	if(body["metadata"].HasMember("email"))
		group.email=body["metadata"]["email"].GetString();
	else
		group.email=user.email;
	if(group.email.empty())
		group.email=" "; //Dynamo will get upset if a string is empty
		
	if(body["metadata"].HasMember("phone"))
		group.phone=body["metadata"]["phone"].GetString();
	else
		group.phone=user.phone;
	if(group.phone.empty())
		group.phone=" "; //Dynamo will get upset if a string is empty
	
	if(body["metadata"].HasMember("scienceField"))
		group.scienceField=normalizeScienceField(body["metadata"]["scienceField"].GetString());
	if(group.scienceField.empty()) {
		const std::string& errMsg = "Unrecognized value for Group scienceField";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError("Unrecognized value for Group scienceField\n"
		                                         "See http://slateci.io/docs/science-fields for a list of accepted values"));
	}
	
	if(body["metadata"].HasMember("description"))
		group.description=body["metadata"]["description"].GetString();
	if(group.description.empty())
		group.description=" "; //Dynamo will get upset if a string is empty
	
	group.valid=true;
	
	log_info("Creating Group " << group);
	bool created=store.addGroup(group);
	if(!created) {
		const std::string& errMsg = "Group creation failed";
		setWebSpanError(span, errMsg, 500);
		span->End();
		return crow::response(500, generateError("Group creation failed"));
	}
	
	//Make the creating user an initial member of the group
	bool added=store.addUserToGroup(user.id, group.id);
	if(!added){
		//TODO: possible problem: If we get here, we may end up with a valid group
		//but with no members and not return its ID either
		auto problem="Failed to add creating user "+
		             boost::lexical_cast<std::string>(user)+" to new Group "+
		             boost::lexical_cast<std::string>(group);
		log_error(problem);
		const std::string& errMsg = problem;
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(500,generateError(problem));
	}
	
	log_info("Created " << group << " on behalf of " << user);

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "Group", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", rapidjson::StringRef(group.id.c_str()), alloc);
	metadata.AddMember("name", rapidjson::StringRef(group.name.c_str()), alloc);
	metadata.AddMember("email", rapidjson::StringRef(group.email.c_str()), alloc);
	metadata.AddMember("phone", rapidjson::StringRef(group.phone.c_str()), alloc);
	metadata.AddMember("scienceField", rapidjson::StringRef(group.scienceField.c_str()), alloc);
	metadata.AddMember("description", rapidjson::StringRef(group.description.c_str()), alloc);
	result.AddMember("metadata", metadata, alloc);

	span->End();
	return crow::response(to_string(result));
}

crow::response getGroupInfo(PersistentStore& store, const crow::request& req, const std::string& groupID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " requested information about " << groupID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	//Any user in the system may query a Group's information
	
	Group group = store.getGroup(groupID);
	if(!group) {
		const std::string& errMsg = "Group not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		return crow::response(404, generateError(errMsg));
	}
	span->SetAttribute("group", group.name);

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", rapidjson::StringRef(group.id.c_str()), alloc);
	metadata.AddMember("name", rapidjson::StringRef(group.name.c_str()), alloc);
	metadata.AddMember("email", rapidjson::StringRef(group.email.c_str()), alloc);
	metadata.AddMember("phone", rapidjson::StringRef(group.phone.c_str()), alloc);
	metadata.AddMember("scienceField", rapidjson::StringRef(group.scienceField.c_str()), alloc);
	metadata.AddMember("description", rapidjson::StringRef(group.description.c_str()), alloc);
	result.AddMember("kind", "Group", alloc);
	result.AddMember("metadata", metadata, alloc);

	span->End();
	return crow::response(to_string(result));
}

crow::response updateGroup(PersistentStore& store, const crow::request& req, const std::string& groupID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " requested to update " << groupID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	//Only admins and members of a Group can alter it
	if(!user.admin && !store.userInGroup(user.id,groupID)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	Group targetGroup = store.getGroup(groupID);
	if(!targetGroup) {
		const std::string& errMsg = "Group not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		return crow::response(404, generateError(errMsg));
	}
	span->SetAttribute("group", targetGroup.name);

	//unpack the new Group info
	rapidjson::Document body;
	try{
		body.Parse(req.body.c_str());
	}catch(std::runtime_error& err){
		const std::string& errMsg = "Invalid JSON in request body";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	if(body.IsNull()) {
		const std::string& errMsg = "Invalid JSON in request body";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	if(!body.HasMember("metadata")) {
		const std::string& errMsg = "Missing Group metadata in request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"].IsObject()) {
		const std::string& errMsg = "Incorrect type for metadata";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
		
	bool doUpdate=false;
	if(body["metadata"].HasMember("email")){
		if(!body["metadata"]["email"].IsString()) {
			const std::string& errMsg = "Incorrect type for email";
			setWebSpanError(span, errMsg, 400);
			span->End();
			return crow::response(400, generateError(errMsg));
		}
		targetGroup.email=body["metadata"]["email"].GetString();
		doUpdate=true;
	}
	if(body["metadata"].HasMember("phone")){
		if(!body["metadata"]["phone"].IsString()) {
			const std::string& errMsg = "Incorrect type for phone";
			setWebSpanError(span, errMsg, 400);
			span->End();
			return crow::response(400, generateError(errMsg));
		}
		targetGroup.phone=body["metadata"]["phone"].GetString();
		doUpdate=true;
	}
	if(body["metadata"].HasMember("scienceField")){
		if(!body["metadata"]["scienceField"].IsString()) {
			const std::string& errMsg = "Incorrect type for scienceField";
			setWebSpanError(span, errMsg, 400);
			span->End();
			return crow::response(400, generateError(errMsg));
		}
		targetGroup.scienceField=normalizeScienceField(body["metadata"]["scienceField"].GetString());
		if(targetGroup.scienceField.empty()) {
			const std::string& errMsg = "Unrecognized value for Group scienceField";
			setWebSpanError(span, errMsg, 400);
			span->End();
			return crow::response(400, generateError(errMsg));
		}
		doUpdate=true;
	}
	if(body["metadata"].HasMember("description")){
		if(!body["metadata"]["description"].IsString()) {
			const std::string& errMsg = "Incorrect type for description";
			setWebSpanError(span, errMsg, 400);
			span->End();
			return crow::response(400, generateError(errMsg));
		}
		targetGroup.description=body["metadata"]["description"].GetString();
		doUpdate=true;
	}
	
	if(!doUpdate){
		log_info("Requested update to " << targetGroup << " is trivial");
		span->End();
		return(crow::response(200));
	}
	
	log_info("Updating " << targetGroup);
	bool success=store.updateGroup(targetGroup);
	
	if(!success){
		std::ostringstream errMsg;
		errMsg << "Failed to update " << targetGroup;
		setWebSpanError(span, errMsg.str(), 500);
		span->End();
		return crow::response(500, generateError(errMsg.str()));
	}

	span->End();
	return(crow::response(200));
}

crow::response deleteGroup(PersistentStore& store, const crow::request& req, const std::string& groupID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " requested to delete " << groupID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	Group targetGroup = store.getGroup(groupID);
	if(!targetGroup) {
		const std::string& errMsg = "Group not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		return crow::response(404, generateError(errMsg));
	}
	span->SetAttribute("group", targetGroup.name);

	//Only admins and members of a Group can delete it
	if(!user.admin && !store.userInGroup(user.id,groupID)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	log_info("Deleting " << targetGroup);
	bool deleted = store.removeGroup(targetGroup.id);

	if (!deleted) {
		const std::string& errMsg = "Group deletion failed";
		setWebSpanError(span, errMsg, 500);
		span->End();
		return crow::response(500, generateError(errMsg));
	}
	
	std::vector<std::future<void>> work;
	
	// Remove all instances owned by the group
	for(auto& instance : store.listApplicationInstancesByClusterOrGroup(targetGroup.id,""))
		work.emplace_back(std::async(std::launch::async,[&store,instance](){ internal::deleteApplicationInstance(store,instance,true); }));
	
	// Remove all secrets owned by the group
	for(auto& secret : store.listSecrets(targetGroup.id,""))
		work.emplace_back(std::async(std::launch::async,[&store,secret](){ internal::deleteSecret(store,secret,true); }));
	
	// Remove the Group's namespace on each cluster
	auto cluster_names = store.listClusters();
	for (auto& cluster : cluster_names){
		work.emplace_back(std::async(std::launch::async,[&store,&targetGroup,cluster](){
			try{
				kubernetes::kubectl_delete_namespace(*store.configPathForCluster(cluster.id), targetGroup);
			}
			catch(std::runtime_error& err){
				log_error("Failed to delete " << targetGroup << " namespace from " << cluster << ": " << err.what());
			}
		}));
	}
	
	//make sure all instances, secrets, and namespaces are deleted before
	//deleting any clusters, since some of the other objects may be on clusters
	//to be deleted
	for(auto& item : work)
		item.wait();
	work.clear();
	
	// Remove all clusters owned by the group
	for(auto& cluster : cluster_names){
		if(cluster.owningGroup==targetGroup.id)
			work.emplace_back(std::async(std::launch::async,[&store,cluster](){
				internal::deleteCluster(store,cluster,true);
			}));
	}
	
	//make sure all cluster deletions are done
	for(auto& item : work)
		item.wait();

	span->End();
	return(crow::response(200));
}

crow::response listGroupMembers(PersistentStore& store, const crow::request& req, const std::string& groupID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to list members of " << groupID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	Group targetGroup = store.getGroup(groupID);
	if(!targetGroup) {
		const std::string& errMsg = "Group not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}
	span->SetAttribute("group", targetGroup.name);
	//Only admins and members of a Group can list its members
	if(!user.admin && !store.userInGroup(user.id,targetGroup.id)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	auto userIDs=store.getMembersOfGroup(targetGroup.id);
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(userIDs.size(), alloc);
	for(const std::string& userID : userIDs){
		User user=store.getUser(userID);
		rapidjson::Value userResult(rapidjson::kObjectType);
		userResult.AddMember("apiVersion", "v1alpha3", alloc);
		userResult.AddMember("kind", "User", alloc);
		rapidjson::Value userData(rapidjson::kObjectType);
		userData.AddMember("id", user.id, alloc);
		userData.AddMember("name", user.name, alloc);
		userData.AddMember("email", user.email, alloc);
		userData.AddMember("phone", user.phone, alloc);
		userData.AddMember("institution", user.institution, alloc);
		userResult.AddMember("metadata", userData, alloc);
		resultItems.PushBack(userResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);
	span->End();
	return crow::response(to_string(result));
}

crow::response listGroupClusters(PersistentStore& store, const crow::request& req, const std::string& groupID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to list clusters owned by " << groupID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	Group targetGroup = store.getGroup(groupID);
	if(!targetGroup) {
		const std::string& errMsg = "Group not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}
	span->SetAttribute("group", targetGroup.name);

	//anyone can list a Group's clusters?
	
	auto clusterIDs=store.clustersOwnedByGroup(targetGroup.id);
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(clusterIDs.size(), alloc);
	for(const std::string& clusterID : clusterIDs){
		Cluster cluster=store.getCluster(clusterID);
		rapidjson::Value clusterResult(rapidjson::kObjectType);
		clusterResult.AddMember("apiVersion", "v1alpha3", alloc);
		clusterResult.AddMember("kind", "Cluster", alloc);
		rapidjson::Value clusterData(rapidjson::kObjectType);
		clusterData.AddMember("id", cluster.id, alloc);
		clusterData.AddMember("name", cluster.name, alloc);
		clusterData.AddMember("owningGroup", targetGroup.name, alloc);
		clusterData.AddMember("owningOrganization", cluster.owningOrganization, alloc);
		clusterResult.AddMember("metadata", clusterData, alloc);
		resultItems.PushBack(clusterResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);
	span->End();
	return crow::response(to_string(result));
}
