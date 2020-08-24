#include "test.h"

TEST(UnauthenticatedGetVolumeInfo){
	using namespace httpRequests;
	TestContext tc;

	//try creating a volume claim with no authentication
	auto createResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/some-volume");
	ENSURE_EQUAL(createResp.status,403,
	            "Requests to delete volumes without authentication should be rejected");

	//try creating a secret with invalid authentication
	createResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/some-volume?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(createResp.status,403,
	            "Requests to delete volumes with invalid authentication should be rejected");
}

TEST(GetVolumeInfo){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/VolumeInfoResultSchema.json");
	
	//create a group
	//register a cluster
	//create a volume claim
	//fetch the volume info
	//check that
	//	- the reuslt matches the required schema
	//	- the result data is correct
}

TEST(MalformedGetVolumeInfo){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/VolumeInfoResultSchema.json");
	
	//create a group
	//register a cluster
	//create a volume claim
	//create another user which does not belong to the group
	//ensure that the second user cannot get the volume info
}