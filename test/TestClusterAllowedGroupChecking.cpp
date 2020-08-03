#include "test.h"

#include <set>
#include <utility>

#include <ServerUtilities.h>

TEST(UnauthenticatedListClusterAllowedGroups){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing allowed Group with no authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/some-cluster/allowed_groups/a-group");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list allowed groups without authentication should be rejected");
	
	//try listing clusters with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/some-cluster/allowed_groups/a-group?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list allowed groups with invalid authentication should be rejected");
}

TEST(CheckClusterAllowedGroups){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string groupName1="first-group";
	std::string groupName2="second-group";
	
	auto schema=loadSchema(getSchemaDir()+"/GroupAccessResultSchema.json");
	
	//add a Group to register a cluster with
	std::string groupID1;
	{
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName1, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
							 to_string(createGroup));
		ENSURE_EQUAL(groupResp.status,200, "Group creation request should succeed");
		ENSURE(!groupResp.body.empty());
		rapidjson::Document groupData;
		groupData.Parse(groupResp.body);
		groupID1=groupData["metadata"]["id"].GetString();
	}
	
	//register a cluster
	std::string clusterID;
	{
		auto kubeConfig=tc.getKubeConfig();
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", groupID1, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request1));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document clusterData;
		clusterData.Parse(createResp.body);
		clusterID=clusterData["metadata"]["id"].GetString();
	}
	
	//check the groups which can use the cluster
	{
		auto checkResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
		                      "/allowed_groups/"+groupName1+"?token="+adminKey);
		ENSURE_EQUAL(checkResp.status,200, "Group access check request should succeed");
		ENSURE(!checkResp.body.empty());
		rapidjson::Document checkData;
		checkData.Parse(checkResp.body);
		ENSURE_CONFORMS(checkData,schema);
		ENSURE_EQUAL(checkData["cluster"].GetString(),clusterID,"ID of cluster should match query");
		ENSURE_EQUAL(checkData["group"].GetString(),groupName1,"Name of Group should match query");
		ENSURE_EQUAL(checkData["accessAllowed"].GetBool(),true,"Owning group should have access");
	}
	
	//add another Group to give access to the cluster
	std::string groupID2;
	{
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName2, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
							 to_string(createGroup));
		ENSURE_EQUAL(groupResp.status,200, "Group creation request should succeed");
		ENSURE(!groupResp.body.empty());
		rapidjson::Document groupData;
		groupData.Parse(groupResp.body);
		groupID2=groupData["metadata"]["id"].GetString();
	}
	
	{
		auto checkResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
							   "/allowed_groups/"+groupName2+"?token="+adminKey);
		ENSURE_EQUAL(checkResp.status,200, "Group access check request should succeed");
		ENSURE(!checkResp.body.empty());
		rapidjson::Document checkData;
		checkData.Parse(checkResp.body);
		ENSURE_CONFORMS(checkData,schema);
		ENSURE_EQUAL(checkData["cluster"].GetString(),clusterID,"ID of cluster should match query");
		ENSURE_EQUAL(checkData["group"].GetString(),groupName2,"Name of Group should match query");
		ENSURE_EQUAL(checkData["accessAllowed"].GetBool(),false,"Unrelated group should not have access");
	}
	
	{ //grant the new Group access to the cluster
		auto accessResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
		                    "/allowed_groups/"+groupID2+"?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "Group access grant request should succeed: "+accessResp.body);
	}
	
	//check the groups which can use the cluster again
	{
		auto checkResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
							   "/allowed_groups/"+groupName1+"?token="+adminKey);
		ENSURE_EQUAL(checkResp.status,200, "Group access check request should succeed");
		ENSURE(!checkResp.body.empty());
		rapidjson::Document checkData;
		checkData.Parse(checkResp.body);
		ENSURE_CONFORMS(checkData,schema);
		ENSURE_EQUAL(checkData["cluster"].GetString(),clusterID,"ID of cluster should match query");
		ENSURE_EQUAL(checkData["group"].GetString(),groupName1,"Name of Group should match query");
		ENSURE_EQUAL(checkData["accessAllowed"].GetBool(),true,"Owning group should have access");
	}
	{
		auto checkResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
							   "/allowed_groups/"+groupName2+"?token="+adminKey);
		ENSURE_EQUAL(checkResp.status,200, "Group access check request should succeed");
		ENSURE(!checkResp.body.empty());
		rapidjson::Document checkData;
		checkData.Parse(checkResp.body);
		ENSURE_CONFORMS(checkData,schema);
		ENSURE_EQUAL(checkData["cluster"].GetString(),clusterID,"ID of cluster should match query");
		ENSURE_EQUAL(checkData["group"].GetString(),groupName2,"Name of Group should match query");
		ENSURE_EQUAL(checkData["accessAllowed"].GetBool(),true,"Second group should have access");
	}
	
	{ //grant the all groups access to the cluster
		auto accessResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
		                    "/allowed_groups/*?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "Universal access grant request should succeed: "+accessResp.body);
	}
	
	//list the groups which can use the cluster again
	{
		auto checkResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
							   "/allowed_groups/"+groupName1+"?token="+adminKey);
		ENSURE_EQUAL(checkResp.status,200, "Group access check request should succeed");
		ENSURE(!checkResp.body.empty());
		rapidjson::Document checkData;
		checkData.Parse(checkResp.body);
		ENSURE_CONFORMS(checkData,schema);
		ENSURE_EQUAL(checkData["cluster"].GetString(),clusterID,"ID of cluster should match query");
		ENSURE_EQUAL(checkData["group"].GetString(),groupName1,"Name of Group should match query");
		ENSURE_EQUAL(checkData["accessAllowed"].GetBool(),true,"Owning group should have access");
	}
	{
		auto checkResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
							   "/allowed_groups/"+groupName2+"?token="+adminKey);
		ENSURE_EQUAL(checkResp.status,200, "Group access check request should succeed");
		ENSURE(!checkResp.body.empty());
		rapidjson::Document checkData;
		checkData.Parse(checkResp.body);
		ENSURE_CONFORMS(checkData,schema);
		ENSURE_EQUAL(checkData["cluster"].GetString(),clusterID,"ID of cluster should match query");
		ENSURE_EQUAL(checkData["group"].GetString(),groupName2,"Name of Group should match query");
		ENSURE_EQUAL(checkData["accessAllowed"].GetBool(),true,"Second group should have access");
	}
}

TEST(MalformedCheckClusterAllowedGroups){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	
	std::string clusterID1="nonexistent-cluster";
	std::string clusterID2="cluster_does_not_exist";
	
	{ //attempt to check nonexistent cluster by name
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID1+
		                      "/allowed_groups?token="+adminKey);
		ENSURE_EQUAL(listResp.status,404, 
		             "Group access list request for nonexistent cluster should be rejected");
	}
	{ //attempt to check nonexistent cluster by 'ID'
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID2+
		                      "/allowed_groups?token="+adminKey);
		ENSURE_EQUAL(listResp.status,404, 
		             "Group access list request for nonexistent cluster should be rejected");
	}
	
	std::string groupName1="first-group";
	std::string groupName2="second-group";
	
	//add a Group to register a cluster with
	std::string groupID1;
	{
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName1, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
								to_string(createGroup));
		ENSURE_EQUAL(groupResp.status,200, "Group creation request should succeed");
		ENSURE(!groupResp.body.empty());
		rapidjson::Document groupData;
		groupData.Parse(groupResp.body);
		groupID1=groupData["metadata"]["id"].GetString();
	}
	
	//register a cluster
	std::string clusterID;
	{
		auto kubeConfig=tc.getKubeConfig();
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", groupID1, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
								 to_string(request1));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document clusterData;
		clusterData.Parse(createResp.body);
		clusterID=clusterData["metadata"]["id"].GetString();
	}
	
	//attempt to check access of a non-existent group
	{
		auto checkResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
							   "/allowed_groups/"+groupName2+"?token="+adminKey);
		ENSURE_EQUAL(checkResp.status,404, "Group access check request for a non-existent group should fail");
	}
}
