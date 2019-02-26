#include "test.h"

#include <set>

#include <ServerUtilities.h>

TEST(UnauthenticatedListUserGroupMemberships){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing a user's Group memberships with no authentication
	//doesn't matter whether the user is real since this should be rejected on other grounds
	auto addResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/User_ABC/groups");
	ENSURE_EQUAL(addResp.status,403,
				 "Requests to list users' Group memberships without authentication should be rejected");
	
	//try listing a user's Group memberships with invalid authentication
	addResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/User_ABC/groups?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(addResp.status,403,
				 "Requests to list users' Group memberships with invalid authentication should be rejected");
}

TEST(ListUserGroupMemberships){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string groupName1="some-org";
	std::string groupName2="some-other-org";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName1, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Group creation request should succeed");
	}
	
	{ //create another VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName2, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Group creation request should succeed");
	}
	
	std::string uid;
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
	}
	
	auto schema=loadSchema(getSchemaDir()+"/GroupListResultSchema.json");
	
	{ //list the groups to which the user belongs
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/groups?token="+adminKey);
		ENSURE_EQUAL(infoResp.status,200,"Getting user's Group memberships should succeed");
		rapidjson::Document data;
		data.Parse(infoResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),0,"User should belong to no groups");
	}
	
	{ //add the user to a VO
		auto addResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/groups/"+groupName1+"?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,200,"User addition to Group request should succeed");
	}
	
	{ //list the groups to which the user belongs
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/groups?token="+adminKey);
		ENSURE_EQUAL(infoResp.status,200,"Getting user's Group memberships should succeed");
		rapidjson::Document data;
		data.Parse(infoResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),1,"User should belong to one Group");
		ENSURE_EQUAL(data["items"][0]["metadata"]["name"].GetString(),groupName1,"User should belong to the correct Group");
	}
	
	{ //add the user to a second VO
		auto addResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/groups/"+groupName2+"?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,200,"User addition to Group request should succeed");
	}
	
	{ //list the groups to which the user belongs
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/groups?token="+adminKey);
		ENSURE_EQUAL(infoResp.status,200,"Getting user's Group memberships should succeed");
		rapidjson::Document data;
		data.Parse(infoResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),2,"User should belong to two groups");
		std::set<std::string> groups;
		groups.insert(data["items"][0]["metadata"]["name"].GetString());
		groups.insert(data["items"][1]["metadata"]["name"].GetString());
		ENSURE_EQUAL(groups.size(),2,"User should belong to two distinct groups");
		ENSURE(groups.count(groupName1),"User should belong to first Group");
		ENSURE(groups.count(groupName2),"User should belong to second Group");
	}
	
	{ //remove the user from the first VO
		auto remResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/groups/"+groupName1+"?token="+adminKey);
		ENSURE_EQUAL(remResp.status,200,"User removal from Group request should succeed");
	}
	
	{ //list the groups to which the user belongs
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/groups?token="+adminKey);
		ENSURE_EQUAL(infoResp.status,200,"Getting user's Group memberships should succeed");
		rapidjson::Document data;
		data.Parse(infoResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),1,"User should belong to one Group");
		ENSURE_EQUAL(data["items"][0]["metadata"]["name"].GetString(),groupName2,"User should belong to the correct Group");
	}
}
