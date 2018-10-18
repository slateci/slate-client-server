#include "test.h"

#include <set>
#include <utility>

#include <PersistentStore.h>
#include <Utilities.h>

TEST(UnauthenticatedAddVOAllowedApplication){
	using namespace httpRequests;
	TestContext tc;
	
	//try adding an allowed application with no authentication
	auto addResp=httpPut(tc.getAPIServerURL()+
	                     "/v1alpha1/clusters/some-cluster/allowed_vos/some-vo/applications/some_app","");
	ENSURE_EQUAL(addResp.status,403,
	             "Requests to grant a VO permission to use an application without "
	             "authentication should be rejected");
	
	//try adding an allowed application with invalid authentication
	addResp=httpPut(tc.getAPIServerURL()+
	                "/v1alpha1/clusters/some-cluster/allowed_vos/some-vo/applications/some_app"
	                "?token=00112233-4455-6677-8899-aabbccddeeff","");
	ENSURE_EQUAL(addResp.status,403,
	             "Requests to grant a VO permission to use an application with "
	             "invalid authentication should be rejected");
}

TEST(AllowVOUseOfSingleApplication){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName1="vo-single-app-use-allow-owning-vo";
	std::string voName2="vo-single-app-use-allow-guest-vo";
	
	std::string voID1;
	{ //add a VO to register a cluster with
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName1, alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,
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
		request1.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("vo", voID1, alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey, 
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
		createVO.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName2, alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,
							 to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
		ENSURE(!voResp.body.empty());
		rapidjson::Document voData;
		voData.Parse(voResp.body);
		voID2=voData["metadata"]["id"].GetString();
	}
	
	{ //grant the new VO access to the cluster
		auto accessResp=httpPut(tc.getAPIServerURL()+"/v1alpha1/clusters/"+clusterID+
								"/allowed_vos/"+voID2+"?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "VO access grant request should succeed: "+accessResp.body);
	}
	
	{ //grant the new VO permission to use the test application
		auto addResp=httpPut(tc.getAPIServerURL()+
							 "/v1alpha1/clusters/"+clusterID+"/allowed_vos/"+voID2+"/applications/test-app?token="+adminKey,"");
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
				auto delResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/instances/"+id+"?token="+key);
		}
	} cleanup(tc,instID,adminKey);
	{ //test installing an application
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", voName2, alloc);
		request.AddMember("cluster", clusterID, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		instID=data["metadata"]["id"].GetString();
	}
}

TEST(AllowVOUseOfAllApplications){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName1="vo-all-app-use-allow-owning-vo";
	std::string voName2="vo-all-app-use-allow-guest-vo";
	
	std::string voID1;
	{ //add a VO to register a cluster with
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName1, alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,
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
		request1.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("vo", voID1, alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey, 
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
		createVO.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName2, alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,
							 to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
		ENSURE(!voResp.body.empty());
		rapidjson::Document voData;
		voData.Parse(voResp.body);
		voID2=voData["metadata"]["id"].GetString();
	}
	
	{ //grant the new VO access to the cluster
		auto accessResp=httpPut(tc.getAPIServerURL()+"/v1alpha1/clusters/"+clusterID+
								"/allowed_vos/"+voID2+"?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "VO access grant request should succeed: "+accessResp.body);
	}
	
	{ //grant the new VO permission to use the test application
		auto addResp=httpPut(tc.getAPIServerURL()+
							 "/v1alpha1/clusters/"+clusterID+"/allowed_vos/"+voID2+"/applications/*?token="+adminKey,"");
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
				auto delResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/instances/"+id+"?token="+key);
		}
	} cleanup(tc,instID,adminKey);
	{ //test installing an application
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", voName2, alloc);
		request.AddMember("cluster", clusterID, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		instID=data["metadata"]["id"].GetString();
	}
}

TEST(MalformedAllowUseOfApplication){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName1="owning-vo";
	std::string voName2="guest-vo";
	
	{ //attempt to grant permission for an application on a cluster which does not exist
		auto accessResp=httpPut(tc.getAPIServerURL()+"/v1alpha1/clusters/"+"nonexistent-cluster"+
								"/allowed_vos/"+voName2+"/applications/test-app?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,404, 
		             "Request to grant permission for an application on a nonexistent cluster should be rejected");
	}
	
	std::string voID1;
	{ //add a VO to register a cluster with
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName1, alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,
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
		request1.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("vo", voID1, alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey, 
		                         to_string(request1));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document clusterData;
		clusterData.Parse(createResp.body);
		clusterID=clusterData["metadata"]["id"].GetString();
	}
	
	{ //attempt to grant permission for an application to a VO which does not exist
		auto accessResp=httpPut(tc.getAPIServerURL()+"/v1alpha1/clusters/"+clusterID+
								"/allowed_vos/nonexistent-vo/applications/test-app?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,404, 
		             "Request to grant permission for a nonexistent VO to use an application should be rejected");
	}
	
	std::string tok;
	{ //create a user which does not belong to the owning VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		tok=createData["metadata"]["access_token"].GetString();
	}
	
	std::string voID2;
	{ //add another VO to give access to the cluster
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName2, alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+tok,
							 to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
		ENSURE(!voResp.body.empty());
		rapidjson::Document voData;
		voData.Parse(voResp.body);
		voID2=voData["metadata"]["id"].GetString();
	}
	
	{ //have the non-owning user attempt to grant permission
		auto accessResp=httpPut(tc.getAPIServerURL()+"/v1alpha1/clusters/"+clusterID+
								"/allowed_vos/"+voID2+"/applications/test-app?token="+tok,"");
		ENSURE_EQUAL(accessResp.status,403, 
		             "Request to grant permission for an application by a non-member of the owning VO should be rejected");
	}
}

TEST(WildcardInteraction){
	auto dbResp=httpRequests::httpGet("http://localhost:52000/dynamo/create");
	ENSURE_EQUAL(dbResp.status,200);
	std::string dbPort=dbResp.body;
	
	const std::string awsAccessKey="foo";
	const std::string awsSecretKey="bar";
	Aws::SDKOptions options;
	Aws::InitAPI(options);
	using AWSOptionsHandle=std::unique_ptr<Aws::SDKOptions,void(*)(Aws::SDKOptions*)>;
	AWSOptionsHandle opt_holder(&options,
								[](Aws::SDKOptions* options){
									Aws::ShutdownAPI(*options); 
								});
	Aws::Auth::AWSCredentials credentials(awsAccessKey,awsSecretKey);
	Aws::Client::ClientConfiguration clientConfig;
	clientConfig.scheme=Aws::Http::Scheme::HTTP;
	clientConfig.endpointOverride="localhost:"+dbPort;
	
	PersistentStore store(credentials,clientConfig,
	                      "slate_portal_user","encryptionKey",
	                      "",9200);
	
	VO vo1;
	vo1.id=idGenerator.generateVOID();
	vo1.name="vo1";
	vo1.valid=true;
	
	bool success=store.addVO(vo1);
	ENSURE(success,"VO addition should succeed");
	
	VO vo2;
	vo2.id=idGenerator.generateVOID();
	vo2.name="vo2";
	vo2.valid=true;
	
	success=store.addVO(vo2);
	ENSURE(success,"VO addition should succeed");
	
	Cluster cluster1;
	cluster1.id=idGenerator.generateClusterID();
	cluster1.name="cluster1";
	cluster1.config="-"; //Dynamo will get upset if this is empty, but it will not be used
	cluster1.systemNamespace="-"; //Dynamo will get upset if this is empty, but it will not be used
	cluster1.owningVO=vo1.id;
	cluster1.valid=true;
	
	success=store.addCluster(cluster1);
	ENSURE(success,"Cluster addition should succeed");
	
	success=store.addVOToCluster(vo2.id,cluster1.id);
	
	const std::string testAppName="test-app";
	const std::string universalAppName="<all>";
	
	auto allowed=store.listApplicationsVOMayUseOnCluster(vo2.id,cluster1.id);
	ENSURE_EQUAL(allowed.size(),1,"Universal permission should be granted by default");
	ENSURE_EQUAL(allowed.count(universalAppName),1,"Universal permission should be granted by default");
	
	success=store.allowVoToUseApplication(vo2.id,cluster1.id,testAppName);
	ENSURE(success,"Changing application permissions should succeed");
	allowed=store.listApplicationsVOMayUseOnCluster(vo2.id,cluster1.id);
	ENSURE_EQUAL(allowed.size(),1,"Specific permissions should replace universal permissions");
	ENSURE_EQUAL(allowed.count(testAppName),1,"Specific permissions should replace universal permissions");
	
	success=store.allowVoToUseApplication(vo2.id,cluster1.id,universalAppName);
	ENSURE(success,"Changing application permissions should succeed");
	allowed=store.listApplicationsVOMayUseOnCluster(vo2.id,cluster1.id);
	ENSURE_EQUAL(allowed.size(),1,"Resetting universal permissions should replace specific permissions");
	ENSURE_EQUAL(allowed.count(universalAppName),1,"Resetting universal permissions should replace specific permissions");
}
