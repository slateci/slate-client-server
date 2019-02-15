#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedGetVOInfo){
	using namespace httpRequests;
	TestContext tc;
	
	//try fetching VO information with no authentication
	auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos/some-vo");
	ENSURE_EQUAL(infoResp.status,403,
				 "Requests to get VO info without authentication should be rejected");
	
	//try fetching VO information with invalid authentication
	infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos/some-vo?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(infoResp.status,403,
				 "Requests to get VO info with invalid authentication should be rejected");
}

TEST(GetVOInfo){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=getPortalToken();
	
	//add a VO
	const std::string voName="testvo1";
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp1=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,"Portal admin user should be able to create a VO");
	
	ENSURE(!createResp1.body.empty());
	rapidjson::Document respData;
	respData.Parse(createResp1.body.c_str());
	auto respSchema = loadSchema(getSchemaDir()+"/VOCreateResultSchema.json");
	ENSURE_CONFORMS(respData,respSchema);
	
	//get VO's info
	auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos/"+voName+"?token="+adminKey);
	ENSURE_EQUAL(infoResp.status,200,"Portal admin user should be able to list VOs");

	ENSURE(!infoResp.body.empty());
	rapidjson::Document data;
	data.Parse(infoResp.body.c_str());
	auto schema = loadSchema(getSchemaDir()+"/VOInfoResultSchema.json");
	ENSURE_CONFORMS(data,schema);
	
	const auto& metadata=data["metadata"];
	ENSURE_EQUAL(metadata["name"].GetString(),voName,"VO name should match");
	ENSURE_EQUAL(metadata["scienceField"].GetString(),std::string("Logic"),
		     "VO field of science should match");
	ENSURE(metadata.HasMember("id"));
}
