#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedListGroupMembers){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing Group members with no authentication
	//doesn't matter whether request body is correct since this should be rejected on other grounds
	auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/Group_123/members");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list members of groups without authentication should be rejected");
	
	//try listing Group members with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/Group_123/members?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list members of groups with invalid authentication should be rejected");
}

TEST(ListNonexistentGroupMembers){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	
	//try listing Group members with invalid authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/Group_123/members?token="+adminKey);
	ENSURE_EQUAL(listResp.status,404,
				 "Requests to list members of nonexistent groups should be rejected");
}

TEST(ListGroupMembers){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminID=tc.getPortalUserID();
	std::string adminKey=tc.getPortalToken();
	std::string groupName="some-org";
	auto schema=loadSchema(getSchemaDir()+"/UserListResultSchema.json");
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Group creation request should succeed");
	}
	
	auto userInListing=[](const rapidjson::Value::Array& items, const std::string& uid){
		return(std::find_if(items.begin(),items.end(),[&uid](const rapidjson::Value& item){
			return item["metadata"]["id"].GetString()==uid;
		})!=items.end());
	};
	
	{ //list Group members
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/"+groupName+"/members?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Listing Group members should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),1,"Group should have one member");
		ENSURE(userInListing(data["items"].GetArray(),adminID),"Creator should be a member of the Group");
	}
	
	std::string uid;
	std::string token;
	{ //create a user
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("phone", "555-5555", alloc);
		metadata.AddMember("institution", "Center of the Earth University", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		uid=createData["metadata"]["id"].GetString();
		token=createData["metadata"]["access_token"].GetString();
	}
	{ //add the user to a VO
		auto addResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/groups/"+groupName+"?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,200,"User addition to Group request should succeed");
	}
	
	{ //list Group members
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/"+groupName+"/members?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Listing Group members should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),2,"Group should have two members");
		const auto items=data["items"].GetArray();
		ENSURE(userInListing(items,adminID));
		ENSURE(userInListing(items,uid));
	}
	
	{ //remove the admin from the VO
		auto remResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+adminID+"/groups/"+groupName+"?token="+adminKey);
		ENSURE_EQUAL(remResp.status,200,"User removal from Group request should succeed");
	}
	
	{ //list Group members
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/"+groupName+"/members?token="+token);
		ENSURE_EQUAL(listResp.status,200,"Listing Group members should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),1,"Group should have one member");
		ENSURE(userInListing(data["items"].GetArray(),uid));
	}
}

TEST(NonMemberListGroupMembers){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminID=tc.getPortalUserID();
	std::string adminKey=tc.getPortalToken();
	std::string groupName="some-org";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Group creation request should succeed");
	}
	
	std::string uid;
	std::string token;
	{ //create a user
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("phone", "555-5555", alloc);
		metadata.AddMember("institution", "Center of the Earth University", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		uid=createData["metadata"]["id"].GetString();
		token=createData["metadata"]["access_token"].GetString();
	}
	
	{ //have nonmember attempt to list Group members
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/"+groupName+"/members?token="+token);
		ENSURE_EQUAL(listResp.status,403,"Requests by non-members to list Group members should be rejected");
	}
}
