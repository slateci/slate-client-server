#include "test.h"

#include <ServerUtilities.h>

// Duplicates on the same group and cluster should fail
TEST(TestDuplicatesOnSameClusterAndGroup) {
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey = tc.getPortalToken();

	const std::string groupName = "test-dup";
	const std::string clusterName = "testcluster1";

	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Physics", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Group creation request should succeed");
	}

	{ //create a cluster
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
		metadata.AddMember("owningOrganization", "Black Mesa", alloc);
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
	struct cleanupHelper {
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

	const std::string application = "test-app";
	const std::string config1="num: 22";
	const std::string config2="thing: stuff";
	{ //install a thing
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
		}
	}

	{ // Try to install another thing
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", config1+"\n"+config2, alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/"+application+"?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,400,"Duplicate app on the same group & cluster should fail");
	}
}

// Duplicates on the same cluster but different groups should succeed
TEST(TestDuplicatesOnSameClusterDiffGroups) {
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey = tc.getPortalToken();

	const std::string groupName1 = "test-dup1";
	const std::string groupName2 = "test-dup2";
	const std::string clusterName = "testcluster2";

	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName1, alloc);
		metadata.AddMember("scienceField", "Biology", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"First group creation request should succeed");
	}

	std::string group2ID;
	{ //create another VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName2, alloc);
		metadata.AddMember("scienceField", "Chemistry", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Second group creation request should succeed");
		rapidjson::Document data;
		data.Parse(createResp.body);
		if (data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("id")) {
			group2ID = data["metadata"]["id"].GetString();
		}
	}

	std::string clustID;
	{ //create a cluster
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
		metadata.AddMember("group", groupName1, alloc);
		metadata.AddMember("owningOrganization", "Ocean Research Project", alloc);
		metadata.AddMember("serverAddress", serverAddress, alloc);
		metadata.AddMember("caData", caData, alloc);
		metadata.AddMember("token", token, alloc);
		metadata.AddMember("namespace", kubeNamespace, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200,
					 "Cluster creation request should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document data;
		data.Parse(createResp.body);
		if (data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("id")) {
			clustID = data["metadata"]["id"].GetString();
		}
	}

	{ //give other group access to other cluster
		auto allowResp = httpPut(
			tc.getAPIServerURL() + "/" + currentAPIVersion + "/clusters/" + clustID + "/allowed_groups/" +
			group2ID + "?token=" + adminKey, "");
		ENSURE_EQUAL(allowResp.status, 200,
			     "Second group should be given access to cluster");
	}

	std::string instID;
	std::string instID2;
	struct cleanupHelper {
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

	cleanupHelper cleanup2(tc, instID2, adminKey);

	const std::string application = "test-app";
	const std::string config1 = "num: 22";
	const std::string config2 = "thing: stuff";
	const std::string config3 = "num: 23";
	const std::string config4="thing: ooer";

	{ //install a thing
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName1, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", config1+"\n"+config2, alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/"+application+"?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		if (data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("id")) {
			instID = data["metadata"]["id"].GetString();
		}
	}

	{ //install another thing
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName2, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", config3+"\n"+config4, alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/"+application+"?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Duplicate application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		if (data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("id")) {
			instID2 = data["metadata"]["id"].GetString();
		}
	}
}