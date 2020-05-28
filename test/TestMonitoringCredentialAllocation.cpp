#include "test.h"

#include "PersistentStore.h"
#include "ServerUtilities.h"

TEST(InternalListWithNoCredentials){
	DatabaseContext db;
	auto store=db.makePersistentStore();
	
	auto credentials=store->listMonitoringCredentials();
	ENSURE(credentials.empty()); //there should be no credentials if none have been added
}

TEST(InternalAddCredential){
	DatabaseContext db;
	auto store=db.makePersistentStore();
	
	auto credentials=store->listMonitoringCredentials();
	ENSURE(credentials.empty()); //there should be no credentials if none have been added
	
	S3Credential c1("foo","bar");
	bool result=store->addMonitoringCredential(c1);
	ENSURE_EQUAL(result,true,"Adding a valid credential should succeed");
	
	credentials=store->listMonitoringCredentials();
	ENSURE_EQUAL(credentials.size(),1,"One credential should be returned");
	ENSURE_EQUAL(credentials[0].accessKey,c1.accessKey,"AccessKeys should match");
	ENSURE_EQUAL(credentials[0].secretKey,c1.secretKey,"SecretKeys should match");
	ENSURE_EQUAL(credentials[0].inUse,c1.inUse,"InUse flags should match");
	ENSURE_EQUAL(credentials[0].revoked,c1.revoked,"Revoked flags should match");
	
	S3Credential c2("baz","quux");
	result=store->addMonitoringCredential(c2);
	ENSURE_EQUAL(result,true,"Adding a sceond valid credential should succeed");
	
	credentials=store->listMonitoringCredentials();
	ENSURE_EQUAL(credentials.size(),2,"Two credential should be returned");
	{
		std::size_t c1Idx, c2Idx;
		if(credentials.front()==c1){
			c1Idx=0;
			c2Idx=1;
		}
		else{
			c1Idx=1;
			c2Idx=0;
		}
		ENSURE_EQUAL(credentials[c1Idx].accessKey,c1.accessKey,"AccessKeys should match");
		ENSURE_EQUAL(credentials[c1Idx].secretKey,c1.secretKey,"SecretKeys should match");
		ENSURE_EQUAL(credentials[c1Idx].inUse,c1.inUse,"InUse flags should match");
		ENSURE_EQUAL(credentials[c1Idx].revoked,c1.revoked,"Revoked flags should match");
		
		ENSURE_EQUAL(credentials[c2Idx].accessKey,c2.accessKey,"AccessKeys should match");
		ENSURE_EQUAL(credentials[c2Idx].secretKey,c2.secretKey,"SecretKeys should match");
		ENSURE_EQUAL(credentials[c2Idx].inUse,c2.inUse,"InUse flags should match");
		ENSURE_EQUAL(credentials[c2Idx].revoked,c2.revoked,"Revoked flags should match");
	}
}

TEST(InternalGetCredential){
	DatabaseContext db;
	auto store=db.makePersistentStore();
	
	auto cred=store->getMonitoringCredential("foo");
	ENSURE(!cred); //it should be impossible to fetch a credential which does not exist
	
	S3Credential c1("foo","bar");
	bool result=store->addMonitoringCredential(c1);
	ENSURE_EQUAL(result,true,"Adding a valid credential should succeed");
	
	cred=store->getMonitoringCredential("foo");
	ENSURE(cred); //retrieval should succeed

	ENSURE_EQUAL(cred.accessKey,c1.accessKey,"AccessKeys should match");
	ENSURE_EQUAL(cred.secretKey,c1.secretKey,"SecretKeys should match");
	ENSURE_EQUAL(cred.inUse,c1.inUse,"InUse flags should match");
	ENSURE_EQUAL(cred.revoked,c1.revoked,"Revoked flags should match");	
}

TEST(InternaAllocateNoCredential){
	DatabaseContext db;
	auto store=db.makePersistentStore();
	
	auto cred=std::get<0>(store->allocateMonitoringCredential());
	ENSURE(!cred);
}

TEST(InternaAllocateCredential){
	DatabaseContext db;
	auto store=db.makePersistentStore();
	
	S3Credential c1("foo","bar");
	bool result=store->addMonitoringCredential(c1);
	ENSURE_EQUAL(result,true,"Adding a valid credential should succeed");
	
	auto cred=std::get<0>(store->allocateMonitoringCredential());
	ENSURE(cred);
	ENSURE_EQUAL(cred,c1,"The only available credential should be the one allocated");
	ENSURE_EQUAL(cred.inUse,true,"The allocated credential should be marked as in-use");
	ENSURE_EQUAL(cred.revoked,false,"The allocated credential should not be marked as revoked");
	
	//fetching the credential again should equivalently show it as allocated
	cred=store->getMonitoringCredential("foo");
	ENSURE(cred);
	ENSURE_EQUAL(cred,c1,"The only available credential should be the one allocated");
	ENSURE_EQUAL(cred.inUse,true,"The allocated credential should be marked as in-use");
	ENSURE_EQUAL(cred.revoked,false,"The allocated credential should not be marked as revoked");
}

TEST(InternalAllocateMultipleCredentials){
	DatabaseContext db;
	auto store=db.makePersistentStore();
	
	const unsigned int nCreds=10;
	
	//create a number of credentials
	for(unsigned int i=0; i<nCreds; i++){
		//uniqueness of secret keys isn't enforced
		bool result=store->addMonitoringCredential(S3Credential(std::to_string(i),"blah"));
		ENSURE(result);
	}
	
	//try to allocate the same number of credentials
	for(unsigned int i=0; i<nCreds; i++){
		auto cred=std::get<0>(store->allocateMonitoringCredential());
		ENSURE(cred);
		ENSURE_EQUAL(cred.inUse,true,"The allocated credential should be marked as in-use");
		ENSURE_EQUAL(cred.revoked,false,"The allocated credential should not be marked as revoked");
		//fetch the same credential again to ensure matching results
		auto cred2=store->getMonitoringCredential(cred.accessKey);
		ENSURE(cred2);
		ENSURE_EQUAL(cred2,cred,"The only available credential should be the one allocated");
		ENSURE_EQUAL(cred2.inUse,true,"The allocated credential should be marked as in-use");
		ENSURE_EQUAL(cred2.revoked,false,"The allocated credential should not be marked as revoked");
	}
	
	{ //try to get one credential too many
		auto cred=std::get<0>(store->allocateMonitoringCredential());
		ENSURE(!cred);
	}
}

TEST(InternalRevokeUnusedCredential){
	DatabaseContext db;
	auto store=db.makePersistentStore();

	S3Credential c1("foo","bar");
	bool result=store->revokeMonitoringCredential(c1.accessKey);
	ENSURE_EQUAL(result,false,"It should be impossible to revoke a credential which does not exist");
	
	result=store->addMonitoringCredential(c1);
	ENSURE_EQUAL(result,true,"Adding a valid credential should succeed");
	
	result=store->revokeMonitoringCredential(c1.accessKey);
	ENSURE_EQUAL(result,true,"It should be possible to revoke a credential which is not in use");
	
	//fetching the credential should show it as revoked
	auto cred=store->getMonitoringCredential(c1.accessKey);
	ENSURE(cred);
	ENSURE_EQUAL(cred,c1,"The identity of the credential should match");
	ENSURE_EQUAL(cred.inUse,false,"The allocated credential should be marked as not in-use");
	ENSURE_EQUAL(cred.revoked,true,"The allocated credential should be marked as revoked");	
}

TEST(InternalRevokeInUseCredential){
	DatabaseContext db;
	auto store=db.makePersistentStore();

	S3Credential c1("foo","bar");
	bool result=store->revokeMonitoringCredential(c1.accessKey);
	ENSURE_EQUAL(result,false,"It should be impossible to revoke a credential which does not exist");
	
	result=store->addMonitoringCredential(c1);
	ENSURE_EQUAL(result,true,"Adding a valid credential should succeed");
	
	auto cred=std::get<0>(store->allocateMonitoringCredential());
	ENSURE(cred);
	
	result=store->revokeMonitoringCredential(c1.accessKey);
	ENSURE_EQUAL(result,true,"It should be possible to revoke a credential an in-use credential");
	
	//fetching the credential again should show it as revoked
	cred=store->getMonitoringCredential(c1.accessKey);
	ENSURE(cred);
	ENSURE_EQUAL(cred,c1,"The identity of the credential should match");
	ENSURE_EQUAL(cred.inUse,false,"The allocated credential should be marked as not in-use");
	ENSURE_EQUAL(cred.revoked,true,"The allocated credential should be marked as revoked");	
}

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

TEST(AllocateClusterCredential){
	using namespace httpRequests;
	TestContext tc;
	std::string adminKey=tc.getPortalToken();
	auto createClusterUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey;
	
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

	auto kubeConfig = tc.getKubeConfig();

	// create the cluster
	std::string clusterID;
	{
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", rapidjson::StringRef(groupID), alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request1));
		ENSURE_EQUAL(createResp.status,200,"Cluster creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		clusterID=createData["metadata"]["id"].GetString();
	}
	
	{
		auto clusterResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+"?token="+adminKey);
		ENSURE_EQUAL(clusterResp.status,200,"Cluster info request should succeed");
		rapidjson::Document clusterData;
		clusterData.Parse(clusterResp.body);
		ENSURE_EQUAL(clusterData["metadata"]["hasMonitoring"].GetBool(),false,
					 "Newly created cluster should not have a monitoring credential assigned");
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
	
	//On to the main event!
	{
		auto credResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"
							  +clusterID+"/monitoring_credential?token="+adminKey);
		ENSURE_EQUAL(credResp.status,200,"Monitoring credential fetch request should succeed");
		rapidjson::Document credData;
		credData.Parse(credResp.body);
	
		auto schema=loadSchema(getSchemaDir()+"/MonitoringCredentialResultSchema.json");
		ENSURE_CONFORMS(credData,schema);
		ENSURE_EQUAL(credData["metadata"]["accessKey"].GetString(),c1.accessKey,
					 "Credential access key should match");
		ENSURE_EQUAL(credData["metadata"]["secretKey"].GetString(),c1.secretKey,
					 "Credential secret key should match");
		ENSURE_EQUAL(credData["metadata"]["inUse"].GetBool(),true,
					 "Credential should be marked as in use");
		ENSURE_EQUAL(credData["metadata"]["revoked"].GetBool(),false,
					 "Credential should not be marked as revoked");
	}
	
	{
		auto clusterResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+"?token="+adminKey);
		ENSURE_EQUAL(clusterResp.status,200,"Cluster info request should succeed");
		rapidjson::Document clusterData;
		clusterData.Parse(clusterResp.body);
		ENSURE_EQUAL(clusterData["metadata"]["hasMonitoring"].GetBool(),true,
					 "Cluster should have a monitoring credential assigned");
	}
}

TEST(AllocateClusterCredentialUnavailable){
	using namespace httpRequests;
	TestContext tc;
	std::string adminKey=tc.getPortalToken();
	auto createClusterUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey;
	
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

	auto kubeConfig = tc.getKubeConfig();

	// create the cluster
	std::string clusterID;
	{
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", rapidjson::StringRef(groupID), alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request1));
		ENSURE_EQUAL(createResp.status,200,"Cluster creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		clusterID=createData["metadata"]["id"].GetString();
	}
	
	{
		auto clusterResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+"?token="+adminKey);
		ENSURE_EQUAL(clusterResp.status,200,"Cluster info request should succeed");
		rapidjson::Document clusterData;
		clusterData.Parse(clusterResp.body);
		ENSURE_EQUAL(clusterData["metadata"]["hasMonitoring"].GetBool(),false,
					 "Newly created cluster should not have a monitoring credential assigned");
	}
	
	//On to the main event!
	{
		auto credResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"
							  +clusterID+"/monitoring_credential?token="+adminKey);
		ENSURE_EQUAL(credResp.status,500,"Monitoring credential fetch request should fail when no credentials are available");
	}
}

TEST(NoDoubleAllocation){
	using namespace httpRequests;
	TestContext tc;
	std::string adminKey=tc.getPortalToken();
	auto createClusterUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey;
	
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

	auto kubeConfig = tc.getKubeConfig();

	// create the two clusters
	std::string clusterID1;
	{
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", rapidjson::StringRef(groupID), alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request1));
		ENSURE_EQUAL(createResp.status,200,"Cluster creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		clusterID1=createData["metadata"]["id"].GetString();
	}
	std::string clusterID2;
	{
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster2", alloc);
		metadata.AddMember("group", rapidjson::StringRef(groupID), alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request1));
		ENSURE_EQUAL(createResp.status,200,"Cluster creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		clusterID2=createData["metadata"]["id"].GetString();
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
	
	//On to the main event!
	{
		auto credResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"
							  +clusterID1+"/monitoring_credential?token="+adminKey);
		ENSURE_EQUAL(credResp.status,200,"Monitoring credential fetch request should succeed");
		rapidjson::Document credData;
		credData.Parse(credResp.body);
	
		auto schema=loadSchema(getSchemaDir()+"/MonitoringCredentialResultSchema.json");
		ENSURE_CONFORMS(credData,schema);
		ENSURE_EQUAL(credData["metadata"]["accessKey"].GetString(),c1.accessKey,
					 "Credential access key should match");
		ENSURE_EQUAL(credData["metadata"]["secretKey"].GetString(),c1.secretKey,
					 "Credential secret key should match");
		ENSURE_EQUAL(credData["metadata"]["inUse"].GetBool(),true,
					 "Credential should be marked as in use");
		ENSURE_EQUAL(credData["metadata"]["revoked"].GetBool(),false,
					 "Credential should not be marked as revoked");
	}
	
	{
		auto clusterResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID1+"?token="+adminKey);
		ENSURE_EQUAL(clusterResp.status,200,"Cluster info request should succeed");
		rapidjson::Document clusterData;
		clusterData.Parse(clusterResp.body);
		ENSURE_EQUAL(clusterData["metadata"]["hasMonitoring"].GetBool(),true,
					 "Cluster should have a monitoring credential assigned");
	}
	
	//since the only credential has been granted to cluster 1, cluster 2 should not be able to get one
	{
		auto credResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"
							  +clusterID2+"/monitoring_credential?token="+adminKey);
		ENSURE_EQUAL(credResp.status,500,"Monitoring credential fetch request should fail when no credentials are available");
	}
} 

//non-admins should not be able to get/assign credentials
TEST(AllocateClusterCredentialUnauthorized){
	using namespace httpRequests;
	TestContext tc;
	std::string adminKey=tc.getPortalToken();
	auto createClusterUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey;
	
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
	
	std::string userID, userToken;
	{ //create second user that isn't part of the group
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
	
	auto kubeConfig = tc.getKubeConfig();

	// create the cluster
	std::string clusterID;
	{
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", rapidjson::StringRef(groupID), alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request1));
		ENSURE_EQUAL(createResp.status,200,"Cluster creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		clusterID=createData["metadata"]["id"].GetString();
	}
	
	{
		auto clusterResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+"?token="+adminKey);
		ENSURE_EQUAL(clusterResp.status,200,"Cluster info request should succeed");
		rapidjson::Document clusterData;
		clusterData.Parse(clusterResp.body);
		ENSURE_EQUAL(clusterData["metadata"]["hasMonitoring"].GetBool(),false,
					 "Newly created cluster should not have a monitoring credential assigned");
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
	
	//On to the main event!
	{
		auto credResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"
							  +clusterID+"/monitoring_credential?token="+userToken);
		ENSURE_EQUAL(credResp.status,403,"Monitoring credential fetch requests by unrelated users should be denied");
	}
}

//revoked credentials should never be allocated
TEST(AllocateClusterCredentialRevoked){
	using namespace httpRequests;
	TestContext tc;
	std::string adminKey=tc.getPortalToken();
	auto createClusterUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey;
	
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
	
	auto kubeConfig = tc.getKubeConfig();

	// create the cluster
	std::string clusterID;
	{
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", rapidjson::StringRef(groupID), alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request1));
		ENSURE_EQUAL(createResp.status,200,"Cluster creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		clusterID=createData["metadata"]["id"].GetString();
	}
	
	{
		auto clusterResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+"?token="+adminKey);
		ENSURE_EQUAL(clusterResp.status,200,"Cluster info request should succeed");
		rapidjson::Document clusterData;
		clusterData.Parse(clusterResp.body);
		ENSURE_EQUAL(clusterData["metadata"]["hasMonitoring"].GetBool(),false,
					 "Newly created cluster should not have a monitoring credential assigned");
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
	
	{ //revoke the credential
		auto revResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/monitoring_credentials/"+c1.accessKey+"/revoke?token="+adminKey,"");
		ENSURE_EQUAL(revResp.status,200,"Credential revocation request should succeed: "+revResp.body);
	}
	
	//On to the main event!
	{
		auto credResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"
							  +clusterID+"/monitoring_credential?token="+adminKey);
		ENSURE_EQUAL(credResp.status,500,"Revoked credentials should not be allocated");
	}
}
