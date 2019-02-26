#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedListUsers){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing users with no authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list users without authentication should be rejected");
	
	//try listing users with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list users with invalid authentication should be rejected");
}

TEST(ListUsers){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string userURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey;
	auto listResp=httpGet(userURL);
	ENSURE_EQUAL(listResp.status,200,"Portal admin user should be able to list users");
	
	ENSURE(!listResp.body.empty());
	rapidjson::Document data;
	data.Parse(listResp.body.c_str());
	
	auto schema=loadSchema(getSchemaDir()+"/UserListResultSchema.json");
	ENSURE_CONFORMS(data,schema);
	
	//should be only one user
	ENSURE_EQUAL(data["items"].Size(),1,"One user record should be returned");
	const auto& metadata=data["items"][0]["metadata"];
	ENSURE_EQUAL(metadata["name"].GetString(),std::string("WebPortal"),
	             "User name should match");
	ENSURE_EQUAL(metadata["email"].GetString(),std::string("admin@slateci.io"),
	             "User email should match");
	ENSURE_EQUAL(metadata["phone"].GetString(),std::string("555-5555"),
	             "User phone should match");
	ENSURE_EQUAL(metadata["institution"].GetString(),std::string("SLATE"),
	             "User institution should match");
	ENSURE_EQUAL(metadata["id"].GetString(),
	             std::string("User_12345678-9abc-def0-1234-56789abcdef0"),
	             "User ID should match");
	
	//add another user
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("phone", "555-5555", alloc);
		metadata.AddMember("institution", "Center of the Earth University", alloc);
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

TEST(ListUsersByGroup){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string userURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey;

	//add a regular user
	rapidjson::Document createUser(rapidjson::kObjectType);
	{
		auto& alloc = createUser.GetAllocator();
	        createUser.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("phone", "555-5555", alloc);
		metadata.AddMember("institution", "Center of the Earth University", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		createUser.AddMember("metadata", metadata, alloc);
	}
	auto userResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(createUser));
	ENSURE_EQUAL(userResp.status,200,"Portal admin user should be able to create a regular user");
	ENSURE(!userResp.body.empty());
	rapidjson::Document userData;
	userData.Parse(userResp.body.c_str());
	
	auto userSchema=loadSchema(getSchemaDir()+"/UserInfoResultSchema.json");
	ENSURE_CONFORMS(userData, userSchema);

	auto userKey=userData["metadata"]["access_token"].GetString();

	
	//add a VO
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testgroup1", alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp1=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,"Portal admin user should be able to create a Group");
	
	ENSURE(!createResp1.body.empty());
	rapidjson::Document respData;
	respData.Parse(createResp1.body.c_str());
	auto respSchema = loadSchema(getSchemaDir()+"/GroupCreateResultSchema.json");
	ENSURE_CONFORMS(respData,respSchema);

	auto groupID=respData["metadata"]["id"].GetString();

	//list all users
	auto listResp=httpGet(userURL);
	ENSURE_EQUAL(listResp.status,200,"Portal admin user should be able to list users");
	
	ENSURE(!listResp.body.empty());
	rapidjson::Document data;
	data.Parse(listResp.body.c_str());
	
	auto schema=loadSchema(getSchemaDir()+"/UserListResultSchema.json");
	ENSURE_CONFORMS(data,schema);

	//should be two users
	ENSURE_EQUAL(data["items"].Size(),2,"Two user records should be returned");
	
	//list users associated with the created VO
	listResp=httpGet(userURL+"&group="+groupID);
	ENSURE_EQUAL(listResp.status,200,"Portal admin user should be able to list users");
	
	ENSURE(!listResp.body.empty());
	data.Parse(listResp.body.c_str());
        ENSURE_CONFORMS(data,schema);
	
	//should be only one user
	ENSURE_EQUAL(data["items"].Size(),1,"One user record should be returned for the Group");
	const auto& metadata=data["items"][0]["metadata"];
	ENSURE_EQUAL(metadata["name"].GetString(),std::string("WebPortal"),
	             "User name should match");
	ENSURE_EQUAL(metadata["email"].GetString(),std::string("admin@slateci.io"),
	             "User email should match");
	ENSURE_EQUAL(metadata["phone"].GetString(),std::string("555-5555"),
	             "User phone should match");
	ENSURE_EQUAL(metadata["institution"].GetString(),std::string("SLATE"),
	             "User institution should match");
	ENSURE_EQUAL(metadata["id"].GetString(),
	             std::string("User_12345678-9abc-def0-1234-56789abcdef0"),
	             "User ID should match");
}
