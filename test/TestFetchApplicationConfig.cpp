#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedFetchApplicationConfig){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing applications with no authentication
	auto addResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/test-app?test");
	ENSURE_EQUAL(addResp.status,200,
				 "Requests to fetch application config without authentication should be permitted");
}

TEST(FetchApplicationConfig){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	auto schema=loadSchema(getSchemaDir()+"/AppConfResultSchema.json");
	
	auto confResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/test-app?test&token="+adminKey);
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
	
	std::string adminKey=tc.getPortalToken();
	
	auto confResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/not-an-app?test&token="+adminKey);
	ENSURE_EQUAL(confResp.status,404,"Fetching non-existent application configuration should fail");
}

TEST(UnauthenticatedFetchApplicationInfo){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing applications with no authentication
	auto addResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/test-app/info?test");
	ENSURE_EQUAL(addResp.status,200,
				 "Requests to fetch application information without authentication should be permitted");
}

TEST(FetchApplicationInfo){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	auto schema=loadSchema(getSchemaDir()+"/AppInfoResultSchema.json");
	
	auto confResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/test-app/info?test&token="+adminKey);
	ENSURE_EQUAL(confResp.status,200,"Fetching application information should succeed");
	rapidjson::Document data;
	data.Parse(confResp.body);
	ENSURE_CONFORMS(data,schema);
}

TEST(FetchNonexistentApplicationInfo){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	
	auto confResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/not-an-app/info?test&token="+adminKey);
	ENSURE_EQUAL(confResp.status,404,"Fetching non-existent application information should fail");
}
