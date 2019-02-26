#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedGetGroupInfo){
	using namespace httpRequests;
	TestContext tc;
	
	//try fetching Group information with no authentication
	auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/some-group");
	ENSURE_EQUAL(infoResp.status,403,
				 "Requests to get Group info without authentication should be rejected");
	
	//try fetching Group information with invalid authentication
	infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/some-group?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(infoResp.status,403,
				 "Requests to get Group info with invalid authentication should be rejected");
}

TEST(GetGroupInfo){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=getPortalToken();
	
	//add a VO
	const std::string groupName="testgroup1";
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
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
	
	//get Group's info
	auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/"+groupName+"?token="+adminKey);
	ENSURE_EQUAL(infoResp.status,200,"Portal admin user should be able to list groups");

	ENSURE(!infoResp.body.empty());
	rapidjson::Document data;
	data.Parse(infoResp.body.c_str());
	auto schema = loadSchema(getSchemaDir()+"/GroupInfoResultSchema.json");
	ENSURE_CONFORMS(data,schema);
	
	const auto& metadata=data["metadata"];
	ENSURE_EQUAL(metadata["name"].GetString(),groupName,"Group name should match");
	ENSURE_EQUAL(metadata["scienceField"].GetString(),std::string("Logic"),
		     "Group field of science should match");
	ENSURE(metadata.HasMember("id"));
}
