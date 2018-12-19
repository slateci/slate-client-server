#include "test.h"

#include <set>
#include <utility>

#include <Utilities.h>

TEST(UnauthenticatedRemoveClusterAllowedVO){
	using namespace httpRequests;
	TestContext tc;
	
	//try removing an allowed VO with no authentication
	auto remResp=httpDelete(tc.getAPIServerURL()+
	                     "/"+currentAPIVersion+"/clusters/some-cluster/allowed_vos/some-vo");
	ENSURE_EQUAL(remResp.status,403,
	             "Requests to revoke a VO's access to a cluster without "
	             "authentication should be rejected");
	
	//try removing an allowed VO with invalid authentication
	remResp=httpDelete(tc.getAPIServerURL()+
	                "/"+currentAPIVersion+"/clusters/some-cluster/allowed_vos/some-vo"
	                "?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(remResp.status,403,
	             "Requests to revoke a VO's access to a cluster with invalid "
	             "authentication should be rejected");
}

TEST(RemoveVOAccessToCluster){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName1="vo-access-deny-owning-vo";
	std::string voName2="vo-access-deny-guest-vo";
	
	std::string voID1;
	{ //add a VO to register a cluster with
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName1, alloc);
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
	
	{ //list the VOs which can use the cluster
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
		                      "/allowed_vos?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200, "VO access list request should succeed");
		ENSURE(!listResp.body.empty());
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		std::cout << listResp.body << std::endl;
		ENSURE_EQUAL(listData["items"].Size(),2,"Two VOs should now have access to the cluster");
		std::set<std::pair<std::string,std::string>> vos;
		for(const auto& item : listData["items"].GetArray())
			vos.emplace(item["metadata"]["id"].GetString(),item["metadata"]["name"].GetString());
		ENSURE(vos.count(std::make_pair(voID1,voName1)),"Owning VO should still have access");
		ENSURE(vos.count(std::make_pair(voID2,voName2)),"Additional VO should have access");
	}
	
	{ //remove the new VO's access to the cluster again
		auto revokeResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								   "/allowed_vos/"+voID2+"?token="+adminKey);
		ENSURE_EQUAL(revokeResp.status,200, "VO access removal request should succeed: "+revokeResp.body);
	}
	
	{ //list the VOs which can use the cluster again
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
		                      "/allowed_vos?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200, "VO access list request should succeed");
		ENSURE(!listResp.body.empty());
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		std::cout << listResp.body << std::endl;
		ENSURE_EQUAL(listData["items"].Size(),1,"One VO should now have access to the cluster");
		std::set<std::pair<std::string,std::string>> vos;
		for(const auto& item : listData["items"].GetArray())
			vos.emplace(item["metadata"]["id"].GetString(),item["metadata"]["name"].GetString());
		ENSURE(vos.count(std::make_pair(voID1,voName1)),"Owning VO should still have access");
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
		ENSURE_EQUAL(instResp.status,403,"Application install request should fail after access is removed");
	}
}

TEST(RemoveUniversalAccessToCluster){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName1="universal-access-deny-owning-vo";
	std::string voName2="universal-access-deny-guest-vo";
	
	std::string voID1;
	{ //add a VO to register a cluster with
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName1, alloc);
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
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,
							 to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
		ENSURE(!voResp.body.empty());
		rapidjson::Document voData;
		voData.Parse(voResp.body);
		voID2=voData["metadata"]["id"].GetString();
	}
	
	{ //grant all VOs access to the cluster
		auto accessResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								"/allowed_vos/*?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "VO access grant request should succeed: "+accessResp.body);
	}
	
	{ //list the VOs which can use the cluster
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
		                      "/allowed_vos?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200, "VO access list request should succeed");
		ENSURE(!listResp.body.empty());
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		std::cout << listResp.body << std::endl;
		ENSURE_EQUAL(listData["items"].Size(),1,"One pseudo-VO should now have access to the cluster");
		std::set<std::pair<std::string,std::string>> vos;
		for(const auto& item : listData["items"].GetArray())
			vos.emplace(item["metadata"]["id"].GetString(),item["metadata"]["name"].GetString());
		ENSURE(vos.count(std::make_pair("*","<all>")),"All VOs should have access");
	}
	
	{ //remove non-owning VOs' access to the cluster again
		auto revokeResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								   "/allowed_vos/*?token="+adminKey);
		ENSURE_EQUAL(revokeResp.status,200, "VO access removal request should succeed: "+revokeResp.body);
	}
	
	{ //list the VOs which can use the cluster again
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
		                      "/allowed_vos?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200, "VO access list request should succeed");
		ENSURE(!listResp.body.empty());
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		std::cout << listResp.body << std::endl;
		ENSURE_EQUAL(listData["items"].Size(),1,"One VO should now have access to the cluster");
		std::set<std::pair<std::string,std::string>> vos;
		for(const auto& item : listData["items"].GetArray())
			vos.emplace(item["metadata"]["id"].GetString(),item["metadata"]["name"].GetString());
		ENSURE(vos.count(std::make_pair(voID1,voName1)),"Owning VO should still have access");
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
		ENSURE_EQUAL(instResp.status,403,"Application install request should fail after access is removed");
	}
}

TEST(MalformedRevokeVOAccessToCluster){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName1="owning-vo";
	std::string voName2="guest-vo";
	
	{ //attempt to revoke access to a cluster which does not exist
		auto revokeResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+"nonexistent-cluster"+
								   "/allowed_vos/"+voName2+"?token="+adminKey);
		ENSURE_EQUAL(revokeResp.status,404, 
		             "Request to revoke access to a nonexistent cluster should be rejected");
	}
	
	std::string voID1;
	{ //add a VO to register a cluster with
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName1, alloc);
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
	
	{ //attempt to revoke access for a VO which does not exist
		auto revokeResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								   "/allowed_vos/"+"nonexistent-vo"+"?token="+adminKey);
		ENSURE_EQUAL(revokeResp.status,404, 
		             "Request to revoke access for a nonexistent VO should be rejected");
	}
	
	std::string tok;
	{ //create a user which does not belong to the owning VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
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
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+tok,
							 to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
		ENSURE(!voResp.body.empty());
		rapidjson::Document voData;
		voData.Parse(voResp.body);
		voID2=voData["metadata"]["id"].GetString();
	}
	
	{ //have the non-owning user attempt to revoke access
		auto revokeResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								   "/allowed_vos/"+voID2+"?token="+tok);
		ENSURE_EQUAL(revokeResp.status,403, 
		             "Request to revoke access by a non-member of the owning VO should be rejected");
	}
}
