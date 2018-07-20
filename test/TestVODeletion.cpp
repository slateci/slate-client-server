#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedDeleteVO){
	using namespace httpRequests;
	TestContext tc;
	
	//try deleting a VO with no authentication
	auto createResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/vos/VO_1234567890");
	ENSURE_EQUAL(createResp.status,403,
				 "Requests to delete VOs without authentication should be rejected");
	
	//try deleting a VO with invalid authentication
	createResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/vos/VO_1234567890?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(createResp.status,403,
				 "Requests to delete VOs with invalid authentication should be rejected");

}

TEST(DeleteVO){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=getPortalToken();
	auto baseVOUrl=tc.getAPIServerURL()+"/v1alpha1/vos";
	auto token="?token="+adminKey;
	
	//add a VO
	rapidjson::Document request1(rapidjson::kObjectType);
	{
	  auto& alloc = request1.GetAllocator();
	  request1.AddMember("apiVersion", "v1alpha1", alloc);
	  rapidjson::Value metadata(rapidjson::kObjectType);
	  metadata.AddMember("name", "testvo1", alloc);
	  request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp1=httpPost(baseVOUrl+token,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,"Portal admin user should be able to create a VO");

	ENSURE(!createResp1.body.empty());
	rapidjson::Document respData1;
	respData1.Parse(createResp1.body.c_str());
	ENSURE_EQUAL(respData1["metadata"]["name"].GetString(),std::string("testvo1"),
	             "VO name should match");
	auto id=respData1["metadata"]["id"].GetString();
	
	//delete the just added VO
	auto deleteResp=httpDelete(baseVOUrl+"/"+id+token);
	ENSURE_EQUAL(deleteResp.status,200,"Portal admin user should be able to delete VOs");

	//get list of VOs to check if deleted
	auto listResp=httpGet(baseVOUrl+token);
	ENSURE_EQUAL(listResp.status,200,"Portal admin user should be able to list VOs");
	ENSURE(!listResp.body.empty());
	rapidjson::Document data(rapidjson::kObjectType);
	data.Parse(listResp.body.c_str());
	auto schema=loadSchema("../../slate-portal-api-spec/VOListResultSchema.json");
	ENSURE_CONFORMS(data,schema);

	//check that there are no VOs
	ENSURE_EQUAL(data["items"].Size(),0,"No VO records should be returned");
}

TEST(DeleteNonexistantVO){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=getPortalToken();
	auto baseVOUrl=tc.getAPIServerURL()+"/v1alpha1/vos";
	auto token="?token="+adminKey;

	//try to delete nonexisting VO
	auto deleteResp2=httpDelete(baseVOUrl+"/VO_1234567890"+token);
	ENSURE_EQUAL(deleteResp2.status,404,
		     "Requests to delete a VO that doesn't exist should be rejected");

}
