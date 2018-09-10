#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedListVOClusters){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing VO members with no authentication
	//doesn't matter whether request body is correct since this should be rejected on other grounds
	auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/vos/VO_123/clusters");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list clusters owned by VOs without authentication should be rejected");
	
	//try listing VO members with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/vos/VO_123/clusters?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list clusters owned by VOs with invalid authentication should be rejected");
}

TEST(ListNonexistentVOClusters){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	
	//try listing VO members with invalid authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/vos/VO_123/clusters?token="+adminKey);
	ENSURE_EQUAL(listResp.status,404,
				 "Requests to list clusters owned by nonexistent VOs should be rejected");
}

TEST(ListVOClusters){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName="some-org";
	std::string clusterName="testcluster";
	auto schema=loadSchema(getSchemaDir()+"/ClusterListResultSchema.json");
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	{ //list VO clusters
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/vos/"+voName+"/clusters?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Listing VO owned clusters should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),0,"VO should own no clusters");
	}
	
	std::string clusterID;
	{ //add cluster
		auto kubeConfig = getKubeConfig();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("vo", rapidjson::StringRef(voName), alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200,
		             "Cluster creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body.c_str());
		clusterID=createData["metadata"]["id"].GetString();
	}
	
	{ //list VO clusters
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/vos/"+voName+"/clusters?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Listing VO owned clusters should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),1,"VO should own one cluster");
		ENSURE_EQUAL(data["items"][0]["metadata"]["id"].GetString(),clusterID,
		             "Correct cluster ID should be listed");
		ENSURE_EQUAL(data["items"][0]["metadata"]["name"].GetString(),clusterName,
		             "Correct cluster name should be listed");
		ENSURE_EQUAL(data["items"][0]["metadata"]["owningVO"].GetString(),voName,
		             "Correct owning VO name should be listed");
	}
}