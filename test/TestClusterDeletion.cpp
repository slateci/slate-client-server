#include "test.h"

#include <ServerUtilities.h>
#include <PersistentStore.h>

TEST(UnauthenticatedDeleteCluster){
	using namespace httpRequests;
	TestContext tc;
	
	//try deleting a cluster with no authentication
	auto deleteResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/Cluster_1234567890");
	ENSURE_EQUAL(deleteResp.status,403,
		     "Requests to delete clusters without authentication should be rejected");

	//try deleting a cluster with invalid authentication
	deleteResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/Cluster_1234567890?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(deleteResp.status,403,
		     "Requests to delete clusters with invalid authentication should be rejected");
}

TEST(DeleteCluster){
	using namespace httpRequests;
	TestContext tc;
	std::string adminKey=getPortalToken();
	auto createClusterUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey;
	
	// create Group to register cluster with
	rapidjson::Document createGroup(rapidjson::kObjectType);
	{
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testgroup1", alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
	}
	auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
			     to_string(createGroup));
	ENSURE_EQUAL(groupResp.status,200,"Group creation request should succeed");
	rapidjson::Document groupData;
	groupData.Parse(groupResp.body.c_str());
	auto groupID=groupData["metadata"]["id"].GetString();	

	auto kubeConfig = tc.getKubeConfig();

	// create the cluster
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", rapidjson::StringRef(groupID), alloc);
		metadata.AddMember("organization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp=httpPost(createClusterUrl, to_string(request1));
	ENSURE_EQUAL(createResp.status,200,
		     "Cluster creation request should succeed");
	rapidjson::Document createData;
	createData.Parse(createResp.body);
	auto clusterID=createData["metadata"]["id"].GetString();

	// check that cluster is returned with list request
	auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey);
	ENSURE_EQUAL(listResp.status,200,"Cluster list request should succeed");
	rapidjson::Document listData;
	listData.Parse(listResp.body);
	ENSURE_EQUAL(listData["items"].Size(),1,"One cluster should be returned");
	
	// delete the cluster
	auto deleteResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
				   "?token="+adminKey);
	ENSURE_EQUAL(deleteResp.status,200,"Cluster deletion should succeed");

	//list clusters to check that cluster is gone
	listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey);
	ENSURE_EQUAL(listResp.status,200,"Cluster list request should succeed");
	listData.Parse(listResp.body);
	ENSURE_EQUAL(listData["items"].Size(),0,"No clusters should remain");
	
}

TEST(DeleteNonexistentCluster){
	using namespace httpRequests;
	TestContext tc;	
	std::string adminKey=getPortalToken();

	//try to delete cluster with invalid ID
	auto deleteResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/Cluster_1234567890?token="+adminKey);
	ENSURE_EQUAL(deleteResp.status,404,"Deletion of a non-existant cluster should be rejected");
}

TEST(DeletingClusterRemovesAccessGrants){
	//The public API should already prevent any operation involving a deleted 
	//cluster, which is good, but prevents checking whether ancilliary records
	//have really been removed. 
	auto dbResp=httpRequests::httpGet("http://localhost:52000/dynamo/create");
	ENSURE_EQUAL(dbResp.status,200);
	std::string dbPort=dbResp.body;
	
	const std::string awsAccessKey="foo";
	const std::string awsSecretKey="bar";
	Aws::SDKOptions options;
	Aws::InitAPI(options);
	using AWSOptionsHandle=std::unique_ptr<Aws::SDKOptions,void(*)(Aws::SDKOptions*)>;
	AWSOptionsHandle opt_holder(&options,
								[](Aws::SDKOptions* options){
									Aws::ShutdownAPI(*options); 
								});
	Aws::Auth::AWSCredentials credentials(awsAccessKey,awsSecretKey);
	Aws::Client::ClientConfiguration clientConfig;
	clientConfig.scheme=Aws::Http::Scheme::HTTP;
	clientConfig.endpointOverride="localhost:"+dbPort;
	
	PersistentStore store(credentials,clientConfig,
	                      "slate_portal_user","encryptionKey",
	                      "",9200);
	
	Group group1;
	group1.id=idGenerator.generateGroupID();
	group1.name="group1";
	group1.email="abc@def";
	group1.phone="22";
	group1.scienceField="stuff";
	group1.description=" ";
	group1.valid=true;
	
	bool success=store.addGroup(group1);
	ENSURE(success,"Group addition should succeed");
	
	Group group2;
	group2.id=idGenerator.generateGroupID();
	group2.name="group2";
	group2.email="ghi@jkl";
	group2.phone="29";
	group2.scienceField="stuff";
	group2.description=" ";
	group2.valid=true;
	
	success=store.addGroup(group2);
	ENSURE(success,"Group addition should succeed");

	Cluster cluster;
	cluster.id=idGenerator.generateClusterID();
	cluster.name="cluster";
	cluster.config="-"; //Dynamo will get upset if this is empty, but it will not be used
	cluster.systemNamespace="-"; //Dynamo will get upset if this is empty, but it will not be used
	cluster.owningGroup=group1.id;
	cluster.owningOrganization="Something";
	cluster.valid=true;
	
	success=store.addCluster(cluster);
	ENSURE(success,"Cluster creation should succeed");
	
	success=store.addGroupToCluster(group2.id,cluster.id);
	ENSURE(success,"Granting non-owning Group access to cluster should succeed");
	
	ENSURE(store.groupAllowedOnCluster(group2.id,cluster.id), 
	       "Non-owning Group should have access to cluster");
	
	success=store.removeCluster(cluster.id);
	ENSURE(success,"Cluster deletion should succeed");
	
	//Ensure that the access record is really gone
	ENSURE(!store.groupAllowedOnCluster(group2.id,cluster.id), 
	       "Non-owning Group should not have access to deleted cluster");
	
	//repeat the exercise with a wildcard grant
	success=store.addCluster(cluster);
	ENSURE(success,"Cluster creation should succeed");
	
	success=store.addGroupToCluster(PersistentStore::wildcard,cluster.id);
	ENSURE(success,"Granting universal Group access to cluster should succeed");
	
	ENSURE(store.groupAllowedOnCluster(group2.id,cluster.id), 
	       "Non-owning Group should have access to cluster");
	
	success=store.removeCluster(cluster.id);
	ENSURE(success,"Cluster deletion should succeed");
	
	//Ensure that the access record is really gone
	ENSURE(!store.groupAllowedOnCluster(group2.id,cluster.id), 
	       "Non-owning Group should not have access to deleted cluster");
}
