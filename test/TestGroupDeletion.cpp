#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedDeleteGroup){
	using namespace httpRequests;
	TestContext tc;
	
	//try deleting a Group with no authentication
	auto createResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/Group_1234567890");
	ENSURE_EQUAL(createResp.status,403,
				 "Requests to delete groups without authentication should be rejected");
	
	//try deleting a Group with invalid authentication
	createResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/Group_1234567890?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(createResp.status,403,
				 "Requests to delete groups with invalid authentication should be rejected");

}

TEST(DeleteGroup){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=getPortalToken();
	auto baseGroupUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups";
	auto token="?token="+adminKey;
	
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
	auto createResp1=httpPost(baseGroupUrl+token,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,"Portal admin user should be able to create a Group");

	ENSURE(!createResp1.body.empty());
	rapidjson::Document respData1;
	respData1.Parse(createResp1.body.c_str());
	ENSURE_EQUAL(respData1["metadata"]["name"].GetString(),std::string("testgroup1"),
	             "Group name should match");
	auto id=respData1["metadata"]["id"].GetString();
	
	//delete the just added VO
	auto deleteResp=httpDelete(baseGroupUrl+"/"+id+token);
	ENSURE_EQUAL(deleteResp.status,200,"Portal admin user should be able to delete groups");

	//get list of groups to check if deleted
	auto listResp=httpGet(baseGroupUrl+token);
	ENSURE_EQUAL(listResp.status,200,"Portal admin user should be able to list groups");
	ENSURE(!listResp.body.empty());
	rapidjson::Document data(rapidjson::kObjectType);
	data.Parse(listResp.body.c_str());
	auto schema=loadSchema(getSchemaDir()+"/GroupListResultSchema.json");
	ENSURE_CONFORMS(data,schema);

	//check that there are no groups
	ENSURE_EQUAL(data["items"].Size(),0,"No Group records should be returned");
}

TEST(DeleteNonexistantGroup){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=getPortalToken();
	auto baseGroupUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups";
	auto token="?token="+adminKey;

	//try to delete nonexisting VO
	auto deleteResp2=httpDelete(baseGroupUrl+"/Group_1234567890"+token);
	ENSURE_EQUAL(deleteResp2.status,404,
		     "Requests to delete a Group that doesn't exist should be rejected");

}

TEST(NonmemberDeleteGroup){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto baseGroupUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups";
	const std::string groupName="testgroup";
	
	//add a VO
	std::string groupID;
	{
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testgroup1", alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(baseGroupUrl+"?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Portal admin user should be able to create a Group");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		groupID=createData["metadata"]["id"].GetString();
	}
	
	std::string tok;
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
		tok=createData["metadata"]["access_token"].GetString();
	}
	
	{ //have the user attempt to delete the Group, despite not being a member
		auto deleteResp=httpDelete(baseGroupUrl+"/"+groupID+"?token="+tok);
		ENSURE_EQUAL(deleteResp.status,403,
		             "A non-admin user should not be able to delete groups to which it does not belong");
	}
}
