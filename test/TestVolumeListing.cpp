#include "test.h"

TEST(UnauthenticatedListVolumess){
	using namespace httpRequests;
	TestContext tc;

	//try listing volumes with no authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes");
	ENSURE_EQUAL(listResp.status,403,
	             "Requests to list volumes without authentication should be rejected");

	//try listing volumes with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
	            "Requests to list volumes with invalid authentication should be rejected");
}

TEST(ListVolumes){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/VolumeListResultSchema.json");
	
	//create a group
	//register a cluster
	//list volumes, ensure none found
	//create a volume
	//list volumes, ensure data matches schema and one correct item returned
	//create a second volume
	//list volumes, ensure data matches schema and two correct items returned
}

TEST(ListVolumessByCluster){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/VolumeListResultSchema.json");
	
	//create a group
	//register two clusters
	//create one volume on each cluster
	//list volumes on each cluster, ensure only correct volume appears
}

TEST(ListVolumessByGroup){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/VolumeListResultSchema.json");
	
	//create two groups
	//register a clusters
	//grant the second group access to the cluster
	//create one volume for each group
	//list volumes for each group, ensure only correct volume appears
}