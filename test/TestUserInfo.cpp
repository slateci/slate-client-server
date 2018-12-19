#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedGetUserInfo){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing users with no authentication
	auto resp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/User_1234");
	ENSURE_EQUAL(resp.status,403,
	             "Requests to get user info without authentication should be rejected");
	
	//try listing users with invalid authentication
	resp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/User_1234?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(resp.status,403,
	             "Requests to get user info with invalid authentication should be rejected");
}

TEST(NonexistentGetUserInfo){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto resp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/blah?token="+adminKey);
	ENSURE_EQUAL(resp.status,404,
				 "Requests to get info on a nonexistent user should be rejected");
}

TEST(GetUserInfo){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	const std::string adminUID="User_12345678-9abc-def0-1234-56789abcdef0";
	auto resp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+adminUID+"?token="+adminKey);
	ENSURE_EQUAL(resp.status,200,
				 "Getting the portal user's information should succeed");
	
	ENSURE(!resp.body.empty(),"Response body should not be empty");
	rapidjson::Document data;
	data.Parse(resp.body.c_str());
	
	auto schema=loadSchema(getSchemaDir()+"/UserInfoResultSchema.json");
	ENSURE_CONFORMS(data,schema);
	
	const auto& metadata=data["metadata"];
	ENSURE_EQUAL(metadata["name"].GetString(),std::string("WebPortal"),
	             "User name should match");
	ENSURE_EQUAL(metadata["email"].GetString(),std::string("admin@slateci.io"),
	             "User email should match");
	ENSURE_EQUAL(metadata["id"].GetString(),
	             std::string("User_12345678-9abc-def0-1234-56789abcdef0"),
	             "User ID should match");
}

TEST(GetUserNewInfo){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	
	std::string uid;
	//add a new user
	{
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request1));
		ENSURE_EQUAL(createResp.status,200,
		             "User creation request should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document createData;
		createData.Parse(createResp.body.c_str());
		uid=createData["metadata"]["id"].GetString();
	}
	
	//then get the info about that user
	auto resp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"?token="+adminKey);
	ENSURE_EQUAL(resp.status,200,
				 "Getting the new user's information should succeed");
	
	ENSURE(!resp.body.empty(),"Response body should not be empty");
	rapidjson::Document data;
	data.Parse(resp.body.c_str());
	
	auto schema=loadSchema(getSchemaDir()+"/UserInfoResultSchema.json");
	ENSURE_CONFORMS(data,schema);
	
	const auto& metadata=data["metadata"];
	ENSURE_EQUAL(metadata["name"].GetString(),std::string("Bob"),
	             "User name should match");
	ENSURE_EQUAL(metadata["email"].GetString(),std::string("bob@place.com"),
	             "User email should match");
	ENSURE_EQUAL(metadata["id"].GetString(),uid,
	             "User ID should match");
}

TEST(GetInfoAuthorization){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string baseUserURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token=";
	
	std::string uid1, tok1, uid2, tok2;
	{ //add a new user
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(baseUserURL+adminKey,to_string(request1));
		ENSURE_EQUAL(createResp.status,200,
		             "User creation request should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document createData;
		createData.Parse(createResp.body.c_str());
		uid1=createData["metadata"]["id"].GetString();
		tok1=createData["metadata"]["access_token"].GetString();
	}
	{ //add another new user
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Fred", alloc);
		metadata.AddMember("email", "fred@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Fred's Globus ID", alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(baseUserURL+adminKey,to_string(request1));
		ENSURE_EQUAL(createResp.status,200,
		             "User creation request should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document createData;
		createData.Parse(createResp.body.c_str());
		uid2=createData["metadata"]["id"].GetString();
		tok2=createData["metadata"]["access_token"].GetString();
	}
	
	auto resp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid1+"?token="+tok1);
	ENSURE_EQUAL(resp.status,200,"First user should be able to get own info");
	
	resp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid2+"?token="+tok2);
	ENSURE_EQUAL(resp.status,200,"Second user should be able to get own info");
	
	resp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid2+"?token="+tok1);
	ENSURE_EQUAL(resp.status,403,"First user should not be able to get second user's info");
	
	resp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid1+"?token="+tok2);
	ENSURE_EQUAL(resp.status,403,"Second user should not be able to get first user's info");
}
