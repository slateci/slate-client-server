#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedUpdateCluster){
	using namespace httpRequests;
	TestContext tc;
	
	//try updating a cluster with no authentication
	auto resp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/Cluster_1234567890","stuff");
	ENSURE_EQUAL(resp.status,403,
	             "Requests update clusters without authentication should be rejected");
	
	//try updating a cluster with invalid authentication
	resp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/Cluster_1234567890?token=00112233-4455-6677-8899-aabbccddeeff","stuff");
	ENSURE_EQUAL(resp.status,403,
	             "Requests to update cluster with invalid authentication should be rejected");
}

TEST(UpdateNonexistentCluster){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	auto resp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/Cluster_1234567890?token="+adminKey,"stuff");
	ENSURE_EQUAL(resp.status,404,
				 "Requests to update a nonexistant cluster should be rejected");
}

TEST(UpdateCluster){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string originalConfig=tc.getKubeConfig();
	//std::string newConfig=originalConfig+"\n\n\n";

	//create Group to register cluster with
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

	//create cluster
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto kubeConfig = tc.getKubeConfig();
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
	auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey,
				 to_string(request1));
	ENSURE_EQUAL(createResp.status,200,
		     "Cluster creation request should succeed");
	ENSURE(!createResp.body.empty());
	rapidjson::Document createData;
	createData.Parse(createResp.body.c_str());
	auto clusterID=createData["metadata"]["id"].GetString();

	auto infoSchema=loadSchema(getSchemaDir()+"/ClusterInfoResultSchema.json");
	
	{ //update cluster's organization
		rapidjson::Document updateRequest(rapidjson::kObjectType);
		auto& alloc = updateRequest.GetAllocator();
		updateRequest.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("owningOrganization", "Department of the Interior", alloc);
		updateRequest.AddMember("metadata", metadata, alloc);

		auto updateResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+"?token="+adminKey,
					 to_string(updateRequest));
		ENSURE_EQUAL(updateResp.status,200,"Updating the cluster config should succeed");
		
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+"?token="+adminKey);
		ENSURE_EQUAL(infoResp.status,200,"Cluster info request should succeed");
		ENSURE(!infoResp.body.empty());
		rapidjson::Document infoData;
		infoData.Parse(infoResp.body.c_str());
		ENSURE_CONFORMS(infoData,infoSchema);
		ENSURE_EQUAL(infoData["metadata"]["name"].GetString(),std::string("testcluster"),
					 "Cluster name should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["owningOrganization"].GetString(),std::string("Department of the Interior"),
					 "Cluster organization should match new value");
	}
	{ //update cluster's organization
		rapidjson::Document updateRequest(rapidjson::kObjectType);
		auto& alloc = updateRequest.GetAllocator();
		updateRequest.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		rapidjson::Value locations(rapidjson::kArrayType);
		rapidjson::Value L1(rapidjson::kObjectType);
		L1.AddMember("lat", 22.7, alloc);
		L1.AddMember("lon", -68, alloc);
		locations.PushBack(L1,alloc);
		rapidjson::Value L2(rapidjson::kObjectType);
		L2.AddMember("lat", 54.66, alloc);
		L2.AddMember("lon", -87.2, alloc);
		locations.PushBack(L2,alloc);
		metadata.AddMember("location", locations, alloc);
		updateRequest.AddMember("metadata", metadata, alloc);

		auto updateResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+"?token="+adminKey,
					 to_string(updateRequest));
		ENSURE_EQUAL(updateResp.status,200,"Updating the cluster config should succeed");
		
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+"?token="+adminKey);
		ENSURE_EQUAL(infoResp.status,200,"Cluster info request should succeed");
		ENSURE(!infoResp.body.empty());
		rapidjson::Document infoData;
		infoData.Parse(infoResp.body.c_str());
		ENSURE_CONFORMS(infoData,infoSchema);
		ENSURE_EQUAL(infoData["metadata"]["name"].GetString(),std::string("testcluster"),
					 "Cluster name should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["owningOrganization"].GetString(),std::string("Department of the Interior"),
					 "Cluster organization should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["location"].GetArray().Size(),2,"Locations array should have two entries");
		ENSURE_EQUAL(infoData["metadata"]["location"][0]["lat"].GetDouble(),22.7,"First location should have correct lattitude");
		ENSURE_EQUAL(infoData["metadata"]["location"][0]["lon"].GetDouble(),-68,"First location should have correct longitude");
		ENSURE_EQUAL(infoData["metadata"]["location"][1]["lat"].GetDouble(),54.66,"Second location should have correct lattitude");
		ENSURE_EQUAL(infoData["metadata"]["location"][1]["lon"].GetDouble(),-87.2,"Second location should have correct longitude");
	}
}

TEST(MalformedUpdateRequests){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	//create Group to register cluster with
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

	auto kubeConfig=tc.getKubeConfig();
	
	//create cluster
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		auto caData = tc.getServerCAData();
		auto token = tc.getUserToken();
		auto kubeNamespace = tc.getKubeNamespace();
		auto serverAddress = tc.getServerAddress();
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
	std::cout << "POSTing to " << tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey << std::endl;
	auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey,
				 to_string(request1));
	ENSURE_EQUAL(createResp.status,200,
		     "Cluster creation request should succeed");
	ENSURE(!createResp.body.empty());
	rapidjson::Document createData;
	createData.Parse(createResp.body.c_str());
	auto clusterID=createData["metadata"]["id"].GetString();

	auto clusterUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+"?token="+adminKey;
	
	{ //invalid JSON request body
		auto updateResp=httpPut(clusterUrl,"This is not JSON");
		ENSURE_EQUAL(updateResp.status,400,"Invalid JSON requests should be rejected");
	}
	{ //empty JSON
		rapidjson::Document request(rapidjson::kObjectType);
		auto updateResp=httpPut(clusterUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Empty JSON requests should be rejected");
	}
	{ //missing metadata
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		auto updateResp=httpPut(clusterUrl, to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Requests without metadata should be rejected");
	}
	{ //wrong organization type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("owningOrganization", true, alloc);
		auto updateResp=httpPut(clusterUrl, to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Requests with invalid owningOrganization should be rejected");
	}
	{ //wrong location type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("location", "The Moon", alloc);
		auto updateResp=httpPut(clusterUrl, to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Requests with invalid location should be rejected");
	}

}

TEST(UpdateClusterForGroupNotMemberOf){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
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
	ENSURE(!groupResp.body.empty());
	rapidjson::Document groupData;
	groupData.Parse(groupResp.body.c_str());
	auto groupID=groupData["metadata"]["id"].GetString();	

	auto kubeConfig = tc.getKubeConfig();
	
	//create cluster
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
	auto createResp=httpPost(createClusterUrl, to_string(request1));
	ENSURE_EQUAL(createResp.status,200,
		     "Cluster creation request should succeed");

	ENSURE(!createResp.body.empty());
	rapidjson::Document createData;
	createData.Parse(createResp.body.c_str());
	auto clusterID=createData["metadata"]["id"].GetString();

	//create second user that isn't part of the created VO
	rapidjson::Document createUser(rapidjson::kObjectType);
	{
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
	}
	auto userResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,
			       to_string(createUser));
	ENSURE_EQUAL(userResp.status,200,
		     "User creation request should succeed");
	ENSURE(!userResp.body.empty());
	rapidjson::Document userData;
	userData.Parse(userResp.body.c_str());
	auto userID=userData["metadata"]["id"].GetString();
	auto userToken=userData["metadata"]["access_token"].GetString();

	//try to update the kubeconfig for the created cluster
	// TODO: is this needed?
//	rapidjson::Document updateRequest(rapidjson::kObjectType);
//	{
//		auto& alloc = updateRequest.GetAllocator();
//		updateRequest.AddMember("apiVersion", currentAPIVersion, alloc);
//	        rapidjson::Value metadata(rapidjson::kObjectType);
//		metadata.AddMember("kubeconfig", tc.getKubeConfig()+"\n\n\n", alloc);
//		updateRequest.AddMember("metadata", metadata, alloc);
//	}
	
//	auto updateResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+"?token="+userToken,
//				to_string(updateRequest));
//	ENSURE_EQUAL(updateResp.status,403,
//		     "User who is not part of the Group owning the cluster should not be able to update the cluster");
}
