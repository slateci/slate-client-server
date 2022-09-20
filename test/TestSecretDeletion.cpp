#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedDeleteSecret){
	using namespace httpRequests;
	TestContext tc;

	//try creating a secret with no authentication
	auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets/Secret_xyz");
	ENSURE_EQUAL(delResp.status,403,
	             "Requests to delete secrets without authentication should be rejected");

	//try creating a secret with invalid authentication
	delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets/Secret_xyz?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(delResp.status,403,
	             "Requests to delete secrets with invalid authentication should be rejected");
}

TEST(DeleteSecret){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets?token="+adminKey;

	//create a VO
	const std::string groupName="test-delete-secret-group";
	{
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
		                     to_string(createGroup));
		ENSURE_EQUAL(groupResp.status,200, "Group creation request should succeed");
	}

	const std::string clusterName="testcluster";
	{ //add a cluster
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
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
	}
	
	const std::string secretName="deletesecret-secret1";
	std::string secretID;
	{ //install a secret
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", secretName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Secret creation should succeed: "+createResp.body);
		rapidjson::Document data;
		data.Parse(createResp.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/SecretCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		secretID=data["metadata"]["id"].GetString();
	}
	
	{ //delete the secret
		auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets/"+secretID+"?token="+adminKey);
		ENSURE_EQUAL(delResp.status,200,"Secret deletion should succeed");
	}
}

TEST(DeleteSecretMalformedRequests){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();

	{ //attempt to delete a secret which does not exist
		auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets/not-a-valid-secret?token="+adminKey);
		ENSURE_EQUAL(delResp.status,404,"Requests to delete non-existent secrets should be rejected");
	}
	
	//create a VO
	const std::string groupName="test-delete-secret-malformed-group";
	{
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
		                     to_string(createGroup));
		ENSURE_EQUAL(groupResp.status,200, "Group creation request should succeed");
	}

	const std::string clusterName="testcluster";
	{ //add a cluster
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
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
	}

	const std::string secretName="deletesecretmalformed-secret1";
	std::string secretID;
	struct cleanupHelper{
		TestContext& tc;
		const std::string& id, key;
		cleanupHelper(TestContext& tc, const std::string& id, const std::string& key):
		tc(tc),id(id),key(key){}
		~cleanupHelper(){
			if(!id.empty())
				auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets/"+id+"?token="+key);
		}
	} cleanup(tc,secretID,adminKey);
	
	{ //install a secret
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", secretName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Secret creation should succeed: "+createResp.body);
		rapidjson::Document data;
		data.Parse(createResp.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/SecretCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		secretID=data["metadata"]["id"].GetString();
	}
	
	std::string uid;
	std::string otherToken;
	{ //create an unrelated user
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
		uid=createData["metadata"]["id"].GetString();
		otherToken=createData["metadata"]["access_token"].GetString();
	}
	
	{ //attempt to delete the secret as the unrelated user
		auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets/"+secretID+"?token="+otherToken);
		ENSURE_EQUAL(delResp.status,403,"Requests to delete secrets by non-members of the owning Group should be rejected");
	}
}
