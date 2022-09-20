#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedListGroupClusters){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing Group members with no authentication
	//doesn't matter whether request body is correct since this should be rejected on other grounds
	auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/Group_123/clusters");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list clusters owned by groups without authentication should be rejected");
	
	//try listing Group members with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/Group_123/clusters?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list clusters owned by groups with invalid authentication should be rejected");
}

TEST(ListNonexistentGroupClusters){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	
	//try listing Group members with invalid authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/Group_123/clusters?token="+adminKey);
	ENSURE_EQUAL(listResp.status,404,
				 "Requests to list clusters owned by nonexistent groups should be rejected");
}

TEST(ListGroupClusters){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string groupName="some-org";
	std::string clusterName="testcluster";
	auto schema=loadSchema(getSchemaDir()+"/ClusterListResultSchema.json");
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Group creation request should succeed");
	}
	
	{ //list Group clusters
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/"+groupName+"/clusters?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Listing Group owned clusters should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),0,"Group should own no clusters");
	}
	
	std::string clusterID;
	{ //add cluster
		auto kubeConfig = tc.getKubeConfig();
		auto caData = tc.getServerCAData();
		auto token = tc.getUserToken();
		auto kubeNamespace = tc.getKubeNamespace();
		auto serverAddress = tc.getServerAddress();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("group", rapidjson::StringRef(groupName), alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("serverAddress", serverAddress, alloc);
		metadata.AddMember("caData", caData, alloc);
		metadata.AddMember("token", token, alloc);
		metadata.AddMember("namespace", kubeNamespace, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200,
		             "Cluster creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body.c_str());
		clusterID=createData["metadata"]["id"].GetString();
	}
	
	{ //list Group clusters
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/"+groupName+"/clusters?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Listing Group owned clusters should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),1,"Group should own one cluster");
		ENSURE_EQUAL(data["items"][0]["metadata"]["id"].GetString(),clusterID,
		             "Correct cluster ID should be listed");
		ENSURE_EQUAL(data["items"][0]["metadata"]["name"].GetString(),clusterName,
		             "Correct cluster name should be listed");
		ENSURE_EQUAL(data["items"][0]["metadata"]["owningGroup"].GetString(),groupName,
		             "Correct owning Group name should be listed");
	}
}
