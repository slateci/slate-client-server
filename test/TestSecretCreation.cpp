#include "test.h"

#include <Archive.h>
#include <FileHandle.h>
#include <KubeInterface.h>
#include <Logging.h>
#include <ServerUtilities.h>

TEST(UnauthenticatedCreateSecret){
	using namespace httpRequests;
	TestContext tc;

	//try creating a secret with no authentication
	auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets","");
	ENSURE_EQUAL(createResp.status,403,
	            "Requests to create secrets without authentication should be rejected");

	//try creating a secret with invalid authentication
	createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets?token=00112233-4455-6677-8899-aabbccddeeff","");
	ENSURE_EQUAL(createResp.status,403,
	            "Requests to create secrets with invalid authentication should be rejected");
}

TEST(CreateSecret){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/SecretCreateResultSchema.json");
	
	//create a VO
	const std::string voName="test-create-secret-vo";
	{
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,
		                     to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
	}

	const std::string clusterName="testcluster";
	{ //add a cluster
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("organization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", tc.getKubeConfig(), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
	}
	
	const std::string secretName="createsecret-secret1";
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
}

TEST(CopySecret){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/SecretCreateResultSchema.json");
	
	const std::map<std::string,std::string> secretData={{"foo","bar"},{"baz","quux"},{"xen","hom"}};
	
	//create a VO
	const std::string voName="test-copy-secret-vo";
	{
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,
		                     to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
	}

	const std::string clusterName="testcluster";
	{ //add a cluster
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("organization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", tc.getKubeConfig(), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
	}
	
	const std::string secretName1="copysecret-secret1";
	const std::string secretName2="copysecret-secret2";
	std::string secretID1;
	std::string secretID2;
	struct cleanupHelper{
		TestContext& tc;
		const std::string& id, key;
		cleanupHelper(TestContext& tc, const std::string& id, const std::string& key):
		tc(tc),id(id),key(key){}
		~cleanupHelper(){
			if(!id.empty())
				auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets/"+id+"?token="+key);
		}
	} cleanup1(tc,secretID1,adminKey), cleanup2(tc,secretID2,adminKey);
	
	{ //install the original secret
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", secretName1, alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		for(const auto& entry : secretData){
			rapidjson::Value key;
			key.SetString(entry.first.c_str(), entry.first.size(), alloc);
			contents.AddMember(key, rapidjson::StringRef(entry.second), alloc);
		}
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Secret creation should succeed: "+createResp.body);
		rapidjson::Document data;
		data.Parse(createResp.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/SecretCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		secretID1=data["metadata"]["id"].GetString();
	}
	
	{ //Attempt to copy the secret
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", secretName2, alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		request.AddMember("copyFrom", secretID1, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Secret copy should succeed: "+createResp.body);
		rapidjson::Document data;
		data.Parse(createResp.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/SecretCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		secretID2=data["metadata"]["id"].GetString();
	}
	
	{ //check that the copy contains the correct data
		auto getResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets/"+secretID2+"?token="+adminKey);
		ENSURE_EQUAL(getResp.status,200,"Getting secret should succeed");
		ENSURE(!getResp.body.empty());
		rapidjson::Document data;
		data.Parse(getResp.body.c_str());
		ENSURE_EQUAL(data["contents"].MemberCount(),secretData.size(),"Copied secret should contain the correct number of keys");
		for(const auto& entry : secretData){
			ENSURE(data["contents"][entry.first].IsString(),"Each secret key should map to a string");
			ENSURE_EQUAL(data["contents"][entry.first].GetString(),entry.second,"Copied secret should contain the same values as the original");
		}
	}
}

TEST(CreateSecretMalformedRequests){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/SecretCreateResultSchema.json");
	
	std::vector<std::string> secretIDs;
	struct cleanupHelper{
		TestContext& tc;
		const std::vector<std::string>& ids;
		const std::string&key;
		cleanupHelper(TestContext& tc, const std::vector<std::string>& ids, const std::string& key):
		tc(tc),ids(ids),key(key){}
		~cleanupHelper(){
			for(const auto& id : ids)
				auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets/"+id+"?token="+key);
		}
	} cleanup(tc,secretIDs,adminKey);
	
	//create a VO
	const std::string voName="test-create-secret-malformed-vo";
	{
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,
		                     to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
	}
	
	const std::string clusterName="testcluster";
	{ //add a cluster
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("organization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", tc.getKubeConfig(), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
	}
	
	{ //attempt without metadata
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,400,"Secret creation without metadata should be rejected");
	}
	
	{ //attempt with wrong metadata type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("metadata", "a string", alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,400,"Secret creation with wrong metadata type should be rejected");
	}
	
	{ //attempt without name
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,400,"Secret creation without name should be rejected");
	}
	
	{ //attempt with wrong name type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", 17, alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,400,"Secret creation with wrong name type should be rejected");
	}
	
	{ //attempt without vo
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "a-secret", alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,400,"Secret creation without VO should be rejected");
	}
	
	{ //attempt with wrong vo type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "a-secret", alloc);
		metadata.AddMember("vo", 8, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,400,"Secret creation with wrong VO type should be rejected");
	}
	
	{ //attempt without cluster
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "a-secret", alloc);
		metadata.AddMember("vo", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,400,"Secret creation without cluster should be rejected");
	}
	
	{ //attempt with wrong cluster type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "a-secret", alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("cluster", 22, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,400,"Secret creation with wrong cluster type should be rejected");
	}
	
	{ //attempt without contents
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "a-secret", alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,400,"Secret creation without contents should be rejected");
	}
	
	{ //attempt with wrong contents type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "a-secret", alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		request.AddMember("contents", "not an object", alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,400,"Secret creation with wrong contents type should be rejected");
	}
	
	{ //attempt with wrong contents entry type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "a-secret", alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", 6, alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,400,"Secret creation with wrong contents entry type should be rejected");
	}
	
	{ //attempt with too long name
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", std::string(254,'a'), alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,400,"Secret creation with over-long name should be rejected");
	}
	
	{ //attempt with name with forbidden characters
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "#=Illegal_Name()", alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,400,"Secret creation with name with forbidden characters should be rejected");
	}
	
	{ //attempt with non-existent VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "a-secret", alloc);
		metadata.AddMember("vo", "not-a-valid-vo", alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,404,"Secret creation with non-existent target VO should be rejected");
	}
	
	{ //attempt with non-existent cluster
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "a-secret", alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("cluster", "not-a-cluster", alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,404,"Secret creation with non-existent target cluster should be rejected");
	}
	
	{ //attempt with contents and copyFrom
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "a-secret", alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		request.AddMember("copyFrom", "Secret_abc", alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,400,"Secret creation with both contents data and a copy source specified should be rejected");
	}
	
	{ //attempt with copyFrom which refers to a secret which does not exist
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "a-secret", alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		request.AddMember("copyFrom", "Secret_abc", alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,404,"Secret creation with a copy source which does not exist should be rejected");
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
	
	{ //attempt to install from non-member of VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "a-secret", alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets?token="+otherToken, to_string(request));
		ENSURE_EQUAL(createResp.status,403,"Secret creation by non-member of target VO should be rejected");
	}
	
	//create a VO not authorized to use the cluster
	const std::string unauthVOName="test-create-secrets--malformed-vo2";
	{
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", unauthVOName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+otherToken,
		                     to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed"+voResp.body);
	}
	
	{ //attempt to install from non-member of VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "a-secret", alloc);
		metadata.AddMember("vo", unauthVOName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,403,"Secret creation by unauthorized VO should be rejected");
	}
	
	//install a secret correctly, then try to install a duplicate
	for(unsigned int i=0; i<2; i++){
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "the-secret", alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		std::string encodedValue=encodeBase64("bar");
		contents.AddMember("foo", encodedValue, alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		if(i)
			ENSURE_EQUAL(createResp.status,400,"Duplicate secret creation should be rejected");
		else
			ENSURE_EQUAL(createResp.status,200,"First secret creation should succeed");
		if(createResp.status==200){
			rapidjson::Document data;
			data.Parse(createResp.body.c_str());
			secretIDs.push_back(data["metadata"]["id"].GetString());
		}
	}
	
	{ //grant the other VO access to the cluster, and have it try to copy the existing secret
		//grant the second VO access to the cluster
		auto accessResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterName+
								"/allowed_vos/"+unauthVOName+"?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "VO access grant request should succeed: "+accessResp.body);
		
		//try to copy the first VO's secret
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "another-secret", alloc);
		metadata.AddMember("vo", unauthVOName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		request.AddMember("copyFrom", secretIDs.front(), alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets?token="+otherToken, to_string(request));
		ENSURE_EQUAL(createResp.status,403,"Copying a secret from another VO to which the requester does not belong should be rejected");
	}
}

TEST(BinarySecretData){
	using namespace httpRequests;
	TestContext tc;
	
	const std::string secretKey="key";
	const std::string secretData="\x00\x01\x02\x04\x10\x20\x40\xFF";
	
	std::string adminKey=getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/SecretCreateResultSchema.json");
	
	//create a VO
	const std::string voName="test-create-secret-vo";
	{
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,
		                     to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
	}

	const std::string clusterName="testcluster";
	{ //add a cluster
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("organization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", tc.getKubeConfig(), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
	}
	
	const std::string secretName="binary-secret";
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
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		rapidjson::Value key;
		key.SetString(secretKey.c_str(), secretKey.length(), alloc);
		std::string encodedValue=encodeBase64(secretData);
		contents.AddMember(key, encodedValue, alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Secret creation should succeed: "+createResp.body);
		rapidjson::Document data;
		data.Parse(createResp.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/SecretCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		secretID=data["metadata"]["id"].GetString();
	}
	
	//access the created secret directly to ensure that it contains the correct data
	auto tempPath=makeTemporaryFile("kubeconfig_");
	{
		std::ofstream outFile(tempPath.path());
		if(!outFile)
			log_fatal("Failed to open " << tempPath.path() << " for writing");
		std::string kubeconfig=tc.getKubeConfig();
		outFile.write(kubeconfig.c_str(),kubeconfig.size());
	}
	startReaper();
	auto result=kubernetes::kubectl(tempPath,{"get","secret",secretName,
	                                          "-n","slate-vo-"+voName,
	                                          "-o=jsonpath={.data."+secretKey+"}"});
	stopReaper();
	ENSURE_EQUAL(result.status,0,"Should be able to read secret with kubectl");
	std::string retrievedSecret=decodeBase64(result.output);
	ENSURE_EQUAL(secretData,retrievedSecret,
	  "Data returned form the cluster should match the original secret value");
}

TEST(ShellEscaping){
	{ //no quotes
		std::string input="The quick brown fox jumped over the lazy dog.";
		const std::string& expected=input;
		std::string result=shellEscapeSingleQuotes(input);
		ENSURE_EQUAL(result,expected);
	}
	
	{ //single quote in middle
		std::string input="Shan't!";
		std::string expected=R"(Shan'\''t!)";
		std::string result=shellEscapeSingleQuotes(input);
		ENSURE_EQUAL(result,expected);
	}
	
	{ //single quote at beginning
		std::string input="’Tis the wind and nothing more!";
		std::string expected=R"(\'’Tis the wind and nothing more!)";
	}
	
	{ //single quote at end
		std::string input="Having forgotten his lunch, he took the lawyers'";
		std::string expected=R"(Having forgotten his lunch, he took the lawyers'\')";
		std::string result=shellEscapeSingleQuotes(input);
		ENSURE_EQUAL(result,expected);
	}
	
	{ //quotes at beginning and end
		std::string input="'quoted'";
		std::string expected=R"(\''quoted'\')";
	}
	
	{ //complicated string which previously exhibited a bug
		std::string input=R"(If you do want to specify resources, uncomment the following
  # lines, adjust them as necessary, and remove the curly braces after 'resources:'.
  limits:
    cpu: 1000m
    memory: 512Mi
)";
		std::string expected=R"(If you do want to specify resources, uncomment the following
  # lines, adjust them as necessary, and remove the curly braces after '\''resources:'\''.
  limits:
    cpu: 1000m
    memory: 512Mi
)";
		std::string result=shellEscapeSingleQuotes(input);
		ENSURE_EQUAL(result,expected);
	}
}
