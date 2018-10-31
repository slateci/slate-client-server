#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedDeleteUser){
	using namespace httpRequests;
	TestContext tc;
	
	//try deleting a user with no authentication
	//doesn't matter whether request body is correct since this should be rejected on other grounds
	auto deleteResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/users/User_ABC");
	ENSURE_EQUAL(deleteResp.status,403,
				 "Requests to delete users without authentication should be rejected");
	
	//try deleting a user with invalid authentication
	deleteResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/users/User_ABC?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(deleteResp.status,403,
				 "Requests to delete users with invalid authentication should be rejected");
}

TEST(DeleteUser){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	
	std::string uid;
	{ //create a user
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
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		uid=createData["metadata"]["id"].GetString();
	}
	
	{ //delete the user
		auto deleteResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/users/"+uid+"?token="+adminKey);
		ENSURE_EQUAL(deleteResp.status,200,"User deletion request should succeed");
	}
	
	{ //make sure the user is gone
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/users?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"User list request should succeed");
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		//Only the Portal user should remain
		ENSURE_EQUAL(listData["items"].Size(),1,"One user record should remain");
	}
}

TEST(SelfDeleteUser){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	
	std::string uid;
	std::string tok;
	{ //create a user
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
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		uid=createData["metadata"]["id"].GetString();
		tok=createData["metadata"]["access_token"].GetString();
	}
	
	{ //delete the user
		auto deleteResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/users/"+uid+"?token="+tok);
		ENSURE_EQUAL(deleteResp.status,200,"A user should be able to delete itself");
	}
	
	{ //make sure the user is gone
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/users?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"User list request should succeed");
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		//Only the Portal user should remain
		ENSURE_EQUAL(listData["items"].Size(),1,"One user record should remain");
	}
}

TEST(DeleteOtherUser){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	
	std::string uid;
	std::string tok;
	{ //create two users
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
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		tok=createData["metadata"]["access_token"].GetString();
	}
	{ //create two users
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
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		uid=createData["metadata"]["id"].GetString();
	}
	
	{ //have one user attempt to delete the other
		auto deleteResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/users/"+uid+"?token="+tok);
		ENSURE_EQUAL(deleteResp.status,403,"A non-admin user should not be able to delete other users");
	}
	
	{ //All users should still exist
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/users?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"User list request should succeed");
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		//Only the Portal user should remain
		ENSURE_EQUAL(listData["items"].Size(),3,"Three user records should remain");
	}
}

TEST(DeleteNonexistentUser){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	
	//invent an invalid user ID
	std::string uid="User_32875628365823658732658";
	
	{ //attempt to delete the user
		auto deleteResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/users/"+uid+"?token="+adminKey);
		ENSURE_EQUAL(deleteResp.status,404,"Deletion of non-existent user should be rejected");
	}
}

TEST(DeleteUserRemovesFromVOs){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	
	std::string uid;
	std::string tok;
	{ //create a user
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
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		uid=createData["metadata"]["id"].GetString();
		tok=createData["metadata"]["access_token"].GetString();
	}
	
	const std::string voName="testvo1";
	//have the user create a VO, implicitly making it a member
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp1=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+tok,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,
	             "First VO creation request should succeed");
	
	auto userInListing=[](const rapidjson::Value::Array& items, const std::string& uid){
		return(std::find_if(items.begin(),items.end(),[&uid](const rapidjson::Value& item){
			return item["metadata"]["id"].GetString()==uid;
		})!=items.end());
	};
	
	{ //list VO members
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/vos/"+voName+"/members?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Listing VO members should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_EQUAL(data["items"].Size(),1,"VO should have one member");
		ENSURE(userInListing(data["items"].GetArray(),uid),"Creator should be a member of the VO");
	}
	
	{ //delete the user
		auto deleteResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/users/"+uid+"?token="+tok);
		ENSURE_EQUAL(deleteResp.status,200,"A user should be able to delete itself");
	}
	
	{ //list VO members again
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/vos/"+voName+"/members?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Listing VO members should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_EQUAL(data["items"].Size(),0,"VO should have no members");
	}
}
