#include "test.h"

#include <Archive.h>
#include <ServerUtilities.h>

TEST(AdHocInstallForbiddenByDefault){
	using namespace httpRequests;
	TestContext tc;
	
	auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test","");
	ENSURE_EQUAL(instResp.status,400,
				 "By default the server should reject ad-hoc application install requests");
}

TEST(UnauthenticatedApplicationInstall){
	using namespace httpRequests;
	TestContext tc({"--allowAdHocApps=1"});
	
	//try installing an application with no authentication
	auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test","");
	ENSURE_EQUAL(instResp.status,403,
				 "Requests to fetch application config without authentication should be rejected");
	
	//try installing an application with invalid authentication
	instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token=00112233-4455-6677-8899-aabbccddeeff","");
	ENSURE_EQUAL(instResp.status,403,
				 "Requests to fetch application config with invalid authentication should be rejected");
}

std::string getTestAppChart(){
	std::string chartPath;
	ENSURE(fetchFromEnvironment("TEST_SRC",chartPath),"The TEST_SRC environment variable must be set");
	chartPath+="/test_helm_repo/test-app";
	std::stringstream tarBuffer,gzipBuffer;
	TarWriter tw(tarBuffer);
	std::string dirPath=chartPath;
	while(dirPath.size()>1 && dirPath.back()=='/') //strip trailing slashes
		dirPath=dirPath.substr(0,dirPath.size()-1);
	recursivelyArchive(dirPath,tw,true);
	tw.endStream();
	gzipCompress(tarBuffer,gzipBuffer);
	std::string encodedChart=encodeBase64(gzipBuffer.str());
	return encodedChart;
}

TEST(ApplicationInstallDefaultConfig){
	using namespace httpRequests;
	TestContext tc({"--allowAdHocApps=1"});
	
	std::string adminKey=tc.getPortalToken();
	auto schema=loadSchema(getSchemaDir()+"/AppInstallResultSchema.json");
	
	std::string groupName="test-ad-hoc-app-install-def-con";
	std::string clusterName="testcluster";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Group creation request should succeed");
	}
	
	{ //create a cluster
		auto kubeConfig = tc.getKubeConfig();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", kubeConfig, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, to_string(request));
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
				auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances/"+id+"?token="+key);
		}
	} cleanup(tc,instID,adminKey);
	
	{ //install
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("chart",getTestAppChart(),alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		ENSURE_CONFORMS(data,schema);
		instID=data["metadata"]["id"].GetString();
	}
}

TEST(ApplicationInstallWithConfig){
	using namespace httpRequests;
	TestContext tc({"--allowAdHocApps=1"});
	
	std::string adminKey=tc.getPortalToken();
	auto schema=loadSchema(getSchemaDir()+"/AppInstallResultSchema.json");
	
	std::string groupName="test-ad-hoc-app-install-with-con";
	std::string clusterName="testcluster";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Group creation request should succeed");
	}
	
	{ //create a cluster
		auto kubeConfig = tc.getKubeConfig();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", kubeConfig, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, to_string(request));
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
				auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances/"+id+"?token="+key);
		}
	} cleanup(tc,instID,adminKey);
	
	{ //install
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("chart",getTestAppChart(),alloc);
		request.AddMember("configuration", "Instance: test", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		ENSURE_CONFORMS(data,schema);
		instID=data["metadata"]["id"].GetString();
	}
}

TEST(ApplicationInstallByNonowningGroup){
	using namespace httpRequests;
	TestContext tc({"--allowAdHocApps=1"});
	
	std::string adminKey=tc.getPortalToken();
	auto schema=loadSchema(getSchemaDir()+"/AppInstallResultSchema.json");
	
	const std::string groupName="test-ad-hoc-app-install-nonown-group";
	const std::string clusterName="testcluster";
	const std::string guestGroupName="test-app-install-guest-group";
	const std::string chart=getTestAppChart();
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Group creation request should succeed");
	}
	
	{ //create a cluster
		auto kubeConfig = tc.getKubeConfig();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", kubeConfig, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200,
					 "Cluster creation request should succeed");
		ENSURE(!createResp.body.empty());
	}
	
	{ //create a second VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", guestGroupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Group creation request should succeed");
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
	
	{ //attempt to install without access being granted
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", guestGroupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("chart",chart,alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		rapidjson::Document data;
		data.Parse(instResp.body);
		if(data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("id"))
			instID=data["metadata"]["id"].GetString();
		ENSURE_EQUAL(instResp.status,403,
		             "Application install request for unauthorized Group should be rejected");
	}
	
	{ //grant the guest Group access
		auto accessResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterName+
								"/allowed_groups/"+guestGroupName+"?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "Group access grant request should succeed: "+accessResp.body);
	}
	
	{ //install again
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", guestGroupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("chart",chart,alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
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
	TestContext tc({"--allowAdHocApps=1"});
	
	std::string adminID=tc.getPortalUserID();
	std::string adminKey=tc.getPortalToken();
	auto schema=loadSchema(getSchemaDir()+"/AppInstallResultSchema.json");
	
	const std::string groupName="test-ad-hoc-app-install-mal-req";
	const std::string groupName2="test-app-install-mal-req2";
	const std::string clusterName="testcluster2";
	const std::string chart=getTestAppChart();
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Group creation request should succeed");
	}
	
	{ //create another VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName2, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Group creation request should succeed");
		
		//and remove self from this VO
		auto remResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+adminID+"/groups/"+groupName2+"?token="+adminKey);
		ENSURE_EQUAL(remResp.status,200,"User removal from Group request should succeed");
	}
	
	{ //create a cluster
		auto kubeConfig = tc.getKubeConfig();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", kubeConfig, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200,
					 "Cluster creation request should succeed");
		ENSURE(!createResp.body.empty());
	}
	
	{ //attempt without a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("chart",chart,alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request without a Group should be rejected");
	}
	
	{ //attempt without a cluster
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("chart",chart,alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request without a cluster should be rejected");
	}
	
	{ //attempt without a chart
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration","",alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request without a chart should be rejected");
	}
	
	{ //attempt without a configuration
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("chart",chart,alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request without a tag should be rejected");
	}
	
	{ //attempt with wrong type for VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", 72, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("chart",chart,alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request with wrong type for Group should be rejected");
	}
	
	{ //attempt with wrong type for cluster
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", 86, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("chart",chart,alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request with wrong type for cluster should be rejected");
	}
	
	{ //attempt with wrong type for chart
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("chart",45,alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request with wrong type for chart should be rejected");
	}
	
	{ //attempt with wrong type for configuration
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("chart",chart,alloc);
		request.AddMember("configuration", 0, alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request with wrong type for configuration should be rejected");
	}
	
	{ //attempt with invalid VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", "not-a-real-group", alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("chart",chart,alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request with invalid Group should be rejected");
	}
	
	{ //attempt with Group of which user is not a member
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName2, alloc);
		request.AddMember("cluster", "not-a-real-cluster", alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("chart",chart,alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request with Group to which user does not belong should be rejected");
	}
	
	{ //attempt with overlong tag
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("chart",chart,alloc);
		request.AddMember("configuration", "Instance: !!str 012345678901234567890123456789012345678901234567890123456789", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request with overly long tag should be rejected");
	}
	
	{ //attempt with punctuation in tag
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("chart",chart,alloc);
		request.AddMember("configuration", "Instance: ~!@#$%^&*()_+={}|[]", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request with punctuation in tag should be rejected");
	}
	
	{ //attempt with trailing dash in tag
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("chart",chart,alloc);
		request.AddMember("configuration", "Instance: trailing-dash-", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Application install request with trailing dash in tag should be rejected");
	}
}

TEST(BadChartTarballs){
	using namespace httpRequests;
	TestContext tc({"--allowAdHocApps=1"});
	
	std::string adminKey=tc.getPortalToken();
	auto schema=loadSchema(getSchemaDir()+"/AppInstallResultSchema.json");
	
	std::string groupName="test-ad-hoc-app-install-bad-chart";
	std::string clusterName="testcluster";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Group creation request should succeed");
	}
	
	{ //create a cluster
		auto kubeConfig = tc.getKubeConfig();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", kubeConfig, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200,
					 "Cluster creation request should succeed");
		ENSURE(!createResp.body.empty());
	}
	
	const std::string simpleChart=R"(
apiVersion: "v1"
name: "broken-app"
version: 0.0.0
description: "A malformed application"
)";
	const std::string simpleValues="Instance: default";
	const std::string simpleTemplate=R"(
apiVersion: v1
kind: ConfigMap
metadata:
  name: config
  labels:
    instance: {{ .Values.Instance }}
data:
  stuff: ABC
)";
	
	{ //attempt with a tarball which does not have a Chart.yaml
		std::stringstream tarBuffer,gzipBuffer;
		TarWriter tw(tarBuffer);
		tw.appendDirectory("broken-app");
		tw.appendFile("broken-app/values.yaml",simpleValues);
		tw.appendDirectory("broken-app/templates");
		tw.appendFile("broken-app/templates/configmap.yaml",simpleTemplate);
		tw.endStream();
		gzipCompress(tarBuffer,gzipBuffer);
		std::string chart=encodeBase64(gzipBuffer.str());
		
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("chart",chart,alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,
		             "Application install request with malformed chart (missing Chart.yaml) should be rejected");
	}
	
	{ //attempt with a tarball which does not have a values.yaml
		std::stringstream tarBuffer,gzipBuffer;
		TarWriter tw(tarBuffer);
		tw.appendDirectory("broken-app");
		tw.appendFile("broken-app/Chart.yaml",simpleChart);
		tw.appendDirectory("broken-app/templates");
		tw.appendFile("broken-app/templates/configmap.yaml",simpleTemplate);
		tw.endStream();
		gzipCompress(tarBuffer,gzipBuffer);
		std::string chart=encodeBase64(gzipBuffer.str());
		
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("chart",chart,alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,500,
		             "Application install request with malformed chart (missing values.yaml) should be rejected");
	}
	
	{ //attempt with a tarball which does not have a templates subdirectory
		std::stringstream tarBuffer,gzipBuffer;
		TarWriter tw(tarBuffer);
		tw.appendDirectory("broken-app");
		tw.appendFile("broken-app/Chart.yaml",simpleChart);
		tw.appendFile("broken-app/values.yaml",simpleValues);
		tw.endStream();
		gzipCompress(tarBuffer,gzipBuffer);
		std::string chart=encodeBase64(gzipBuffer.str());
		
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("chart",chart,alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,500,
		             "Application install request with malformed chart (missing templates) should be rejected");
	}
	
	{ //attempt with an evil tarball which tries to write a file to the test source directory (which is assumed to be writeable)
		std::string testSrcDir;
		ENSURE(fetchFromEnvironment("TEST_SRC",testSrcDir),"The TEST_SRC environment variable must be set");
	
		std::stringstream tarBuffer,gzipBuffer;
		TarWriter tw(tarBuffer);
		tw.appendDirectory("evil-chart");
		tw.appendSymLink("evil-chart/dir",testSrcDir);
		tw.appendFile("evil-chart/dir/file","injected");
		tw.endStream();
		gzipCompress(tarBuffer,gzipBuffer);
		std::string chart=encodeBase64(gzipBuffer.str());
		
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("chart",chart,alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,500,
		             "Application install request with malformed chart (file injection) should be rejected");
	}
	
	{ //attempt with an evil tarball which tries to use a symlink to read an external file
		std::string testSrcDir;
		ENSURE(fetchFromEnvironment("TEST_SRC",testSrcDir),"The TEST_SRC environment variable must be set");
		
		std::stringstream tarBuffer,gzipBuffer;
		TarWriter tw(tarBuffer);
		tw.appendDirectory("broken-app");
		tw.appendFile("broken-app/Chart.yaml",simpleChart);
		tw.appendFile("broken-app/values.yaml",simpleValues);
		tw.appendDirectory("broken-app/templates");
		tw.appendSymLink("broken-app/templates/configmap.yaml",testSrcDir+"/TestAdHocApplicationInstall.cpp");
		tw.endStream();
		gzipCompress(tarBuffer,gzipBuffer);
		std::string chart=encodeBase64(gzipBuffer.str());
		
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("chart",chart,alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/ad-hoc?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,500,
		             "Application install request with malformed chart (link to external file) should be rejected");
	}
}
