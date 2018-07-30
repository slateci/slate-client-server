#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedListApplications){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing applications with no authentication
	auto addResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/apps");
	ENSURE_EQUAL(addResp.status,403,
				 "Requests to list applications without authentication should be rejected");
	
	//try listing applications with invalid authentication
	addResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/apps?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(addResp.status,403,
				 "Requests to list applications with invalid authentication should be rejected");
}

TEST(ListApplications){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto schema=loadSchema("../../slate-portal-api-spec/AppListResultSchema.json");
	
	auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/apps?test&token="+adminKey);
	ENSURE_EQUAL(listResp.status,200,"Listing applications should succeed");
	rapidjson::Document data;
	data.Parse(listResp.body);
	ENSURE_CONFORMS(data,schema);
	ENSURE_EQUAL(data["items"].Size(),1,"Listing should contain one application");
	ENSURE_EQUAL(data["items"][0]["metadata"]["name"].GetString(),std::string("test-app"),
	             "Correct application should be reported");
}

//check that we get a sensible response for the main catalogue even though we 
//cannot verify that the contents are correct
TEST(ListApplicationsMainCatalogue){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto schema=loadSchema("../../slate-portal-api-spec/AppListResultSchema.json");
	
	auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/apps?token="+adminKey);
	ENSURE_EQUAL(listResp.status,200,"Listing applications should succeed");
	rapidjson::Document data;
	data.Parse(listResp.body);
	ENSURE_CONFORMS(data,schema);
}

//check that we get a sensible response for the development catalogue even though
//we cannot verify that the contents are correct
TEST(ListApplicationsDevCatalogue){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto schema=loadSchema("../../slate-portal-api-spec/AppListResultSchema.json");
	
	auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/apps?dev&token="+adminKey);
	ENSURE_EQUAL(listResp.status,200,"Listing applications should succeed");
	rapidjson::Document data;
	data.Parse(listResp.body);
	ENSURE_CONFORMS(data,schema);
}
