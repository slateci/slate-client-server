#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedApplicationInstall){
	using namespace httpRequests;
	TestContext tc;
	
	//try installing an application with no authentication
	auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test","");
	ENSURE_EQUAL(instResp.status,403,
				 "Requests to fetch application config without authentication should be rejected");
	
	//try installing an application with invalid authentication
	instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token=00112233-4455-6677-8899-aabbccddeeff","");
	ENSURE_EQUAL(instResp.status,403,
				 "Requests to fetch application config with invalid authentication should be rejected");
}

TEST(ApplicationInstallDefaultConfig){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto schema=loadSchema(getSchemaDir()+"/AppInstallResultSchema.json");
	
	std::string voName="test-app-install-def-con";
	std::string clusterName="testcluster";
	
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
		auto kubeConfig = getKubeConfig();
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
	
	{ //install
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", voName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		ENSURE_CONFORMS(data,schema);
		instID=data["metadata"]["id"].GetString();
	}
}

TEST(ApplicationInstallWithConfig){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto schema=loadSchema(getSchemaDir()+"/AppInstallResultSchema.json");
	
	std::string voName="test-app-install-with-con";
	std::string clusterName="testcluster";
	
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
		auto kubeConfig = getKubeConfig();
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
	
	std::string config;
	{
		auto confResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey);
		ENSURE_EQUAL(confResp.status,200,"Fetching application configuration should succeed");
		rapidjson::Document data;
		data.Parse(confResp.body);
		config=data["spec"]["body"].GetString();
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
	
	{ //install
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", voName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		ENSURE_CONFORMS(data,schema);
		instID=data["metadata"]["id"].GetString();
	}
}

TEST(ApplicationInstallByNonowningVO){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto schema=loadSchema(getSchemaDir()+"/AppInstallResultSchema.json");
	
	std::string voName="test-app-install-nonown-vo";
	std::string clusterName="testcluster";
	std::string guestVOName="test-app-install-guest-vo";
	
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
		auto kubeConfig = getKubeConfig();
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
	
	{ //create a second VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", guestVOName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
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
	
	{ //attempt to install without access being granted
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", guestVOName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		rapidjson::Document data;
		data.Parse(instResp.body);
		if(data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("id"))
			instID=data["metadata"]["id"].GetString();
		ENSURE_EQUAL(instResp.status,403,
		             "Application install request for unauthorized VO should be rejected");
	}
	
	{ //grant the guest VO access
		auto accessResp=httpPut(tc.getAPIServerURL()+"/v1alpha1/clusters/"+clusterName+
								"/allowed_vos/"+guestVOName+"?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "VO access grant request should succeed: "+accessResp.body);
	}
	
	{ //install again
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", guestVOName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		rapidjson::Document data;
		data.Parse(instResp.body);
		if(data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("id"))
			instID=data["metadata"]["id"].GetString();
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed: "+instResp.body);
		ENSURE_CONFORMS(data,schema);
	}
}

TEST(ApplicationInstallMalformedRequests){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminID=getPortalUserID();
	std::string adminKey=getPortalToken();
	auto schema=loadSchema(getSchemaDir()+"/AppInstallResultSchema.json");
	
	std::string voName="test-app-install-mal-req";
	std::string voName2="test-app-install-mal-req2";
	std::string clusterName="testcluster2";
	
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
	
	{ //create another VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName2, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
		
		//and remove self from this VO
		auto remResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/users/"+adminID+"/vos/"+voName2+"?token="+adminKey);
		ENSURE_EQUAL(remResp.status,200,"User removal from VO request should succeed");
	}
	
	{ //create a cluster
		auto kubeConfig = getKubeConfig();
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
	
	{ //attempt without a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request without a VO should be rejected");
	}
	
	{ //attempt without a cluster
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", voName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request without a cluster should be rejected");
	}
	
	{ //attempt without a configuration
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", voName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request without a tag should be rejected");
	}
	
	{ //attempt with wrong type for VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", 72, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request with wrong type for VO should be rejected");
	}
	
	{ //attempt with wrong type for cluster
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", voName, alloc);
		request.AddMember("cluster", 86, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request with wrong type for cluster should be rejected");
	}
	
	{ //attempt with wrong type for configuration
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", voName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", 0, alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request with wrong type for configuration should be rejected");
	}
	
	{ //attempt with invalid VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", "not-a-real-vo", alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request with invalid VO should be rejected");
	}
	
	{ //attempt with VO of which user is not a member
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", voName2, alloc);
		request.AddMember("cluster", "not-a-real-cluster", alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request with VO to which user does not belong should be rejected");
	}
	
	{ //attempt with overlong tag
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", voName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("configuration", "Instance: 012345678901234567890123456789", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request with overly long tag should be rejected");
	}
	
	{ //attempt with punctuation in tag
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", voName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("configuration", "Instance: ~!@#$%^&*()_+={}|[]", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request with punctuation in tag should be rejected");
	}
	
	{ //attempt with trailing dash in tag
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", voName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("configuration", "Instance: trailing-dash-", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request with trailing dash in tag should be rejected");
	}
}

TEST(YAMLReduction){
	{
		const std::string input=R"(foo: "bar")";
		const std::string& expected=input;
		std::string result=reduceYAML(input);
		ENSURE_EQUAL(result,expected);
	}
	{
		const std::string input=R"(foo: "bar"
baz: "quux")";
		const std::string& expected=input;
		std::string result=reduceYAML(input);
		ENSURE_EQUAL(result,expected);
	}
	{
		const std::string input=R"(stuff:
  thing: "majig")";
		const std::string& expected=input;
		std::string result=reduceYAML(input);
		ENSURE_EQUAL(result,expected);
	}
	{ //comments at beginning, middle, and end
		const std::string input=R"(# initial comment
stuff: # settings for the stuff
  thing: "majig" #new thing value)";
		const std::string expected=R"(stuff: 
  thing: "majig" )";
		std::string result=reduceYAML(input);
		ENSURE_EQUAL(result,expected);
	}
	{ //whitespace at beginning, middle and end
		const std::string input=R"(
foo: "bar"
	 
baz: "quux"
)";
		const std::string expected=R"(foo: "bar"
baz: "quux")";
		std::string result=reduceYAML(input);
		ENSURE_EQUAL(result,expected);
	}
	{ //mixed comments and whitespace
		const std::string input=R"(# comment
    
# comment
# comment
# comment
    
    


# comment
  # comment
  # comment)";
		const std::string expected="";
		std::string result=reduceYAML(input);
		ENSURE_EQUAL(result,expected);
	}
}