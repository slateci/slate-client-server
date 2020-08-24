#include "test.h"

TEST(UnauthenticatedDeleteVolume){
	using namespace httpRequests;
	TestContext tc;

	//try creating a volume claim with no authentication
	auto createResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/some-volume");
	ENSURE_EQUAL(createResp.status,403,
	            "Requests to delete volumes without authentication should be rejected");

	//try creating a secret with invalid authentication
	createResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/some-volume?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(createResp.status,403,
	            "Requests to delete volumes with invalid authentication should be rejected");
}

TEST(DeleteVolume){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	
	//create a group
	//register a cluster
	//create a volume claim
	//check that 
	//	- the volume claim request is successful
	//	- the PVC actually exists on the cluster
	//delete the volume claim
	//check that
	//	- listing no longer shows the volume
	//	- the PVC is actually gone from the cluster
}

TEST(DeleteVolumeMalformedRequests){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	
	//create a group
	//register a cluster
	//create a volume claim
	//create another user which does not belong to the group
	//ensure that the second user cannot delete the volum
}