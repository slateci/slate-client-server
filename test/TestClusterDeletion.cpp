#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedDeleteCluster){
	using namespace httpRequests;
	TestContext tc;
	
	//try deleting a cluster with no authentication
	auto deleteResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/clusters/Cluster_1234567890");
	ENSURE_EQUAL(deleteResp.status,403,
		     "Requests to delete clusters without authentication should be rejected");

	//try deleting a cluster with invalid authentication
	deleteResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/clusters/Cluster_1234567890?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(deleteResp.status,403,
		     "Requests to delete clusters with invalid authentication should be rejected");
}

TEST(DeleteCluster){
	using namespace httpRequests;
	TestContext tc;
	std::string adminKey=getPortalToken();
	auto createClusterUrl=tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey;
	
	// create VO to register cluster with
	rapidjson::Document createVO(rapidjson::kObjectType);
	{
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testvo1", alloc);
		createVO.AddMember("metadata", metadata, alloc);
	}
	auto voResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,
			     to_string(createVO));
	ENSURE_EQUAL(voResp.status,200,"VO creation request should succeed");
	rapidjson::Document voData;
	voData.Parse(voResp.body.c_str());
	auto voID=voData["metadata"]["id"].GetString();	

	auto kubeConfig = getKubeConfig();

	// create the cluster
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("vo", rapidjson::StringRef(voID), alloc);
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
	auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey);
	ENSURE_EQUAL(listResp.status,200,"Cluster list request should succeed");
	rapidjson::Document listData;
	listData.Parse(listResp.body);
	ENSURE_EQUAL(listData["items"].Size(),1,"One cluster should be returned");
	
	// delete the cluster
	auto deleteResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/clusters/"+clusterID+
				   "?token="+adminKey);
	ENSURE_EQUAL(deleteResp.status,200,"Cluster deletion should succeed");

	//list clusters to check that cluster is gone
	listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey);
	ENSURE_EQUAL(listResp.status,200,"Cluster list request should succeed");
	listData.Parse(listResp.body);
	ENSURE_EQUAL(listData["items"].Size(),0,"No clusters should remain");
	
}

TEST(DeleteNonexistentCluster){
	using namespace httpRequests;
	TestContext tc;	
	std::string adminKey=getPortalToken();

	//try to delete cluster with invalid ID
	auto deleteResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/clusters/Cluster_1234567890?token="+adminKey);
	ENSURE_EQUAL(deleteResp.status,404,"Deletion of a non-existant cluster should be rejected");
}
