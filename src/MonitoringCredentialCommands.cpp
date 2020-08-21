#include "MonitoringCredentialCommands.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "Logging.h"
#include "ServerUtilities.h"

crow::response listMonitoringCredentials(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list monitoring credentials from " << req.remote_endpoint);
	//only valid platform admins should be allowed to dump the credentials
	if(!user || !user.admin){
		log_info("Request to list monitoring credentials rejected");
		return crow::response(403,generateError("Not authorized"));
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
	
	return crow::response(to_string(result));
}

crow::response addMonitoringCredential(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to add a monitoring credential from " << req.remote_endpoint);
	//only valid platform admins should be allowed to insert new credentials
	if(!user || !user.admin){
		log_info("Request to add a monitoring credential rejected");
		return crow::response(403,generateError("Not authorized"));
	}
	
	rapidjson::Document body;
	try{
		body.Parse(req.body.c_str());
	}catch(std::runtime_error& err){
		log_warn("Invalid JSON in request body");
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	
	if(body.IsNull())
		return crow::response(400,generateError("Invalid JSON in request body"));
	if(!body.HasMember("metadata"))
		return crow::response(400,generateError("Missing metadata in request"));
	if(!body["metadata"].IsObject())
		return crow::response(400,generateError("Incorrect type for metadata member"));
	
	if(!body["metadata"].HasMember("accessKey"))
		return crow::response(400,generateError("Missing access key in request"));
	if(!body["metadata"]["accessKey"].IsString())
		return crow::response(400,generateError("Incorrect type for access key"));
		
	if(!body["metadata"].HasMember("secretKey"))
		return crow::response(400,generateError("Missing secret key in request"));
	if(!body["metadata"]["secretKey"].IsString())
		return crow::response(400,generateError("Incorrect type for secret key"));
	
	S3Credential cred(body["metadata"]["accessKey"].GetString(),
	                  body["metadata"]["secretKey"].GetString());
	
	if(cred.accessKey.empty())
		return crow::response(400,generateError("Empty access key in request"));
	if(cred.secretKey.empty())
		return crow::response(400,generateError("Empty secret key in request"));
	
	log_info("Creating credential " << cred);
	bool added=store.addMonitoringCredential(cred);
	if(!added)
		return crow::response(500,generateError("Storing monitoring credential failed"));
		
	return crow::response(200);
}

crow::response revokeMonitoringCredential(PersistentStore& store, const crow::request& req,
                                          const std::string& credentialID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to revoke monitoring credential "
	         << credentialID << " from " << req.remote_endpoint);
	//only valid platform admins should be allowed to insert new credentials
	if(!user || !user.admin){
		log_info("Request to revoke a monitoring credential rejected");
		return crow::response(403,generateError("Not authorized"));
	}
	
	auto cred=store.getMonitoringCredential(credentialID);
	if(!cred){
		log_warn("Target credential not found");
		return crow::response(404,generateError("Monitoring credential not found"));
	}
	
	log_info("Revoking credential " << cred);
	Cluster usingCluster=store.findClusterUsingCredential(cred);
	if(usingCluster){
		log_info("Removing credential from " << usingCluster << " record");
		bool removed=store.removeClusterMonitoringCredential(usingCluster.id);
		if(!removed)
			return crow::response(500,generateError("Revoking monitoring credential failed"));
	}
	
	bool revoked=store.revokeMonitoringCredential(credentialID);
	if(!revoked)
		return crow::response(500,generateError("Revoking monitoring credential failed"));
		
	return crow::response(200);
}

crow::response deleteMonitoringCredential(PersistentStore& store, const crow::request& req,
                                          const std::string& credentialID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to delete monitoring credential "
	         << credentialID << " from " << req.remote_endpoint);
	//only valid platform admins should be allowed to insert new credentials
	if(!user || !user.admin){
		log_info("Request to delete a monitoring credential rejected");
		return crow::response(403,generateError("Not authorized"));
	}
	
	auto cred=store.getMonitoringCredential(credentialID);
	if(!cred){
		log_warn("Target credential not found");
		return crow::response(404,generateError("Monitoring credential not found"));
	}
	
	log_info("Deleting credential " << cred);
	bool deleted=store.deleteMonitoringCredential(credentialID);
	if(!deleted)
		return crow::response(500,generateError("Deleting monitoring credential failed"));
		
	return crow::response(200);
}
