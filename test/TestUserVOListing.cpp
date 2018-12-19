#include "test.h"

#include <set>

#include <Utilities.h>

TEST(UnauthenticatedListUserVOMemberships){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing a user's VO memberships with no authentication
	//doesn't matter whether the user is real since this should be rejected on other grounds
	auto addResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/User_ABC/vos");
	ENSURE_EQUAL(addResp.status,403,
				 "Requests to list users' VO memberships without authentication should be rejected");
	
	//try listing a user's VO memberships with invalid authentication
	addResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/User_ABC/vos?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(addResp.status,403,
				 "Requests to list users' VO memberships with invalid authentication should be rejected");
}

TEST(ListUserVOMemberships){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName1="some-org";
	std::string voName2="some-other-org";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName1, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	{ //create another VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName2, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	std::string uid;
	{ //create a user
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		uid=createData["metadata"]["id"].GetString();
	}
	
	auto schema=loadSchema(getSchemaDir()+"/VOListResultSchema.json");
	
	{ //list the VOs to which the user belongs
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos?token="+adminKey);
		ENSURE_EQUAL(infoResp.status,200,"Getting user's VO memberships should succeed");
		rapidjson::Document data;
		data.Parse(infoResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),0,"User should belong to no VOs");
	}
	
	{ //add the user to a VO
		auto addResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos/"+voName1+"?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,200,"User addition to VO request should succeed");
	}
	
	{ //list the VOs to which the user belongs
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos?token="+adminKey);
		ENSURE_EQUAL(infoResp.status,200,"Getting user's VO memberships should succeed");
		rapidjson::Document data;
		data.Parse(infoResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),1,"User should belong to one VO");
		ENSURE_EQUAL(data["items"][0]["metadata"]["name"].GetString(),voName1,"User should belong to the correct VO");
	}
	
	{ //add the user to a second VO
		auto addResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos/"+voName2+"?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,200,"User addition to VO request should succeed");
	}
	
	{ //list the VOs to which the user belongs
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos?token="+adminKey);
		ENSURE_EQUAL(infoResp.status,200,"Getting user's VO memberships should succeed");
		rapidjson::Document data;
		data.Parse(infoResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),2,"User should belong to two VOs");
		std::set<std::string> vos;
		vos.insert(data["items"][0]["metadata"]["name"].GetString());
		vos.insert(data["items"][1]["metadata"]["name"].GetString());
		ENSURE_EQUAL(vos.size(),2,"User should belong to two distinct VOs");
		ENSURE(vos.count(voName1),"User should belong to first VO");
		ENSURE(vos.count(voName2),"User should belong to second VO");
	}
	
	{ //remove the user from the first VO
		auto remResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos/"+voName1+"?token="+adminKey);
		ENSURE_EQUAL(remResp.status,200,"User removal from VO request should succeed");
	}
	
	{ //list the VOs to which the user belongs
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos?token="+adminKey);
		ENSURE_EQUAL(infoResp.status,200,"Getting user's VO memberships should succeed");
		rapidjson::Document data;
		data.Parse(infoResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),1,"User should belong to one VO");
		ENSURE_EQUAL(data["items"][0]["metadata"]["name"].GetString(),voName2,"User should belong to the correct VO");
	}
}
