#include "test.h"

#include <Archive.h>
#include <ServerUtilities.h>

TEST(UnauthenticatedFetchSecret){
	using namespace httpRequests;
	TestContext tc;

	//try fetching a secret with no authentication
	auto getResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets/Secret_xyz");
	ENSURE_EQUAL(getResp.status,403,
	             "Requests to fetch secrets without authentication should be rejected");

	//try listing secrets with invalid authentication
	getResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets/Secret_xyz?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(getResp.status,403,
	            "Requests to fetch secrets with invalid authentication should be rejected");
}

TEST(FetchSecret){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/SecretInfoResultSchema.json");
	
	//create a VO
	const std::string groupName="test-fetch-secret-group";
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
	
	const std::string secretName="fetchsecret-secret1";
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
	
	const std::string secretKey="foo";
	const std::string secretValue=R"(The sun was shining on the sea,
Shining with all his might:
He did his very best to make
The billows smooth and bright —
And this was odd, because it was
The middle of the night.)";
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
		auto encodedValue=encodeBase64(secretValue);
		contents.AddMember("foo", encodedValue, alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Secret creation should succeed: "+createResp.body);
		rapidjson::Document data;
		data.Parse(createResp.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/SecretCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		secretID=data["metadata"]["id"].GetString();
	}
	
	{ //get the secret
		auto getResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets/"+secretID+"?token="+adminKey);
		ENSURE_EQUAL(getResp.status,200,"Getting secret should succeed");
		ENSURE(!getResp.body.empty());
		rapidjson::Document data;
		data.Parse(getResp.body.c_str());
		ENSURE_CONFORMS(data,schema);
		ENSURE(data["contents"].HasMember(secretKey),"Returned secret should contain the right key");
		ENSURE(data["contents"][secretKey].IsString(),"Returned secret key should map to a string");
		ENSURE_EQUAL(decodeBase64(data["contents"][secretKey].GetString()),secretValue,"Returned secret should contain the right value");
		std::cout << "Got secret: " << data["contents"][secretKey].GetString() << std::endl;
	}
}

TEST(FetchSecretMalformed){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	
	{ //attempt to get a secret which does not exist
		auto getResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets/not-a-valid-secret?token="+adminKey);
		ENSURE_EQUAL(getResp.status,404,"Requests to fetch non-existent secrets should be rejected");
	}
	
	//create a VO
	const std::string groupName="test-fetch-secret-malformed-group";
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

	const std::string secretName="fetchsecretmalformed-secret1";
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
		auto encodedValue=encodeBase64("bar");
		contents.AddMember("foo", encodedValue, alloc);
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
	
	{ //attempt to fetch the secret as the unrelated user
		auto getResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets/"+secretID+"?token="+otherToken);
		ENSURE_EQUAL(getResp.status,403,"Requests to fetch secrets by non-members of the owning Group should be rejected");
	}
}
