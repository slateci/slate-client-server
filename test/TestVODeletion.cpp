#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedDeleteVO){
	using namespace httpRequests;
	TestContext tc;
	
	//try deleting a VO with no authentication
	auto createResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos/VO_1234567890");
	ENSURE_EQUAL(createResp.status,403,
				 "Requests to delete VOs without authentication should be rejected");
	
	//try deleting a VO with invalid authentication
	createResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos/VO_1234567890?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(createResp.status,403,
				 "Requests to delete VOs with invalid authentication should be rejected");

}

TEST(DeleteVO){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=getPortalToken();
	auto baseVOUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos";
	auto token="?token="+adminKey;
	
	//add a VO
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testvo1", alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
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
	auto schema=loadSchema(getSchemaDir()+"/VOListResultSchema.json");
	ENSURE_CONFORMS(data,schema);

	//check that there are no VOs
	ENSURE_EQUAL(data["items"].Size(),0,"No VO records should be returned");
}

TEST(DeleteNonexistantVO){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=getPortalToken();
	auto baseVOUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos";
	auto token="?token="+adminKey;

	//try to delete nonexisting VO
	auto deleteResp2=httpDelete(baseVOUrl+"/VO_1234567890"+token);
	ENSURE_EQUAL(deleteResp2.status,404,
		     "Requests to delete a VO that doesn't exist should be rejected");

}

TEST(NonmemberDeleteVO){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto baseVOUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos";
	const std::string voName="testvo";
	
	//add a VO
	std::string voID;
	{
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testvo1", alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(baseVOUrl+"?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Portal admin user should be able to create a VO");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		voID=createData["metadata"]["id"].GetString();
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
	
	{ //have the user attempt to delete the VO, despite not being a member
		auto deleteResp=httpDelete(baseVOUrl+"/"+voID+"?token="+tok);
		ENSURE_EQUAL(deleteResp.status,403,
		             "A non-admin user should not be able to delete VOs to which it does not belong");
	}
}
