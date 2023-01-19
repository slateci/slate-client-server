#include "test.h"

#include <PersistentStore.h>
#include <ServerUtilities.h>

TEST(UnauthenticatedAddGroupAllowedApplication){
	using namespace httpRequests;
	TestContext tc;
	
	//try adding an allowed application with no authentication
	auto addResp=httpPut(tc.getAPIServerURL()+
	                     "/"+currentAPIVersion+"/clusters/some-cluster/allowed_groups/some-group/applications/some_app","");
	ENSURE_EQUAL(addResp.status,403,
	             "Requests to grant a Group permission to use an application without "
	             "authentication should be rejected");
	
	//try adding an allowed application with invalid authentication
	addResp=httpPut(tc.getAPIServerURL()+
	                "/"+currentAPIVersion+"/clusters/some-cluster/allowed_groups/some-group/applications/some_app"
	                "?token=00112233-4455-6677-8899-aabbccddeeff","");
	ENSURE_EQUAL(addResp.status,403,
	             "Requests to grant a Group permission to use an application with "
	             "invalid authentication should be rejected");
}

TEST(AllowGroupUseOfSingleApplication){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string groupName1="single-app-use-allow-owning-group";
	std::string groupName2="single-app-use-allow-guest-group";
	
	std::string groupID1;
	{ //add a Group to register a cluster with
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
	
	std::string clusterID;
	{ //register a cluster
		auto kubeConfig = tc.getKubeConfig();
		auto caData = tc.getServerCAData();
		auto token = tc.getUserToken();
		auto kubeNamespace = tc.getKubeNamespace();
		auto serverAddress = tc.getServerAddress();
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", groupID1, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("serverAddress", serverAddress, alloc);
		metadata.AddMember("caData", caData, alloc);
		metadata.AddMember("token", token, alloc);
		metadata.AddMember("namespace", kubeNamespace, alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request1));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document clusterData;
		clusterData.Parse(createResp.body);
		clusterID=clusterData["metadata"]["id"].GetString();
	}
	
	std::string groupID2;
	{ //add another Group to give access to the cluster
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
	
	{ //grant the new Group permission to use the test application
		auto addResp=httpPut(tc.getAPIServerURL()+
							 "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_groups/"+groupID2+"/applications/test-app?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,200);
	}
	
	std::string instID;
	struct cleanupHelper{
		TestContext& tc;
		const std::string& id, key;
		cleanupHelper(TestContext& tc, const std::string& id, const std::string& key):
		tc(tc),id(id),key(key){}
		~cleanupHelper(){
			if(!id.empty())
				auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances/"+id+"?token="+key);
		}
	} cleanup(tc,instID,adminKey);
	{ //test installing an application
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName2, alloc);
		request.AddMember("cluster", clusterID, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		instID=data["metadata"]["id"].GetString();
	}
}

TEST(AllowGroupUseOfAllApplications){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string groupName1="all-app-use-allow-owning-group";
	std::string groupName2="all-app-use-allow-guest-group";
	
	std::string groupID1;
	{ //add a Group to register a cluster with
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
	
	std::string clusterID;
	{ //register a cluster
		auto kubeConfig = tc.getKubeConfig();
		auto caData = tc.getServerCAData();
		auto token = tc.getUserToken();
		auto kubeNamespace = tc.getKubeNamespace();
		auto serverAddress = tc.getServerAddress();

		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", groupID1, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("serverAddress", serverAddress, alloc);
		metadata.AddMember("caData", caData, alloc);
		metadata.AddMember("token", token, alloc);
		metadata.AddMember("namespace", kubeNamespace, alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request1));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document clusterData;
		clusterData.Parse(createResp.body);
		clusterID=clusterData["metadata"]["id"].GetString();
	}
	
	std::string groupID2;
	{ //add another Group to give access to the cluster
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
	
	{ //grant the new Group permission to use the test application
		auto addResp=httpPut(tc.getAPIServerURL()+
							 "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_groups/"+groupID2+"/applications/*?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,200);
	}
	
	std::string instID;
	struct cleanupHelper{
		TestContext& tc;
		const std::string& id, key;
		cleanupHelper(TestContext& tc, const std::string& id, const std::string& key):
		tc(tc),id(id),key(key){}
		~cleanupHelper(){
			if(!id.empty())
				auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances/"+id+"?token="+key);
		}
	} cleanup(tc,instID,adminKey);
	{ //test installing an application
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName2, alloc);
		request.AddMember("cluster", clusterID, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		instID=data["metadata"]["id"].GetString();
	}
}

TEST(MalformedAllowUseOfApplication){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string groupName1="owning-group";
	std::string groupName2="guest-group";
	
	{ //attempt to grant permission for an application on a cluster which does not exist
		auto accessResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+"nonexistent-cluster"+
								"/allowed_groups/"+groupName2+"/applications/test-app?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,404, 
		             "Request to grant permission for an application on a nonexistent cluster should be rejected");
	}
	
	std::string groupID1;
	{ //add a Group to register a cluster with
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
	
	std::string clusterID;
	{ //register a cluster
		auto kubeConfig = tc.getKubeConfig();
		auto caData = tc.getServerCAData();
		auto token = tc.getUserToken();
		auto kubeNamespace = tc.getKubeNamespace();
		auto serverAddress = tc.getServerAddress();
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", groupID1, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("serverAddress", serverAddress, alloc);
		metadata.AddMember("caData", caData, alloc);
		metadata.AddMember("token", token, alloc);
		metadata.AddMember("namespace", kubeNamespace, alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request1));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document clusterData;
		clusterData.Parse(createResp.body);
		clusterID=clusterData["metadata"]["id"].GetString();
	}
	
	{ //attempt to grant permission for an application to a Group which does not exist
		auto accessResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								"/allowed_groups/nonexistent-group/applications/test-app?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,404, 
		             "Request to grant permission for a nonexistent Group to use an application should be rejected");
	}
	
	std::string tok;
	{ //create a user which does not belong to the owning VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("phone", "555-5555", alloc);
		metadata.AddMember("institution", "Center of the Earth University", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		tok=createData["metadata"]["access_token"].GetString();
	}
	
	std::string groupID2;
	{ //add another Group to give access to the cluster
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName2, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+tok,
							 to_string(createGroup));
		ENSURE_EQUAL(groupResp.status,200, "Group creation request should succeed");
		ENSURE(!groupResp.body.empty());
		rapidjson::Document groupData;
		groupData.Parse(groupResp.body);
		groupID2=groupData["metadata"]["id"].GetString();
	}
	
	{ //have the non-owning user attempt to grant permission
		auto accessResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								"/allowed_groups/"+groupID2+"/applications/test-app?token="+tok,"");
		ENSURE_EQUAL(accessResp.status,403, 
		             "Request to grant permission for an application by a non-member of the owning Group should be rejected");
	}
}

TEST(WildcardInteraction){
	DatabaseContext db;
	auto storePtr=db.makePersistentStore();
	auto& store=*storePtr;
	
	Group group1;
	group1.id=idGenerator.generateGroupID();
	group1.name="group1";
	group1.email="abc@def";
	group1.phone="123";
	group1.scienceField="Logic";
	group1.description=" ";
	group1.valid=true;
	
	bool success=store.addGroup(group1);
	ENSURE(success,"Group addition should succeed");
	
	Group group2;
	group2.id=idGenerator.generateGroupID();
	group2.name="group2";
	group2.email="ghi@jkl";
	group2.phone="456";
	group2.scienceField="Logic";
	group2.description=" ";
	group2.valid=true;
	
	success=store.addGroup(group2);
	ENSURE(success,"Group addition should succeed");
	
	Cluster cluster1;
	cluster1.id=idGenerator.generateClusterID();
	cluster1.name="cluster1";
	cluster1.config="-"; //Dynamo will get upset if this is empty, but it will not be used
	cluster1.systemNamespace="-"; //Dynamo will get upset if this is empty, but it will not be used
	cluster1.owningGroup=group1.id;
	cluster1.owningOrganization="kjab";
	cluster1.valid=true;
	
	success=store.addCluster(cluster1);
	ENSURE(success,"Cluster addition should succeed");
	
	success=store.addGroupToCluster(group2.id,cluster1.id);
	
	const std::string testAppName="test-app";
	const std::string universalAppName="<all>";
	
	auto allowed=store.listApplicationsGroupMayUseOnCluster(group2.id,cluster1.id);
	ENSURE_EQUAL(allowed.size(),1,"Universal permission should be granted by default");
	ENSURE_EQUAL(allowed.count(universalAppName),1,"Universal permission should be granted by default");
	
	success=store.allowVoToUseApplication(group2.id,cluster1.id,testAppName);
	ENSURE(success,"Changing application permissions should succeed");
	allowed=store.listApplicationsGroupMayUseOnCluster(group2.id,cluster1.id);
	ENSURE_EQUAL(allowed.size(),1,"Specific permissions should replace universal permissions");
	ENSURE_EQUAL(allowed.count(testAppName),1,"Specific permissions should replace universal permissions");
	
	success=store.allowVoToUseApplication(group2.id,cluster1.id,universalAppName);
	ENSURE(success,"Changing application permissions should succeed");
	allowed=store.listApplicationsGroupMayUseOnCluster(group2.id,cluster1.id);
	ENSURE_EQUAL(allowed.size(),1,"Resetting universal permissions should replace specific permissions");
	ENSURE_EQUAL(allowed.count(universalAppName),1,"Resetting universal permissions should replace specific permissions");
}
