#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedFetchInstanceInfo){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing instances with no authentication
	auto infoResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/instances/ABC");
	ENSURE_EQUAL(infoResp.status,403,
				 "Requests to get instance info without authentication should be rejected");
	
	//try listing instances with invalid authentication
	infoResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/instances/ABC?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(infoResp.status,403,
				 "Requests to get instance info with invalid authentication should be rejected");
}

TEST(FetchInstanceInfo){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto schema=loadSchema(getSchemaDir()+"/InstanceInfoResultSchema.json");
	
	const std::string voName="test-fetch-inst-info";
	const std::string clusterName="testcluster";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	{ //create a cluster
		auto kubeConfig = tc.getKubeConfig();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("kubeconfig", kubeConfig, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200,
					 "Cluster creation request should succeed");
		ENSURE(!createResp.body.empty());
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
	
	const std::string application="test-app";
	const std::string config1="num: 22";
	const std::string config2="thing: \"stuff\"";
	{ //install a thing
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", voName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", config1+"\n"+config2, alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/"+application+"?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		if(data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("id"))
			instID=data["metadata"]["id"].GetString();
	}
	
	{ //get the thing's information
		auto infoResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/instances/"+instID+"?token="+adminKey);
		rapidjson::Document data;
		data.Parse(infoResp.body);
		ENSURE_CONFORMS(data,schema);
		const auto& metadata=data["metadata"];
		ENSURE_EQUAL(metadata["id"].GetString(),instID,"Instance ID should match");
		ENSURE_EQUAL(metadata["application"].GetString(),application,"Instance application should match");
		ENSURE_EQUAL(metadata["vo"].GetString(),voName,"Instance application should match");
		ENSURE_EQUAL(metadata["cluster"].GetString(),clusterName,"Instance cluster should match");
		std::cout << "Config: " << (metadata["configuration"].GetString()) << std::endl;
		ENSURE(std::string(metadata["configuration"].GetString()).find(config1)!=std::string::npos,
		       "Configuration should contain input data");
		ENSURE(std::string(metadata["configuration"].GetString()).find(config2)!=std::string::npos,
		       "Configuration should contain input data");
		ENSURE_EQUAL(data["services"].Size(),1,"Instance should have one service");
	}
}

TEST(UnrelatedUserInstanceInfo){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	
	const std::string voName="test-unreluser-fetch-inst-info";
	const std::string clusterName="testcluster";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	{ //create a cluster
		auto kubeConfig = tc.getKubeConfig();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("kubeconfig", kubeConfig, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200,
					 "Cluster creation request should succeed");
		ENSURE(!createResp.body.empty());
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
	
	const std::string application="test-app";
	{ //install a thing
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", voName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/"+application+"?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		if(data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("id"))
			instID=data["metadata"]["id"].GetString();
	}
	
	std::string tok;
	{ //create an unrelated user
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
	
	{ //have the new user attempt to get get the thing's information
		auto infoResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/instances/"+instID+"?token="+tok);
		ENSURE_EQUAL(infoResp.status,403,
		             "Requests for instance info from users who do not belong to"
		             " the owning VO should be rejected.");
	}
}