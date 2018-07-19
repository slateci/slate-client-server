#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedListUsers){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing users with no authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/users");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list users without authentication should be rejected");
	
	//try listing users with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/users?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list users with invalid authentication should be rejected");
}

TEST(ListUsers){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string userURL=tc.getAPIServerURL()+"/v1alpha1/users?token="+adminKey;
	auto listResp=httpGet(userURL);
	ENSURE_EQUAL(listResp.status,200,"Portal admin user should be able to list users");
	
	ENSURE(!listResp.body.empty());
	rapidjson::Document data;
	data.Parse(listResp.body.c_str());
	
	auto schema=loadSchema("../../slate-portal-api-spec/UserListResultSchema.json");
	ENSURE_CONFORMS(data,schema);
	
	//should be only one user
	ENSURE_EQUAL(data["items"].Size(),1,"One user record should be returned");
	const auto& metadata=data["items"][0]["metadata"];
	ENSURE_EQUAL(metadata["name"].GetString(),std::string("WebPortal"),
	             "User name should match");
	ENSURE_EQUAL(metadata["email"].GetString(),std::string("admin@slateci.io"),
	             "User email should match");
	ENSURE_EQUAL(metadata["id"].GetString(),
	             std::string("User_12345678-9abc-def0-1234-56789abcdef0"),
	             "User ID should match");
	
	//add another user
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	httpPost(userURL,to_string(request1));
	
	//list again
	listResp=httpGet(userURL);
	ENSURE_EQUAL(listResp.status,200,"Portal admin user should be able to list users");
	
	ENSURE(!listResp.body.empty());
	data.Parse(listResp.body.c_str());
	ENSURE_CONFORMS(data,schema);
	
	//should now be two users
	ENSURE_EQUAL(data["items"].Size(),2,"Two user records should be returned");
}
