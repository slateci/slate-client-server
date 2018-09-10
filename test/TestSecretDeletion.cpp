#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedDeleteSecret){
	using namespace httpRequests;
	TestContext tc;

	//try creating a secret with no authentication
	auto delResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/secrets/Secret_xyz");
	ENSURE_EQUAL(delResp.status,403,
	             "Requests to delete secrets without authentication should be rejected");

	//try creating a secret with invalid authentication
	delResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/secrets/Secret_xyz?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(delResp.status,403,
	             "Requests to delete secrets with invalid authentication should be rejected");
}

TEST(DeleteSecret){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/v1alpha1/secrets?token="+adminKey;

	//create a VO
	const std::string voName="test-delete-secret-vo";
	{
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,
		                     to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
	}

	const std::string clusterName="testcluster";
	{ //add a cluster
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("kubeconfig", getKubeConfig(), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
	}
	
	const std::string secretName="deletesecret-secret1";
	std::string secretID;
	{ //install a secret
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", secretName, alloc);
		metadata.AddMember("vo", voName, alloc);
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
		auto delResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/secrets/"+secretID+"?token="+adminKey);
		ENSURE_EQUAL(delResp.status,200,"Secret deletion should succeed");
	}
}

TEST(DeleteSecretMalformedRequests){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();

	{ //attempt to delete a secret which does not exist
		auto delResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/secrets/not-a-valid-secret?token="+adminKey);
		ENSURE_EQUAL(delResp.status,404,"Requests to delete non-existent secrets should be rejected");
	}
	
	//create a VO
	const std::string voName="test-delete-secret-malformed-vo";
	{
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,
		                     to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
	}

	const std::string clusterName="testcluster";
	{ //add a cluster
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("kubeconfig", getKubeConfig(), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey, 
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
				auto delResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/secrets/"+id+"?token="+key);
		}
	} cleanup(tc,secretID,adminKey);
	
	{ //install a secret
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", secretName, alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/secrets?token="+adminKey, to_string(request));
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
		uid=createData["metadata"]["id"].GetString();
		otherToken=createData["metadata"]["access_token"].GetString();
	}
	
	{ //attempt to delete the secret as the unrelated user
		auto delResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/secrets/"+secretID+"?token="+otherToken);
		ENSURE_EQUAL(delResp.status,403,"Requests to delete secrets by non-members of the owning VO should be rejected");
	}
}