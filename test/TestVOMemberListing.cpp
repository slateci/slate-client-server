#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedListVOMembers){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing VO members with no authentication
	//doesn't matter whether request body is correct since this should be rejected on other grounds
	auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/vos/VO_123/members");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list members of VOs without authentication should be rejected");
	
	//try listing VO members with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/vos/VO_123/members?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list members of VOs with invalid authentication should be rejected");
}

TEST(ListNonexistentVOMembers){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	
	//try listing VO members with invalid authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/vos/VO_123/members?token="+adminKey);
	ENSURE_EQUAL(listResp.status,404,
				 "Requests to list members of nonexistent VOs should be rejected");
}

TEST(ListVOMembers){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminID=getPortalUserID();
	std::string adminKey=getPortalToken();
	std::string voName="some-org";
	auto schema=loadSchema("../../slate-portal-api-spec/UserListResultSchema.json");
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
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
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),1,"VO should have one member");
		ENSURE(userInListing(data["items"].GetArray(),adminID),"Creator should be a member of the VO");
	}
	
	std::string uid;
	std::string token;
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
		token=createData["metadata"]["access_token"].GetString();
	}
	{ //add the user to a VO
		auto addResp=httpPut(tc.getAPIServerURL()+"/v1alpha1/users/"+uid+"/vos/"+voName+"?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,200,"User addition to VO request should succeed");
	}
	
	{ //list VO members
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/vos/"+voName+"/members?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,"Listing VO members should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),2,"VO should have two members");
		const auto items=data["items"].GetArray();
		ENSURE(userInListing(items,adminID));
		ENSURE(userInListing(items,uid));
	}
	
	{ //remove the admin from the VO
		auto remResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/users/"+adminID+"/vos/"+voName+"?token="+adminKey);
		ENSURE_EQUAL(remResp.status,200,"User removal from VO request should succeed");
	}
	
	{ //list VO members
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/vos/"+voName+"/members?token="+token);
		ENSURE_EQUAL(listResp.status,200,"Listing VO members should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),1,"VO should have one member");
		ENSURE(userInListing(data["items"].GetArray(),uid));
	}
}

TEST(NonMemberListVOMembers){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminID=getPortalUserID();
	std::string adminKey=getPortalToken();
	std::string voName="some-org";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	std::string uid;
	std::string token;
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
		token=createData["metadata"]["access_token"].GetString();
	}
	
	{ //have nonmember attempt to list VO members
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/vos/"+voName+"/members?token="+token);
		ENSURE_EQUAL(listResp.status,403,"Requests by non-members to list VO members should be rejected");
	}
}
