#include "test.h"

#include <set>
#include <utility>

#include <ServerUtilities.h>

TEST(UnauthenticatedListClusterAllowedgroups){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing allowed Group with no authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/some-cluster/allowed_groups");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list allowed groups without authentication should be rejected");
	
	//try listing clusters with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/some-cluster/allowed_groups?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list allowed groups with invalid authentication should be rejected");
}

TEST(ListClusterAllowedgroups){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string groupName1="first-group";
	std::string groupName2="second-group";
	
	auto schema=loadSchema(getSchemaDir()+"/GroupMembershipListResultSchema.json");
	
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
	
	//list the groups which can use the cluster
	{
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
		                      "/allowed_groups?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200, "Group access list request should succeed");
		ENSURE(!listResp.body.empty());
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		ENSURE_CONFORMS(listData,schema);
		ENSURE_EQUAL(listData["items"].Size(),1,"Only the owning Group should have access to the cluster");
		ENSURE_EQUAL(listData["items"][0]["metadata"]["id"].GetString(),groupID1,"ID of Group with access should match ID of the owning Group");
		ENSURE_EQUAL(listData["items"][0]["metadata"]["name"].GetString(),groupName1,"Name of Group with access should match name of the owning Group");
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
	
	{ //grant the new Group access to the cluster
		auto accessResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
		                    "/allowed_groups/"+groupID2+"?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "Group access grant request should succeed: "+accessResp.body);
	}
	
	//list the groups which can use the cluster again
	{
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
		                      "/allowed_groups?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200, "Group access list request should succeed");
		ENSURE(!listResp.body.empty());
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		ENSURE_CONFORMS(listData,schema);
		std::cout << listResp.body << std::endl;
		ENSURE_EQUAL(listData["items"].Size(),2,"Two groups should now have access to the cluster");
		std::set<std::pair<std::string,std::string>> groups;
		for(const auto& item : listData["items"].GetArray())
			groups.emplace(item["metadata"]["id"].GetString(),item["metadata"]["name"].GetString());
		ENSURE(groups.count(std::make_pair(groupID1,groupName1)),"Owning Group should still have access");
		ENSURE(groups.count(std::make_pair(groupID2,groupName2)),"Additional Group should have access");
	}
	
	{ //grant the all groups access to the cluster
		auto accessResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
		                    "/allowed_groups/*?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "Universal access grant request should succeed: "+accessResp.body);
	}
	
	//list the groups which can use the cluster again
	{
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
		                      "/allowed_groups?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200, "Group access list request should succeed");
		ENSURE(!listResp.body.empty());
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		ENSURE_CONFORMS(listData,schema);
		std::cout << listResp.body << std::endl;
		ENSURE_EQUAL(listData["items"].Size(),1,"One pseudo-Group should now have access to the cluster");
		std::set<std::pair<std::string,std::string>> groups;
		for(const auto& item : listData["items"].GetArray())
			groups.emplace(item["metadata"]["id"].GetString(),item["metadata"]["name"].GetString());
		ENSURE(groups.count(std::make_pair("*","<all>")),"All groups should have access");
	}
}

TEST(MalformedListClusterAllowedgroups){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	
	std::string clusterID1="nonexistent-cluster";
	std::string clusterID2="Cluster_does_not_exist";
	
	{ //attempt to list nonexistent cluster by name
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID1+
		                      "/allowed_groups?token="+adminKey);
		ENSURE_EQUAL(listResp.status,404, 
		             "Group access list request for nonexistent cluster should be rejected");
	}
	{ //attempt to list nonexistent cluster by 'ID'
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID2+
		                      "/allowed_groups?token="+adminKey);
		ENSURE_EQUAL(listResp.status,404, 
		             "Group access list request for nonexistent cluster should be rejected");
	}
}
