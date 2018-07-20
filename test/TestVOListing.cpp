#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedListVOs){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing VOs with no authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/vos");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list VOs without authentication should be rejected");
	
	//try listing VOs with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/vos?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list VOs with invalid authentication should be rejected");
}

TEST(ListVOs){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=getPortalToken();
	std::string voURL=tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey;

	auto listResp=httpGet(voURL);
	ENSURE_EQUAL(listResp.status,200,"Portal admin user should be able to list VOs");

	ENSURE(!listResp.body.empty());
	rapidjson::Document data;
	data.Parse(listResp.body.c_str());

	auto schema=loadSchema("../../slate-portal-api-spec/VOListResultSchema.json");
	ENSURE_CONFORMS(data,schema);

	//should be no VOs
	ENSURE_EQUAL(data["items"].Size(),0,"No VO records should be returned");
	
	//add a VO
	rapidjson::Document request1(rapidjson::kObjectType);
	{
	  auto& alloc = request1.GetAllocator();
	  request1.AddMember("apiVersion", "v1alpha1", alloc);
	  rapidjson::Value metadata(rapidjson::kObjectType);
	  metadata.AddMember("name", "testvo1", alloc);
	  request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp1=httpPost(voURL,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,"Portal admin user should be able to create a VO");
	
	ENSURE(!createResp1.body.empty());
	rapidjson::Document respData;
	respData.Parse(createResp1.body.c_str());
	auto respSchema = loadSchema("../../slate-portal-api-spec/VOCreateResultSchema.json");
	ENSURE_CONFORMS(respData,respSchema);
	
	//list again
	listResp=httpGet(voURL);
	ENSURE_EQUAL(listResp.status,200,"Portal admin user should be able to list VOs");

	ENSURE(!listResp.body.empty());
	data.Parse(listResp.body.c_str());
	ENSURE_CONFORMS(data,schema);
	
	//should be only one VO
	ENSURE_EQUAL(data["items"].Size(),1,"One VO record should be returned");
	
	const auto& metadata=data["items"][0]["metadata"];
	ENSURE_EQUAL(metadata["name"].GetString(),std::string("testvo1"),
		     "VO name should match");
	ENSURE(metadata.HasMember("id"));

	//add a second VO
	rapidjson::Document request2(rapidjson::kObjectType);
	{
	  auto& alloc = request2.GetAllocator();
	  request2.AddMember("apiVersion", "v1alpha1", alloc);
	  rapidjson::Value metadata(rapidjson::kObjectType);
	  metadata.AddMember("name", "testvo2", alloc);
	  request2.AddMember("metadata", metadata, alloc);
	}
	httpPost(voURL,to_string(request2));

	//list again
	listResp=httpGet(voURL);
	ENSURE_EQUAL(listResp.status,200,"Portal admin user should be able to list VOs");

	ENSURE(!listResp.body.empty());
	data.Parse(listResp.body.c_str());
	ENSURE_CONFORMS(data,schema);

	//should now be two VOs
	ENSURE_EQUAL(data["items"].Size(),2,"Two VO records should be returned");
} 
