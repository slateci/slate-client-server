#include "test.h"

#include <set>
#include <utility>

#include <ServerUtilities.h>

TEST(UnauthenticatedRemoveVOAllowedApplication){
	using namespace httpRequests;
	TestContext tc;
	
	//try removing an allowed application with no authentication
	auto remResp=httpDelete(tc.getAPIServerURL()+
	                     "/"+currentAPIVersion+"/clusters/some-cluster/allowed_vos/some-vo/applications/some_app");
	ENSURE_EQUAL(remResp.status,403,
	             "Requests to deny a VO permission to use an application without "
	             "authentication should be rejected");
	
	//try removing an allowed application with invalid authentication
	remResp=httpDelete(tc.getAPIServerURL()+
	                "/"+currentAPIVersion+"/clusters/some-cluster/allowed_vos/some-vo/applications/some_app"
	                "?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(remResp.status,403,
	             "Requests to deny a VO permission to use an application with "
	             "authentication should be rejected");
}

TEST(DenyVOUseOfSingleApplication){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName1="vo-single-app-use-deny-owning-vo";
	std::string voName2="vo-single-app-use-deny-guest-vo";
	
	std::string voID1;
	{ //add a VO to register a cluster with
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName1, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,
							 to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
		ENSURE(!voResp.body.empty());
		rapidjson::Document voData;
		voData.Parse(voResp.body);
		voID1=voData["metadata"]["id"].GetString();
	}
	
	std::string clusterID;
	{ //register a cluster
		auto kubeConfig=tc.getKubeConfig();
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("vo", voID1, alloc);
		metadata.AddMember("organization", "Department of Labor", alloc);
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
	
	std::string voID2;
	{ //add another VO to give access to the cluster
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName2, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,
							 to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
		ENSURE(!voResp.body.empty());
		rapidjson::Document voData;
		voData.Parse(voResp.body);
		voID2=voData["metadata"]["id"].GetString();
	}
	
	{ //grant the new VO access to the cluster
		auto accessResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								"/allowed_vos/"+voID2+"?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "VO access grant request should succeed: "+accessResp.body);
	}
	
	{ //grant the new VO permission to use the test application
		auto addResp=httpPut(tc.getAPIServerURL()+
							 "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_vos/"+voID2+"/applications/test-app?token="+adminKey,"");
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
		request.AddMember("vo", voName2, alloc);
		request.AddMember("cluster", clusterID, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		instID=data["metadata"]["id"].GetString();
	}
	
	{ //deny the guest VO permission to use the test application
		auto addResp=httpDelete(tc.getAPIServerURL()+
							    "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_vos/"+voID2+"/applications/test-app?token="+adminKey);
		ENSURE_EQUAL(addResp.status,200,"Denying a guest VO permission to use an aplication should succeed");
	}
	
	{ //test installing an application again
		std::string instID;
		cleanupHelper cleanup(tc,instID,adminKey);
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("vo", voName2, alloc);
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

TEST(DenyVOUseOfAllApplications){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName1="vo-single-app-use-deny-owning-vo";
	std::string voName2="vo-single-app-use-deny-guest-vo";
	
	std::string voID1;
	{ //add a VO to register a cluster with
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName1, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,
							 to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
		ENSURE(!voResp.body.empty());
		rapidjson::Document voData;
		voData.Parse(voResp.body);
		voID1=voData["metadata"]["id"].GetString();
	}
	
	std::string clusterID;
	{ //register a cluster
		auto kubeConfig=tc.getKubeConfig();
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("vo", voID1, alloc);
		metadata.AddMember("organization", "Department of Labor", alloc);
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
	
	std::string voID2;
	{ //add another VO to give access to the cluster
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName2, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,
							 to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
		ENSURE(!voResp.body.empty());
		rapidjson::Document voData;
		voData.Parse(voResp.body);
		voID2=voData["metadata"]["id"].GetString();
	}
	
	{ //grant the new VO access to the cluster
		auto accessResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								"/allowed_vos/"+voID2+"?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "VO access grant request should succeed: "+accessResp.body);
	}
	
	{ //deny the guest VO permission to use any applications
		auto addResp=httpDelete(tc.getAPIServerURL()+
							    "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_vos/"+voID2+"/applications/*?token="+adminKey);
		ENSURE_EQUAL(addResp.status,200,"Denying a guest VO permission to use an aplication should succeed");
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
		request.AddMember("vo", voName2, alloc);
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
	std::string voName1="owning-vo";
	std::string voName2="guest-vo";
	
	{ //attempt to deny permission for an application on a cluster which does not exist
		auto accessResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+"nonexistent-cluster"+
								   "/allowed_vos/"+voName2+"/applications/test-app?token="+adminKey);
		ENSURE_EQUAL(accessResp.status,404, 
		             "Request to deny permission for an application on a nonexistent cluster should be rejected");
	}
	
	std::string voID1;
	{ //add a VO to register a cluster with
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName1, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,
							 to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
		ENSURE(!voResp.body.empty());
		rapidjson::Document voData;
		voData.Parse(voResp.body);
		voID1=voData["metadata"]["id"].GetString();
	}
	
	std::string clusterID;
	{ //register a cluster
		auto kubeConfig=tc.getKubeConfig();
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("vo", voID1, alloc);
		metadata.AddMember("organization", "Department of Labor", alloc);
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
	
	{ //attempt to deny permission for an application to a VO which does not exist
		auto accessResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								   "/allowed_vos/nonexistent-vo/applications/test-app?token="+adminKey);
		ENSURE_EQUAL(accessResp.status,404, 
		             "Request to deny permission for a nonexistent VO to use an application should be rejected");
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
	
	std::string voID2;
	{ //add another VO to give access to the cluster
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName2, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+tok,
							 to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
		ENSURE(!voResp.body.empty());
		rapidjson::Document voData;
		voData.Parse(voResp.body);
		voID2=voData["metadata"]["id"].GetString();
	}
	
	{ //have the non-owning user attempt to deny permission
		auto accessResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								   "/allowed_vos/"+voID2+"/applications/test-app?token="+tok);
		ENSURE_EQUAL(accessResp.status,403, 
		             "Request to deny permission for an application by a non-member of the owning VO should be rejected");
	}
}
