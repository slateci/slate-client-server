#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedListClusters){
	using namespace httpRequests;
	TestContext tc;

	//try listing clusters with no authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters");
	ENSURE_EQUAL(listResp.status,403,
		     "Requests to list clusters without authentication should be rejected");

	//try listing clusters with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
		     "Requests to list clusters with invalid authentication should be rejected");
}

TEST(ListClusters){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string clusterURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey;

	auto listResp=httpGet(clusterURL);
	ENSURE_EQUAL(listResp.status,200, "Portal admin user should be able to list clusters");

	ENSURE(!listResp.body.empty());
	rapidjson::Document data;
	data.Parse(listResp.body.c_str());

	auto schema=loadSchema(getSchemaDir()+"/ClusterListResultSchema.json");
	ENSURE_CONFORMS(data,schema);

	//should be no clusters
	ENSURE_EQUAL(data["items"].Size(),0,"There should be no clusters returned");

	//add a Group to register a cluster with
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
	ENSURE_EQUAL(groupResp.status,200, "Group creation request should succeed");
	ENSURE(!groupResp.body.empty());
	rapidjson::Document groupData;
	groupData.Parse(groupResp.body.c_str());
	auto groupID=groupData["metadata"]["id"].GetString();

	//add a cluster
	auto kubeConfig=tc.getKubeConfig();
	
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto caData = tc.getServerCAData();
		auto token = tc.getUserToken();
		auto kubeNamespace = tc.getKubeNamespace();
		auto serverAddress = tc.getServerAddress();

		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", rapidjson::StringRef(groupID), alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("serverAddress", serverAddress, alloc);
		metadata.AddMember("caData", caData, alloc);
		metadata.AddMember("token", token, alloc);
		metadata.AddMember("namespace", kubeNamespace, alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp=httpPost(clusterURL, to_string(request1));
	ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");

	// list results again
	listResp=httpGet(clusterURL);
	ENSURE_EQUAL(listResp.status,200, "Portal admin user should be able to list clusters");

	ENSURE(!listResp.body.empty());
	data.Parse(listResp.body.c_str());
	ENSURE_CONFORMS(data,schema);

	//should now be one cluster
	ENSURE_EQUAL(data["items"].Size(),1,"One cluster record should be returned");
	const auto& metadata=data["items"][0]["metadata"];
	ENSURE_EQUAL(metadata["name"].GetString(), std::string("testcluster"),
		     "Cluster name should match");
	ENSURE_EQUAL(metadata["owningGroup"].GetString(), std::string("testgroup1"),
		     "Cluster owning Group should match");
	ENSURE_EQUAL(metadata["owningOrganization"].GetString(), std::string("Department of Labor"),
		     "Cluster owning organization should match");
	ENSURE(metadata.HasMember("id"));
}
