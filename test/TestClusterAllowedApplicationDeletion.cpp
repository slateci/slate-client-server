#include "test.h"

#include <set>
#include <utility>

#include <ServerUtilities.h>

TEST(UnauthenticatedRemoveGroupAllowedApplication){
	using namespace httpRequests;
	TestContext tc;
	
	//try removing an allowed application with no authentication
	auto remResp=httpDelete(tc.getAPIServerURL()+
	                     "/"+currentAPIVersion+"/clusters/some-cluster/allowed_groups/some-group/applications/some_app");
	ENSURE_EQUAL(remResp.status,403,
	             "Requests to deny a Group permission to use an application without "
	             "authentication should be rejected");
	
	//try removing an allowed application with invalid authentication
	remResp=httpDelete(tc.getAPIServerURL()+
	                "/"+currentAPIVersion+"/clusters/some-cluster/allowed_groups/some-group/applications/some_app"
	                "?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(remResp.status,403,
	             "Requests to deny a Group permission to use an application with "
	             "authentication should be rejected");
}

TEST(DenyGroupUseOfSingleApplication){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string groupName1="single-app-use-deny-owning-group";
	std::string groupName2="single-app-use-deny-guest-group";
	
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
	
	struct cleanupHelper{
		TestContext& tc;
		const std::string& id, key;
		cleanupHelper(TestContext& tc, const std::string& id, const std::string& key):
		tc(tc),id(id),key(key){}
		~cleanupHelper(){
			if(!id.empty())
				auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances/"+id+"?token="+key);
		}
	};
	{ //test installing an application
		std::string instID;
		cleanupHelper cleanup(tc,instID,adminKey);
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
	
	{ //deny the guest Group permission to use the test application
		auto addResp=httpDelete(tc.getAPIServerURL()+
							    "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_groups/"+groupID2+"/applications/test-app?token="+adminKey);
		ENSURE_EQUAL(addResp.status,200,"Denying a guest Group permission to use an aplication should succeed");
	}
	
	{ //test installing an application again
		std::string instID;
		cleanupHelper cleanup(tc,instID,adminKey);
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName2, alloc);
		request.AddMember("cluster", clusterID, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/test-app?test&token="+adminKey,to_string(request));
		if(instResp.status==200){
			rapidjson::Document data;
			data.Parse(instResp.body);
			instID=data["metadata"]["id"].GetString();
		}
		ENSURE_EQUAL(instResp.status,403,"Application install request should fail");
	}
}

TEST(DenyGroupUseOfAllApplications){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string groupName1="single-app-use-deny-owning-group";
	std::string groupName2="single-app-use-deny-guest-group";
	
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
	
	{ //deny the guest Group permission to use any applications
		auto addResp=httpDelete(tc.getAPIServerURL()+
							    "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_groups/"+groupID2+"/applications/*?token="+adminKey);
		ENSURE_EQUAL(addResp.status,200,"Denying a guest Group permission to use an aplication should succeed");
	}
	
	struct cleanupHelper{
		TestContext& tc;
		const std::string& id, key;
		cleanupHelper(TestContext& tc, const std::string& id, const std::string& key):
		tc(tc),id(id),key(key){}
		~cleanupHelper(){
			if(!id.empty())
				auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances/"+id+"?token="+key);
		}
	};
	
	{ //test installing an application
		std::string instID;
		cleanupHelper cleanup(tc,instID,adminKey);
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName2, alloc);
		request.AddMember("cluster", clusterID, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/test-app?test&token="+adminKey,to_string(request));
		if(instResp.status==200){
			rapidjson::Document data;
			data.Parse(instResp.body);
			instID=data["metadata"]["id"].GetString();
		}
		ENSURE_EQUAL(instResp.status,403,"Application install request should fail");
	}
}

TEST(MalformedDenyUseOfApplication){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string groupName1="owning-group";
	std::string groupName2="guest-group";
	
	{ //attempt to deny permission for an application on a cluster which does not exist
		auto accessResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+"nonexistent-cluster"+
								   "/allowed_groups/"+groupName2+"/applications/test-app?token="+adminKey);
		ENSURE_EQUAL(accessResp.status,404, 
		             "Request to deny permission for an application on a nonexistent cluster should be rejected");
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
	
	{ //attempt to deny permission for an application to a Group which does not exist
		auto accessResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								   "/allowed_groups/nonexistent-group/applications/test-app?token="+adminKey);
		ENSURE_EQUAL(accessResp.status,404, 
		             "Request to deny permission for a nonexistent Group to use an application should be rejected");
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
	
	{ //have the non-owning user attempt to deny permission
		auto accessResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								   "/allowed_groups/"+groupID2+"/applications/test-app?token="+tok);
		ENSURE_EQUAL(accessResp.status,403, 
		             "Request to deny permission for an application by a non-member of the owning Group should be rejected");
	}
}
