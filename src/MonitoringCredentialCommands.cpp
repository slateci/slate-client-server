#include "MonitoringCredentialCommands.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"

#include "Logging.h"
#include "ServerUtilities.h"

crow::response listMonitoringCredentials(PersistentStore& store, const crow::request& req){
	auto tracer = getTracer();
	auto span = tracer->StartSpan(req.url);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to list monitoring credentials from " << req.remote_endpoint);
	//only valid platform admins should be allowed to dump the credentials
	if(!user || !user.admin){
		log_info("Request to list monitoring credentials rejected");
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
		
	auto credentials=store.listMonitoringCredentials();
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(credentials.size(), alloc);
	for(const S3Credential& cred : credentials){
		rapidjson::Value credResult(rapidjson::kObjectType);
		credResult.AddMember("apiVersion", "v1alpha3", alloc);
		credResult.AddMember("kind", "MonitoringCredential", alloc);
		rapidjson::Value credData(rapidjson::kObjectType);
		credData.AddMember("accessKey", cred.accessKey, alloc);
		credData.AddMember("secretKey", cred.secretKey, alloc);
		credData.AddMember("inUse", cred.inUse, alloc);
		credData.AddMember("revoked", cred.revoked, alloc);
		credResult.AddMember("metadata", credData, alloc);
		resultItems.PushBack(credResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);
	span->End();
	return crow::response(to_string(result));
}

crow::response addMonitoringCredential(PersistentStore& store, const crow::request& req){
	auto tracer = getTracer();
	auto span = tracer->StartSpan(req.url);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to add a monitoring credential from " << req.remote_endpoint);
	//only valid platform admins should be allowed to insert new credentials
	if(!user || !user.admin){
		log_info("Request to add a monitoring credential rejected");
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	rapidjson::Document body;
	try{
		body.Parse(req.body.c_str());
	}catch(std::runtime_error& err){
		const std::string& errMsg = "Invalid JSON in request body";
		setWebSpanError(span, errMsg + " exception: " + err.what(), 400);
		span->End();
		log_warn(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	
	if(body.IsNull()) {
		const std::string& errMsg = "Invalid JSON in request body";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body.HasMember("metadata")) {
		const std::string& errMsg = "Missing metadata in request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"].IsObject()) {
		const std::string& errMsg = "Incorrect type for metadata member";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	
	if(!body["metadata"].HasMember("accessKey")) {
		const std::string& errMsg = "Missing access key in request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"]["accessKey"].IsString()) {
		const std::string& errMsg = "Incorrect type for access key";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
		
	if(!body["metadata"].HasMember("secretKey")) {
		const std::string& errMsg = "Missing secret key in request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body["metadata"]["secretKey"].IsString()) {
		const std::string& errMsg = "Incorrect type for secret key";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	
	S3Credential cred(body["metadata"]["accessKey"].GetString(),
	                  body["metadata"]["secretKey"].GetString());
	
	if(cred.accessKey.empty()) {
		const std::string& errMsg = "Empty access key in request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(cred.secretKey.empty()) {
		const std::string& errMsg = "Empty secret key in request";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	
	log_info("Creating credential " << cred);
	bool added=store.addMonitoringCredential(cred);
	if(!added) {
		const std::string& errMsg = "Storing monitoring credential failed";
		setWebSpanError(span, errMsg, 500);
		span->End();
		log_error(errMsg);
		return crow::response(500, generateError(errMsg));
	}
	span->End();
	return crow::response(200);
}

crow::response revokeMonitoringCredential(PersistentStore& store, const crow::request& req,
                                          const std::string& credentialID){
	auto tracer = getTracer();
	auto span = tracer->StartSpan(req.url);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to revoke monitoring credential "
	         << credentialID << " from " << req.remote_endpoint);
	//only valid platform admins should be allowed to insert new credentials
	if(!user || !user.admin){
		log_info("Request to revoke a monitoring credential rejected");
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	auto cred=store.getMonitoringCredential(credentialID);
	if(!cred){
		const std::string& errMsg = "Monitoring credential not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_warn(errMsg);
		return crow::response(404, generateError(errMsg));
	}
	
	log_info("Revoking credential " << cred);
	Cluster usingCluster=store.findClusterUsingCredential(cred);
	if(usingCluster){
		log_info("Removing credential from " << usingCluster << " record");
		bool removed=store.removeClusterMonitoringCredential(usingCluster.id);
		if(!removed) {
			const std::string& errMsg = "Revoking monitoring credential failed";
			setWebSpanError(span, errMsg, 500);
			span->End();
			log_error(errMsg);
			return crow::response(500, generateError(errMsg));
		}
	}
	
	bool revoked=store.revokeMonitoringCredential(credentialID);
	if(!revoked) {
		const std::string& errMsg = "Revoking monitoring credential failed";
		setWebSpanError(span, errMsg, 500);
		span->End();
		log_error(errMsg);
		return crow::response(500, generateError(errMsg));
	}
	span->End();
	return crow::response(200);
}

crow::response deleteMonitoringCredential(PersistentStore& store, const crow::request& req,
                                          const std::string& credentialID){
	auto tracer = getTracer();
	auto span = tracer->StartSpan(req.url);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to delete monitoring credential "
	         << credentialID << " from " << req.remote_endpoint);
	//only valid platform admins should be allowed to insert new credentials
	if(!user || !user.admin){
		log_info("Request to delete a monitoring credential rejected");
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	auto cred=store.getMonitoringCredential(credentialID);
	if(!cred){
		const std::string& errMsg = "Monitoring credential not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_warn(errMsg);
		return crow::response(404, generateError(errMsg));
	}
	
	log_info("Deleting credential " << cred);
	bool deleted=store.deleteMonitoringCredential(credentialID);
	if(!deleted) {
		const std::string& errMsg = "Deleting monitoring credential failed";
		setWebSpanError(span, errMsg, 500);
		span->End();
		log_error(errMsg);
		return crow::response(500, generateError(errMsg));
	}
	span->End();
	return crow::response(200);
}
