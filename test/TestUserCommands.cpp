#include "test.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

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
	auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/users?token="+adminKey);
	ENSURE_EQUAL(listResp.status,200,"Portal admin user should be able to list users");
	
	//TODO: replace this with real schema verification
	ENSURE(!listResp.body.empty());
	rapidjson::Document data;
	data.Parse(listResp.body.c_str());
	ENSURE(data.IsObject(),"List users response should contain valid JSON");
	ENSURE(data.HasMember("items"),"List users response should contain a list of user records");
	ENSURE(data["items"].IsArray(),"List users response should contain a list of user records");
	ENSURE_EQUAL(data["items"].Size(),1,"One user record should be returned");
	ENSURE(data["items"][0].IsObject(),"Each user record should be an object");
	ENSURE(data["items"][0].HasMember("metadata"),"Each user record should contain metadata");
	ENSURE(data["items"][0]["metadata"].IsObject(),"User metadata should be an object");
	const auto& metadata=data["items"][0]["metadata"];
	ENSURE(metadata.HasMember("name"),"User record should have a name attribute");
	ENSURE(metadata.HasMember("id"),"User record should have an ID attribute");
	ENSURE(metadata.HasMember("email"),"User record should have an email attribute");
	ENSURE(metadata["name"].IsString(),"User name should be a string");
	ENSURE(metadata["id"].IsString(),"User ID should be a string");
	ENSURE(metadata["email"].IsString(),"User email should be a string");
}

TEST(UnauthenticatedCreateUser){
	using namespace httpRequests;
	TestContext tc;
	
	//try cerating a user with no authentication
	//doesn't matter whether request body is correct since this should be rejected on other grounds
	auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/users","");
	ENSURE_EQUAL(createResp.status,403,
				 "Requests to list users without authentication should be rejected");
	
	//try creating a user with invalid authentication
	createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/users?token=00112233-4455-6677-8899-aabbccddeeff","");
	ENSURE_EQUAL(createResp.status,403,
				 "Requests to list users with invalid authentication should be rejected");
}

TEST(DuplicateGlobusIDs){
	using namespace httpRequests;
	TestContext tc;
	
	//TODO: abstract this
	std::string adminKey=getPortalToken();
	auto createUserUrl=tc.getAPIServerURL()+"/v1alpha1/users?token="+adminKey;
	
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Person1", alloc);
		metadata.AddMember("email", "email1", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "SomeGlobusID", alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp1=httpPost(createUserUrl,to_string(request1));
	std::cout << createResp1.body << std::endl;
	ENSURE_EQUAL(createResp1.status,200,
				 "First user creation request should succeed");
	
	rapidjson::Document request2(rapidjson::kObjectType);
	{
		auto& alloc = request2.GetAllocator();
		request2.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Person2", alloc);
		metadata.AddMember("email", "email2", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "SomeGlobusID", alloc);
		request2.AddMember("metadata", metadata, alloc);
	}
	auto createResp2=httpPost(createUserUrl,to_string(request2));
	std::cout << createResp2.body << std::endl;
	ENSURE_EQUAL(createResp2.status,400,
				 "Second user creation request should be blocked");
}