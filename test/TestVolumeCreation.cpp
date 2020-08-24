#include "test.h"

TEST(UnauthenticatedCreateVolume){
	using namespace httpRequests;
	TestContext tc;

	//try creating a volume claim with no authentication
	auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes","");
	ENSURE_EQUAL(createResp.status,403,
	            "Requests to create volumes without authentication should be rejected");

	//try creating a secret with invalid authentication
	createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token=00112233-4455-6677-8899-aabbccddeeff","");
	ENSURE_EQUAL(createResp.status,403,
	            "Requests to create volumes with invalid authentication should be rejected");
}

TEST(CreateVolume){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/VolumeCreateResultSchema.json");
	
	//create a group
	//register a cluster
	//create a volume claim
	//check that 
	//	- the volume claim request is successful
	//	- the result matches the required schema
	//	- the PVC actually exists on the cluster
}

TEST(CreateVolumeMalformedRequests){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/VolumeCreateResultSchema.json");
	
	//create a group
	//register a cluster
	//attempt to create a volume with no metadata in the request
	//attempt to create a volume with each of the required metadata fields missing
	//attempt to create a volume with each of the required metadata fields haivng the wrong type
}