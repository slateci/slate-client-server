#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedReplaceUserToken){
	using namespace httpRequests;
	TestContext tc;
	
	//try updating a user with no authentication
	auto resp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/User_1234/replace_token");
	ENSURE_EQUAL(resp.status,403,
	             "Requests to replace user token without authentication should be rejected");
	
	//try updating a user with invalid authentication
	resp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/User_1234/replace_token?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(resp.status,403,
	             "Requests to replace user token with invalid authentication should be rejected");
}

TEST(ReplaceNonexistentUserToken){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto resp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/blah/replace_token?token="+adminKey);
	ENSURE_EQUAL(resp.status,404,
	             "Requests to replace user token for a nonexistent user should be rejected");
}

TEST(AdminReplaceUserToken){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	const std::string originalName="Bob";
	const std::string originalEmail="bob@place.com";
	
	//add a new user
	std::string uid;
	std::string originalToken;
	{
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", originalName, alloc);
		metadata.AddMember("email", originalEmail, alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,
		             "User creation request should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document createData;
		createData.Parse(createResp.body.c_str());
		uid=createData["metadata"]["id"].GetString();
		originalToken=createData["metadata"]["access_token"].GetString();
	}
	{ //check that the user has access
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+originalToken);
		ENSURE_EQUAL(listResp.status,200,"User should be able to list users");
	}
	std::string newToken;
	{ //replace the user's token
		auto resp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/replace_token?token="+adminKey);
		ENSURE_EQUAL(resp.status,200,
		             "User token replacement request should succeed");
		ENSURE(!resp.body.empty());
		rapidjson::Document updateData;
		updateData.Parse(resp.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/FindUserResultSchema.json");
		ENSURE_CONFORMS(updateData,schema);
		uid=updateData["metadata"]["id"].GetString();
		newToken=updateData["metadata"]["access_token"].GetString();
	}
	{ //check that the old token no longer gives access
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+originalToken);
		ENSURE_EQUAL(listResp.status,403,"Requests with revoked tokens should be rejected");
	}
	{ //check that the new token does give access
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+newToken);
		ENSURE_EQUAL(listResp.status,200,"User should be able to list users with new token.");
	}
}