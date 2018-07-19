#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedUpdateUser){
	using namespace httpRequests;
	TestContext tc;
	
	//try updating a user with no authentication
	auto resp=httpPut(tc.getAPIServerURL()+"/v1alpha1/users/User_1234","stuff");
	ENSURE_EQUAL(resp.status,403,
	             "Requests to get user info without authentication should be rejected");
	
	//try updating a user with invalid authentication
	resp=httpPut(tc.getAPIServerURL()+"/v1alpha1/users/User_1234?token=00112233-4455-6677-8899-aabbccddeeff","stuff");
	ENSURE_EQUAL(resp.status,403,
	             "Requests to get user info with invalid authentication should be rejected");
}

TEST(UpdateNonexistentUser){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto resp=httpPut(tc.getAPIServerURL()+"/v1alpha1/users/blah?token="+adminKey,"stuff");
	ENSURE_EQUAL(resp.status,404,
				 "Requests to get info on a nonexistent user should be rejected");
}
