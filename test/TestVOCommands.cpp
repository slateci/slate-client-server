#include "test.h"

TEST(UnauthenticatedListVOs){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing VOs with no authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/vos");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list VOs without authentication should be rejected");
	
	//try listing VOs with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/vos?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list VOs with invalid authentication should be rejected");
}