#include "test.h"

#include "PersistentStore.h"
#include "ServerUtilities.h"

//can't delete a non-existent cred
//can delete an unused cred
//can't delete an in-use cred
//can delete a revoked cred
TEST(InternalDeleteCredential){
	DatabaseContext db;
	auto store=db.makePersistentStore();
	
	S3Credential c1("foo","bar"),cred;
	bool result=store->deleteMonitoringCredential(c1.accessKey);
	ENSURE_EQUAL(result,false,"It should not be possible to delete a credential which has never been added");
	
	result=store->addMonitoringCredential(c1);
	ENSURE_EQUAL(result,true,"Adding a valid credential should succeed");
	
	result=store->deleteMonitoringCredential(c1.accessKey);
	ENSURE_EQUAL(result,true,"It should be possible to delete a credential which is unused");
	
	auto credentials=store->listMonitoringCredentials();
	ENSURE_EQUAL(credentials.size(),0,"No credentials should remain");
	
	cred=store->getMonitoringCredential(c1.accessKey);
	ENSURE_EQUAL((bool)cred,false,"It should be impossible to retrieve a deleted credential");
	
	result=store->addMonitoringCredential(c1);
	ENSURE_EQUAL(result,true,"Adding a valid credential should succeed");
	cred=std::get<0>(store->allocateMonitoringCredential());
	ENSURE(cred);
	
	result=store->deleteMonitoringCredential(c1.accessKey);
	ENSURE_EQUAL(result,false,"It should be impossible to delete a credential which is in use");
	
	cred=store->getMonitoringCredential(c1.accessKey);
	ENSURE_EQUAL(cred,c1,"It should be still be possible to retrieve a credential when deletion has failed");
	ENSURE_EQUAL(cred.inUse,true,"The credential should remain in use");
	
	result=store->revokeMonitoringCredential(c1.accessKey);
	ENSURE_EQUAL(result,true,"Credential revocation should succeed");
	
	result=store->deleteMonitoringCredential(c1.accessKey);
	ENSURE_EQUAL(result,true,"Deleting a revoked credential should succeed");
	
	cred=store->getMonitoringCredential(c1.accessKey);
	ENSURE_EQUAL((bool)cred,false,"It should be impossible to retrieve a deleted credential");
	
	credentials=store->listMonitoringCredentials();
	ENSURE_EQUAL(credentials.size(),0,"No credentials should remain");
}

TEST(DeleteNonexistentClusterCredential){
	using namespace httpRequests;
	TestContext tc;
	std::string adminKey=tc.getPortalToken();
	
	auto deleteResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/monitoring_credentials/NON-EXISTENT_KEY?token="+adminKey);
	ENSURE_EQUAL(deleteResp.status,404,"Deleting a non-existent credential should fail");
}

TEST(DeleteClusterCredential){
	using namespace httpRequests;
	TestContext tc;
	std::string adminKey=tc.getPortalToken();
	
	std::string groupID;
	{ // create Group to register cluster with
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testgroup1", alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
					 to_string(createGroup));
		ENSURE_EQUAL(groupResp.status,200,"Group creation request should succeed");
		rapidjson::Document groupData;
		groupData.Parse(groupResp.body.c_str());
		groupID=groupData["metadata"]["id"].GetString();
	}

	S3Credential c1("foo","bar");
	{		
		rapidjson::Document createCred(rapidjson::kObjectType);
		auto& alloc = createCred.GetAllocator();
		createCred.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("accessKey", c1.accessKey, alloc);
		metadata.AddMember("secretKey", c1.secretKey, alloc);
		createCred.AddMember("metadata", metadata, alloc);
		auto addResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/monitoring_credentials?token="+adminKey,
			                  to_string(createCred));
		ENSURE_EQUAL(addResp.status,200,"Credential addition request should succeed: "+addResp.body);
	}
	
	{
		auto deleteResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/monitoring_credentials/"+c1.accessKey+"?token="+adminKey);
		ENSURE_EQUAL(deleteResp.status,200,"Deleting a not-in-use credential should succeed");
	}
	
	{
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/monitoring_credentials?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Credential listing should succeed");
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		ENSURE(listData["items"].GetArray().Empty(),"No credentials should remain after deletion");
	}
}

//non-admins should not be able to delete credentials
TEST(DeleteClusterCredentialUnauthorized){
	using namespace httpRequests;
	TestContext tc;
	std::string adminKey=tc.getPortalToken();
	
	std::string userID, userToken;
	{ //create second user that isn't an admin
		rapidjson::Document createUser(rapidjson::kObjectType);
		auto& alloc = createUser.GetAllocator();
		createUser.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("phone", "555-5555", alloc);
		metadata.AddMember("institution", "Center of the Earth University", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		createUser.AddMember("metadata", metadata, alloc);
		auto userResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,
					   to_string(createUser));
		ENSURE_EQUAL(userResp.status,200,
				 "User creation request should succeed");
		ENSURE(!userResp.body.empty());
		rapidjson::Document userData;
		userData.Parse(userResp.body.c_str());
		userID=userData["metadata"]["id"].GetString();
		userToken=userData["metadata"]["access_token"].GetString();
	}
	
	S3Credential c1("foo","bar");
	{		
		rapidjson::Document createCred(rapidjson::kObjectType);
		auto& alloc = createCred.GetAllocator();
		createCred.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("accessKey", c1.accessKey, alloc);
		metadata.AddMember("secretKey", c1.secretKey, alloc);
		createCred.AddMember("metadata", metadata, alloc);
		auto addResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/monitoring_credentials?token="+adminKey,
							  to_string(createCred));
		ENSURE_EQUAL(addResp.status,200,"Credential addition request should succeed: "+addResp.body);
	}
	
	{ //the non-admin should still not be able to delete it
		auto deleteResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/monitoring_credentials/"+c1.accessKey+"?token="+userToken);
		ENSURE_EQUAL(deleteResp.status,403,"Non-admin users should not be allowed to delete credentials");
	}
	
	{
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/monitoring_credentials?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Credential listing should succeed");
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		ENSURE_EQUAL(listData["items"].GetArray().Size(),1,"Credential should remain after deletion attempt");
	}
}
