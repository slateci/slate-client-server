#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedCreateVO){
	using namespace httpRequests;
	TestContext tc;
	
	//try creating a VO with no authentication
	//doesn't matter whether request body is correct since this should be rejected on other grounds
	auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos","");
	ENSURE_EQUAL(createResp.status,403,
				 "Requests to create VOs without authentication should be rejected");
	
	//try creating a VO with invalid authentication
	createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token=00112233-4455-6677-8899-aabbccddeeff","");
	ENSURE_EQUAL(createResp.status,403,
				 "Requests to create VOs with invalid authentication should be rejected");

}

TEST(CreateVO){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto createVOUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey;
	
	//create a VO
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testvo1", alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp1=httpPost(createVOUrl,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,
				 "First VO creation request should succeed");
	
	ENSURE(!createResp1.body.empty());
	rapidjson::Document respData1;
	respData1.Parse(createResp1.body.c_str());
	
	auto schema=loadSchema(getSchemaDir()+"/VOCreateResultSchema.json");
	ENSURE_CONFORMS(respData1,schema);
	ENSURE_EQUAL(respData1["metadata"]["name"].GetString(),std::string("testvo1"),
	             "VO name should match");
	ENSURE(respData1["metadata"].HasMember("id"));
	ENSURE_EQUAL(respData1["kind"].GetString(),std::string("VO"),
	             "Kind of result should be correct");
}

TEST(MalformedCreateRequests){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=getPortalToken();
	auto createVOUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey;

	{ //invalid JSON request body
		auto createResp=httpPost(createVOUrl, "This is not JSON");
		ENSURE_EQUAL(createResp.status,400,
			     "Invalid JSON requests should be rejected");
	}
	{ //empty JSON
		rapidjson::Document request(rapidjson::kObjectType);
		auto createResp=httpPost(createVOUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Empty JSON requests should be rejected");
	}
	{ //missing metadata
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		auto createResp=httpPost(createVOUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests without metadata section should be rejected");
	}
	{ //missing name
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createVOUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests without a VO name should be rejected");
	}
	{ //wrong name type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", 17, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createVOUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests without a VO name should be rejected");
	}
}
