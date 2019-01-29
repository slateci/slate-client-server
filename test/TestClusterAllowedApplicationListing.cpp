#include "test.h"

#include <set>
#include <utility>

#include <ServerUtilities.h>

TEST(UnauthenticatedListVOAllowedApplications){
	using namespace httpRequests;
	TestContext tc;
	
	//try adding an allowed application with no authentication
	auto listResp=httpGet(tc.getAPIServerURL()+
	                      "/"+currentAPIVersion+"/clusters/some-cluster/allowed_vos/some-vo/applications");
	ENSURE_EQUAL(listResp.status,403,
	             "Requests to list the applications a VO is permitted to use without "
	             "authentication should be rejected");
	
	//try adding an allowed application with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+
	                 "/"+currentAPIVersion+"/clusters/some-cluster/allowed_vos/some-vo/applications"
	                 "?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
	             "Requests to list the applications a VO is permitted to use with "
	             "invalid authentication should be rejected");
}

TEST(ListVOAllowedApplications){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName1="vo-app-use-list-owning-vo";
	std::string voName2="vo-app-use-list-guest-vo";
	
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
	
	auto schema=loadSchema(getSchemaDir()+"/AllowedAppListResultSchema.json");
	
	{ //list allowed applications for the owning VO
		auto listResp=httpGet(tc.getAPIServerURL()+
							  "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_vos/"+voID1+
							  "/applications?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Request to list allowed applications should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body.c_str());
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),1,"One application permission record should be returned");
		ENSURE_EQUAL(data["items"][0].GetString(),std::string("<all>"),"Universal permission should be given by default");
	}
	
	{ //list allowed applications for the guest VO
		auto listResp=httpGet(tc.getAPIServerURL()+
							  "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_vos/"+voID2+
							  "/applications?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Request to list allowed applications should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body.c_str());
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),1,"One application permission record should be returned");
		ENSURE_EQUAL(data["items"][0].GetString(),std::string("<all>"),"Universal permission should be given by default");
	}
	
	{ //grant the new VO permission to use the test application
		auto addResp=httpPut(tc.getAPIServerURL()+
							 "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_vos/"+voID2+"/applications/test-app?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,200);
	}
	
	{ //list allowed applications for the guest VO again
		auto listResp=httpGet(tc.getAPIServerURL()+
							  "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_vos/"+voID2+
							  "/applications?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Request to list allowed applications should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body.c_str());
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),1,"One application permission record should be returned");
		ENSURE_EQUAL(data["items"][0].GetString(),std::string("test-app"),"Permission should be given to use the test application");
	}
	
	{ //grant the new VO permission to use another application
		auto addResp=httpPut(tc.getAPIServerURL()+
							 "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_vos/"+voID2+"/applications/another-app?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,200);
	}
	
	{ //list allowed applications for the guest VO again
		auto listResp=httpGet(tc.getAPIServerURL()+
							  "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_vos/"+voID2+
							  "/applications?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Request to list allowed applications should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body.c_str());
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),2,"Two application permission records should be returned");
		ENSURE_EQUAL(data["items"][0].GetString(),std::string("another-app"),"Permission should be given to use a second application");
		ENSURE_EQUAL(data["items"][1].GetString(),std::string("test-app"),"Permission should be given to use the test application");
	}
	
	{ //revoke permissions for the second application
		auto delResp=httpDelete(tc.getAPIServerURL()+
							    "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_vos/"+voID2+"/applications/another-app?token="+adminKey);
		ENSURE_EQUAL(delResp.status,200);
	}
	
	{ //list allowed applications for the guest VO again
		auto listResp=httpGet(tc.getAPIServerURL()+
							  "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_vos/"+voID2+
							  "/applications?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Request to list allowed applications should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body.c_str());
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),1,"One application permission record should be returned");
		ENSURE_EQUAL(data["items"][0].GetString(),std::string("test-app"),"Permission should be given to use the test application");
	}
	
	{ //revoke all application permissions
		auto delResp=httpDelete(tc.getAPIServerURL()+
							    "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_vos/"+voID2+"/applications/*?token="+adminKey);
		ENSURE_EQUAL(delResp.status,200);
	}
	
	{ //list allowed applications for the guest VO again
		auto listResp=httpGet(tc.getAPIServerURL()+
							  "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_vos/"+voID2+
							  "/applications?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Request to list allowed applications should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body.c_str());
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),0,"No application permission records should be returned");
	}
	
	{ //grant the new VO permission to use all applications
		auto addResp=httpPut(tc.getAPIServerURL()+
							 "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_vos/"+voID2+"/applications/*?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,200);
	}
	
	{ //list allowed applications for the guest VO
		auto listResp=httpGet(tc.getAPIServerURL()+
							  "/"+currentAPIVersion+"/clusters/"+clusterID+"/allowed_vos/"+voID2+
							  "/applications?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Request to list allowed applications should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body.c_str());
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),1,"One application permission record should be returned");
		ENSURE_EQUAL(data["items"][0].GetString(),std::string("<all>"),"Universal permission should be given");
	}
}

TEST(MalformedListAllowedApplications){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName1="owning-vo";
	std::string voName2="guest-vo";
	
	{ //attempt to list permissions for applications on a cluster which does not exist
		auto accessResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+"nonexistent-cluster"+
								"/allowed_vos/"+voName2+"/applications?token="+adminKey);
		ENSURE_EQUAL(accessResp.status,404, 
		             "Request to list permissions for applications on a nonexistent cluster should be rejected");
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
	
	{ //attempt to list permissions for applications to a VO which does not exist
		auto accessResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								"/allowed_vos/nonexistent-vo/applications?token="+adminKey);
		ENSURE_EQUAL(accessResp.status,404, 
		             "Request to list permissions for application use by a nonexistent VO");
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
	
	{ //have the unrelated user attempt to list permissions
		auto accessResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								"/allowed_vos/"+voID1+"/applications?token="+tok);
		ENSURE_EQUAL(accessResp.status,403, 
		             "Request to list permissions for application use by an unrelated user should be rejected");
	}
}
