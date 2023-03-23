#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedInstanceFetchReplicas){
	using namespace httpRequests;
	TestContext tc;

	// Try to get scale without authentication
	auto infoResp = httpGet(tc.getAPIServerURL() + "/" + currentAPIVersion + "/instances/ABC/scale");
	ENSURE_EQUAL(infoResp.status, 403,
		     "Requests to get instance replica info without authentication should be rejected");

	// try getting scale with invalid authentication
	infoResp = httpGet(tc.getAPIServerURL() + "/" + currentAPIVersion +
			   "/instances/ABC/scale?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(infoResp.status, 403,
		     "Requests to get instance replica info with invalid authentication should be rejected");
}

TEST(UnauthenticatedInstanceSetReplicas){
	using namespace httpRequests;
	TestContext tc;

	// Try to set without authentication
	auto infoResp = httpPut(tc.getAPIServerURL() + "/" + currentAPIVersion + "/instances/ABC/scale?replicas=3", "");
	ENSURE_EQUAL(infoResp.status, 403,
		     "Requests to rescale instance without authentication should be rejected");

	// try rescaling with invalid authentication
	infoResp = httpPut(tc.getAPIServerURL() + "/" + currentAPIVersion +
			   "/instances/ABC/scale?token=00112233-4455-6677-8899-aabbccddeeff&replicas=3", "");
	ENSURE_EQUAL(infoResp.status, 403,
		     "Requests to rescale instance with invalid authentication should be rejected");
}

TEST(FetchAndSetInstanceReplicas){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=tc.getPortalToken();
	auto schema=loadSchema(getSchemaDir()+"/InstanceScaleResultSchema.json");

	const std::string groupName="test-fetch-inst-scale";
	const std::string clusterName="test-get-scale-cluster";

	{ // create a VO
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

	{ // create a cluster
		auto kubeConfig = tc.getKubeConfig();
		auto caData = tc.getServerCAData();
		auto token = tc.getUserToken();
		auto kubeNamespace = tc.getKubeNamespace();
		auto serverAddress = tc.getServerAddress();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("serverAddress", serverAddress, alloc);
		metadata.AddMember("caData", caData, alloc);
		metadata.AddMember("token", token, alloc);
		metadata.AddMember("namespace", kubeNamespace, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200,
					 "Cluster creation request should succeed");
		ENSURE(!createResp.body.empty());
	}

	std::string instID;
	std::string instName;
	struct cleanupHelper{
		TestContext& tc;
		const std::string& id, key;
		cleanupHelper(TestContext& tc, const std::string& id, const std::string& key):
		tc(tc),id(id),key(key){}
		~cleanupHelper(){
			if (!id.empty()) {
				auto delResp = httpDelete(
					tc.getAPIServerURL() + "/" + currentAPIVersion + "/instances/" + id +
					"?token=" + key);
			}
		}
	} cleanup(tc,instID,adminKey);

	const std::string application="test-app";
	const std::string config1="num: 2571008";
	const std::string config2="thing: foobar";

	{ // install app
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", config1+"\n"+config2, alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/"+application+"?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		if (data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("id")) {
		    instID = data["metadata"]["id"].GetString();
		} else {
		    FAIL("Installation gave no ID");
		}
		if (data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("name")) {
			instName = data["metadata"]["name"].GetString();
		} else {
			FAIL("Installation gave no deployment name");
		}
	}

	{ // Get replica info
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances/"+instID+"/scale?token="+adminKey);
		ENSURE_EQUAL(infoResp.status,200,"Application get instance scale request should succeed");
		rapidjson::Document data;
		data.Parse(infoResp.body);
		ENSURE_CONFORMS(data,schema);
		if (data["deployments"].HasMember(instName)) {
			ENSURE_EQUAL(data["deployments"][instName].GetInt(), 1,
				     "Replica count should be 1 after installation");
		} else {
			FAIL("Deployment count was not accessable");
		}
    	}

	{ // Rescale replica
		auto infoResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances/"+instID+"/scale?token="+adminKey+"&replicas=3", "");
		ENSURE_EQUAL(infoResp.status,200,"Application change instance scale to 3 should succeed");
		rapidjson::Document data;
		data.Parse(infoResp.body);
		ENSURE_CONFORMS(data,schema);
		if (data["deployments"].HasMember(instName)) {
			ENSURE_EQUAL(data["deployments"][instName].GetInt(), 3,
				     "Replica count should be 3 after rescaling");
		} else {
			FAIL("Deployment count was not accessible");
		}
	}

	{ // Rescale replica again!
		auto infoResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances/"+instID+"/scale?token="+adminKey+"&replicas=2", "");
		ENSURE_EQUAL(infoResp.status,200,"Application change instance scale to 2 should succeed");
		rapidjson::Document data;
		data.Parse(infoResp.body);
		ENSURE_CONFORMS(data,schema);
		if (data["deployments"].HasMember(instName)) {
			ENSURE_EQUAL(data["deployments"][instName].GetInt(), 2,
				     "Replica count should be 2 after rescaling a second time");
		} else {
			FAIL("Deployment count was not accessable");
		}
	}
}

TEST(UnrelatedUserInstanceReplicas){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=tc.getPortalToken();
	auto schema=loadSchema(getSchemaDir()+"/InstanceScaleResultSchema.json");

	const std::string groupName="test-unreluser-fetch-inst-scale";
	const std::string clusterName="test-get-scale-cluster-unreluser";

	{ // create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Biology", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Group creation request should succeed");
	}

	{ // create a cluster
		auto kubeConfig = tc.getKubeConfig();
		auto caData = tc.getServerCAData();
		auto token = tc.getUserToken();
		auto kubeNamespace = tc.getKubeNamespace();
		auto serverAddress = tc.getServerAddress();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("owningOrganization", "Area 51", alloc);
		metadata.AddMember("serverAddress", serverAddress, alloc);
		metadata.AddMember("caData", caData, alloc);
		metadata.AddMember("token", token, alloc);
		metadata.AddMember("namespace", kubeNamespace, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation request should succeed");
		ENSURE(!createResp.body.empty());
	}

	std::string instID;
	struct cleanupHelper{
		TestContext& tc;
		const std::string& id, key;
		cleanupHelper(TestContext& tc, const std::string& id, const std::string& key):
		tc(tc),id(id),key(key){}
		~cleanupHelper(){
			if (!id.empty()) {
				auto delResp = httpDelete(
					tc.getAPIServerURL() + "/" + currentAPIVersion + "/instances/" + id +
					"?token=" + key);
			}
		}
	} cleanup(tc,instID,adminKey);

	const std::string application="test-app";
	const std::string config1="num: 345";
	const std::string config2="thing: barfoo";

	{ // install app
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install2", alloc);
		request.AddMember("configuration", config1+"\n"+config2, alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/"+application+"?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		if (data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("id")) {
			instID = data["metadata"]["id"].GetString();
		}
	}

	std::string tok;
	{ // create an unrelated user
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Jeff", alloc);
		metadata.AddMember("email", "jeff@alienhunters.org", alloc);
		metadata.AddMember("phone", "555-5555", alloc);
		metadata.AddMember("institution", "Earthling University of Xenobiology", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Jeff's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		tok=createData["metadata"]["access_token"].GetString();
	}

	{ // have the new user attempt to get replica count
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances/"+instID+"/scale?token="+tok);
		ENSURE_EQUAL(infoResp.status,403,
		             "Requests for instance replica count from users who do not belong to"
		             " the owning Group should be rejected.");
	}

	{ // have the new user attempt to set replica count
		auto infoResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances/"+instID+"/scale?token="+tok+"&replicas=3", "");
		ENSURE_EQUAL(infoResp.status,403,
		             "Requests to change replica count from users who do not belong to"
		             " the owning Group should be rejected.");
	}
}