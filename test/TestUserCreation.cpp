#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedCreateUser){
	using namespace httpRequests;
	TestContext tc;
	
	//try creating a user with no authentication
	//doesn't matter whether request body is correct since this should be rejected on other grounds
	auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/users","");
	ENSURE_EQUAL(createResp.status,403,
				 "Requests to list users without authentication should be rejected");
	
	//try creating a user with invalid authentication
	createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/users?token=00112233-4455-6677-8899-aabbccddeeff","");
	ENSURE_EQUAL(createResp.status,403,
				 "Requests to list users with invalid authentication should be rejected");
}

TEST(CreateUser){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto createUserUrl=tc.getAPIServerURL()+"/v1alpha1/users?token="+adminKey;
	
	//create a regular user
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
	auto createResp1=httpPost(createUserUrl,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,
				 "First user creation request should succeed");
	
	ENSURE(!createResp1.body.empty());
	rapidjson::Document respData1;
	respData1.Parse(createResp1.body.c_str());
	
	auto schema=loadSchema("../../slate-portal-api-spec/UserInfoResultSchema.json");
	ENSURE_CONFORMS(respData1,schema);
	ENSURE_EQUAL(respData1["metadata"]["name"].GetString(),std::string("Bob"),
	             "User name should match");
	ENSURE_EQUAL(respData1["metadata"]["email"].GetString(),std::string("bob@place.com"),
	             "User email should match");
	ENSURE_EQUAL(respData1["metadata"]["admin"].GetBool(),false,
	             "User admin flag should match");
	ENSURE_EQUAL(respData1["metadata"]["VOs"].Size(),0,
	             "User should not belong to any VOs");
	
	//create an admin user
	rapidjson::Document request2(rapidjson::kObjectType);
	{
		auto& alloc = request2.GetAllocator();
		request2.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Fred", alloc);
		metadata.AddMember("email", "fred@place.com", alloc);
		metadata.AddMember("admin", true, alloc);
		metadata.AddMember("globusID", "Fred's Globus ID", alloc);
		request2.AddMember("metadata", metadata, alloc);
	}
	auto createResp2=httpPost(createUserUrl,to_string(request2));
	ENSURE_EQUAL(createResp2.status,200,
				 "Second user creation request should succeed");
	
	ENSURE(!createResp2.body.empty());
	rapidjson::Document respData2;
	respData2.Parse(createResp2.body.c_str());
	
	ENSURE_CONFORMS(respData2,schema);
	ENSURE_EQUAL(respData2["metadata"]["name"].GetString(),std::string("Fred"),
	             "User name should match");
	ENSURE_EQUAL(respData2["metadata"]["email"].GetString(),std::string("fred@place.com"),
	             "User email should match");
	ENSURE_EQUAL(respData2["metadata"]["admin"].GetBool(),true,
	             "User admin flag should match");
	ENSURE_EQUAL(respData2["metadata"]["VOs"].Size(),0,
	             "User should not belong to any VOs");
}

TEST(NonAdminCreateAdmin){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto createUserUrl=tc.getAPIServerURL()+"/v1alpha1/users?token="+adminKey;
	
	//create a regular user
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
	auto createResp1=httpPost(createUserUrl,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,
				 "First user creation request should succeed");
	
	ENSURE(!createResp1.body.empty());
	rapidjson::Document respData1;
	respData1.Parse(createResp1.body.c_str());
	
	auto schema=loadSchema("../../slate-portal-api-spec/UserInfoResultSchema.json");
	ENSURE_CONFORMS(respData1,schema);
	ENSURE_EQUAL(respData1["metadata"]["name"].GetString(),std::string("Bob"),
	             "User name should match");
	ENSURE_EQUAL(respData1["metadata"]["email"].GetString(),std::string("bob@place.com"),
	             "User email should match");
	ENSURE_EQUAL(respData1["metadata"]["admin"].GetBool(),false,
	             "User admin flag should match");
	ENSURE_EQUAL(respData1["metadata"]["VOs"].Size(),0,
	             "User should not belong to any VOs");
	
	std::string nonAdminKey=respData1["metadata"]["access_token"].GetString();
	
	//try to create an admin user, as the non-admin user
	rapidjson::Document request2(rapidjson::kObjectType);
	{
		auto& alloc = request2.GetAllocator();
		request2.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Fred", alloc);
		metadata.AddMember("email", "fred@place.com", alloc);
		metadata.AddMember("admin", true, alloc);
		metadata.AddMember("globusID", "Fred's Globus ID", alloc);
		request2.AddMember("metadata", metadata, alloc);
	}
	auto createResp2=httpPost(tc.getAPIServerURL()+"/v1alpha1/users?token="+nonAdminKey,to_string(request2));
	ENSURE_EQUAL(createResp2.status,403,
				 "NOn-admins should not be able to create admins");
}

TEST(MalformedCreateRequests){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto createUserUrl=tc.getAPIServerURL()+"/v1alpha1/users?token="+adminKey;
	
	{ //invalid JSON request body
		auto createResp=httpPost(createUserUrl,"This is not JSON");
		ENSURE_EQUAL(createResp.status,400,
					 "Invalid JSON requests should be rejected");
	}
	{ //empty JSON
		rapidjson::Document request(rapidjson::kObjectType);
		auto createResp=httpPost(createUserUrl,to_string(request));
		ENSURE_EQUAL(createResp.status,400,
					 "Empty JSON requests should be rejected");
	}
	{ //missing metadata
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		auto createResp=httpPost(createUserUrl,to_string(request));
		ENSURE_EQUAL(createResp.status,400,
					 "Requests without metadata section should be rejected");
	}
	{ //missing name
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createUserUrl,to_string(request));
		ENSURE_EQUAL(createResp.status,400,
					 "Requests without a user name should be rejected");
	}
	{ //wrong name type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", 17, alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createUserUrl,to_string(request));
		ENSURE_EQUAL(createResp.status,400,
					 "Requests without a user name should be rejected");
	}
	{ //missing email
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createUserUrl,to_string(request));
		ENSURE_EQUAL(createResp.status,400,
					 "Requests without a user name should be rejected");
	}
	{ //wrong email type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", false, alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createUserUrl,to_string(request));
		ENSURE_EQUAL(createResp.status,400,
					 "Requests without a user name should be rejected");
	}
	{ //missing admin flag
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createUserUrl,to_string(request));
		ENSURE_EQUAL(createResp.status,400,
					 "Requests without a user name should be rejected");
	}
	{ //wrong admin type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", "yes", alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createUserUrl,to_string(request));
		ENSURE_EQUAL(createResp.status,400,
					 "Requests without a user name should be rejected");
	}
	{ //missing Globus ID
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createUserUrl,to_string(request));
		ENSURE_EQUAL(createResp.status,400,
					 "Requests without a user name should be rejected");
	}
	{ //wrong Globus ID type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", -22.8, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createUserUrl,to_string(request));
		ENSURE_EQUAL(createResp.status,400,
					 "Requests without a user name should be rejected");
	}
}

TEST(DuplicateGlobusIDs){
	using namespace httpRequests;
	TestContext tc;
	
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
	ENSURE_EQUAL(createResp2.status,400,
				 "Second user creation request should be blocked");
}
