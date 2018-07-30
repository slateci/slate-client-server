#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedFetchApplicationConfig){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing applications with no authentication
	auto addResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/apps/foo");
	ENSURE_EQUAL(addResp.status,403,
				 "Requests to fetch application config without authentication should be rejected");
	
	//try listing applications with invalid authentication
	addResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/apps/foo?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(addResp.status,403,
				 "Requests to fetch application config with invalid authentication should be rejected");
}

TEST(FetchApplicationConfig){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto schema=loadSchema("../../slate-portal-api-spec/AppConfResultSchema.json");
	
	auto confResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey);
	ENSURE_EQUAL(confResp.status,200,"Fetching application configuration should succeed");
	rapidjson::Document data;
	data.Parse(confResp.body);
	ENSURE_CONFORMS(data,schema);
	std::string values=data["spec"]["body"].GetString();
	ENSURE(values.find("replicaCount: 1")!=std::string::npos,
	       "Application configure should contain expected data");
}

TEST(FetchNonexistentApplicationConfig){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	
	auto confResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/apps/not-an-app?test&token="+adminKey);
	ENSURE_EQUAL(confResp.status,404,"Fetching non-existent application configuration should fail");
}
