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

TEST(AdminUpdateUser){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	const std::string originalName="Bob";
	const std::string originalEmail="bob@place.com";
	const std::string newName="Bob Smith";
	const std::string newEmail="bob.smith@wherever.edu";
	
	//add a new user
	std::string uid;
	{
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", originalName, alloc);
		metadata.AddMember("email", originalEmail, alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,
		             "User creation request should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document createData;
		createData.Parse(createResp.body.c_str());
		uid=createData["metadata"]["id"].GetString();
	}
	auto userUrl=tc.getAPIServerURL()+"/v1alpha1/users/"+uid+"?token="+adminKey;
	
	{ //change name
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", newName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(userUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,200,"User name update should succeed");
		
		//get the user info to see if the change was recorded
		auto infoResp=httpGet(userUrl);
		ENSURE_EQUAL(infoResp.status,200,"Getting user info should succeed");
		ENSURE(!infoResp.body.empty());
		rapidjson::Document infoData;
		infoData.Parse(infoResp.body.c_str());
		ENSURE_EQUAL(infoData["metadata"]["name"].GetString(),newName,
					 "User name should match updated value");
		ENSURE_EQUAL(infoData["metadata"]["email"].GetString(),originalEmail,
		             "User email remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["admin"].GetBool(),false,
		             "User should still not be an administrator");
	}
	{ //change email
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("email", newEmail, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(userUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,200,"User email update should succeed");
		
		//get the user info to see if the change was recorded
		auto infoResp=httpGet(userUrl);
		ENSURE_EQUAL(infoResp.status,200,"Getting user info should succeed");
		ENSURE(!infoResp.body.empty());
		rapidjson::Document infoData;
		infoData.Parse(infoResp.body.c_str());
		ENSURE_EQUAL(infoData["metadata"]["name"].GetString(),newName,
					 "User name should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["email"].GetString(),newEmail,
		             "User email should match updated value");
		ENSURE_EQUAL(infoData["metadata"]["admin"].GetBool(),false,
		             "User should still not be an administrator");
	}
	{ //change admin status
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("admin", true, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(userUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,200,"User admin update should succeed");
		
		//get the user info to see if the change was recorded
		auto infoResp=httpGet(userUrl);
		ENSURE_EQUAL(infoResp.status,200,"Getting user info should succeed");
		ENSURE(!infoResp.body.empty());
		rapidjson::Document infoData;
		infoData.Parse(infoResp.body.c_str());
		ENSURE_EQUAL(infoData["metadata"]["name"].GetString(),newName,
					 "User name should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["email"].GetString(),newEmail,
		             "User email should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["admin"].GetBool(),true,
		             "User should now be an administrator");
	}
}

TEST(NonUpdateUser){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	const std::string originalName="Bob";
	const std::string originalEmail="bob@place.com";
	const std::string newName="Bob Smith";
	const std::string newEmail="bob.smith@wherever.edu";
	
	//add a new user
	std::string uid;
	std::string token;
	{
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", originalName, alloc);
		metadata.AddMember("email", originalEmail, alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,
		             "User creation request should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document createData;
		createData.Parse(createResp.body.c_str());
		uid=createData["metadata"]["id"].GetString();
		token=createData["metadata"]["access_token"].GetString();
	}
	auto userUrl=tc.getAPIServerURL()+"/v1alpha1/users/"+uid+"?token="+token;
	
	{ //change name
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", newName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(userUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,200,"User name update should succeed");
		
		//get the user info to see if the change was recorded
		auto infoResp=httpGet(userUrl);
		ENSURE_EQUAL(infoResp.status,200,"Getting user info should succeed");
		ENSURE(!infoResp.body.empty());
		rapidjson::Document infoData;
		infoData.Parse(infoResp.body.c_str());
		ENSURE_EQUAL(infoData["metadata"]["name"].GetString(),newName,
					 "User name should match updated value");
		ENSURE_EQUAL(infoData["metadata"]["email"].GetString(),originalEmail,
		             "User email remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["admin"].GetBool(),false,
		             "User should still not be an administrator");
	}
	{ //change email
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("email", newEmail, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(userUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,200,"User email update should succeed");
		
		//get the user info to see if the change was recorded
		auto infoResp=httpGet(userUrl);
		ENSURE_EQUAL(infoResp.status,200,"Getting user info should succeed");
		ENSURE(!infoResp.body.empty());
		rapidjson::Document infoData;
		infoData.Parse(infoResp.body.c_str());
		ENSURE_EQUAL(infoData["metadata"]["name"].GetString(),newName,
					 "User name should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["email"].GetString(),newEmail,
		             "User email should match updated value");
		ENSURE_EQUAL(infoData["metadata"]["admin"].GetBool(),false,
		             "User should still not be an administrator");
	}
	{ //change admin status
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("admin", true, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(userUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,403,
		             "Non-admin user should not be able to update admin status");
		
		//get the user info to see if the change was recorded
		auto infoResp=httpGet(userUrl);
		ENSURE_EQUAL(infoResp.status,200,"Getting user info should succeed");
		ENSURE(!infoResp.body.empty());
		rapidjson::Document infoData;
		infoData.Parse(infoResp.body.c_str());
		ENSURE_EQUAL(infoData["metadata"]["name"].GetString(),newName,
					 "User name should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["email"].GetString(),newEmail,
		             "User email should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["admin"].GetBool(),false,
		             "User should still not be an administrator");
	}
}

TEST(UpdateOtherUser){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	
	//add a new user
	std::string uid;
	std::string token;
	{
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,
		             "User creation request should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document createData;
		createData.Parse(createResp.body.c_str());
		token=createData["metadata"]["access_token"].GetString();
	}
	{
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Fred", alloc);
		metadata.AddMember("email", "fred@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Fred's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,
		             "User creation request should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document createData;
		createData.Parse(createResp.body.c_str());
		uid=createData["metadata"]["id"].GetString();
	}
	auto userUrl=tc.getAPIServerURL()+"/v1alpha1/users/"+uid+"?token="+token;
	
	//One non-admin user should not be able to alter another's account
	{ //change email
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("email", "notfred@someplace.else", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(userUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,403,"Non-admin should not be able the change other users");
	}
}

TEST(MalformedUpdateUser){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	const std::string originalName="Bob";
	const std::string originalEmail="bob@place.com";
	
	//add a new user
	std::string uid;
	{
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", originalName, alloc);
		metadata.AddMember("email", originalEmail, alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,
		             "User creation request should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document createData;
		createData.Parse(createResp.body.c_str());
		uid=createData["metadata"]["id"].GetString();
	}
	auto userUrl=tc.getAPIServerURL()+"/v1alpha1/users/"+uid+"?token="+adminKey;
	
	{ //invalid JSON request body
		auto updateResp=httpPut(userUrl,"This is not JSON");
		ENSURE_EQUAL(updateResp.status,400,"Invalid JSON requests should be rejected");
	}
	{ //empty JSON
		rapidjson::Document request(rapidjson::kObjectType);
		auto updateResp=httpPut(userUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Empty JSON requests should be rejected");
	}
	{ //missing metadata
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		auto updateResp=httpPut(userUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Requests without metadata section should be rejected");
	}
	{ //change name
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", 72, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(userUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Update with wrong type for name should fail");
	}
	{ //change email
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("email", true, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(userUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Update with wrong type for email should fail");
	}
	{ //change admin status
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("admin", "Moldova", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(userUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Update with wrong type for admin flag should fail");
	}
}
