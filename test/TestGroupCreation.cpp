#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedCreateGroup){
	using namespace httpRequests;
	TestContext tc;
	
	//try creating a Group with no authentication
	//doesn't matter whether request body is correct since this should be rejected on other grounds
	auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups","");
	ENSURE_EQUAL(createResp.status,403,
				 "Requests to create groups without authentication should be rejected");
	
	//try creating a Group with invalid authentication
	createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token=00112233-4455-6677-8899-aabbccddeeff","");
	ENSURE_EQUAL(createResp.status,403,
				 "Requests to create groups with invalid authentication should be rejected");

}

TEST(CreateGroup){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto createGroupUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey;
	
	//create a VO
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testgroup1", alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp1=httpPost(createGroupUrl,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,
				 "Group creation request should succeed");
	
	ENSURE(!createResp1.body.empty());
	rapidjson::Document respData1;
	respData1.Parse(createResp1.body.c_str());
	
	auto schema=loadSchema(getSchemaDir()+"/GroupCreateResultSchema.json");
	ENSURE_CONFORMS(respData1,schema);
	ENSURE_EQUAL(respData1["metadata"]["name"].GetString(),std::string("testgroup1"),
	             "Group name should match");
	ENSURE(respData1["metadata"].HasMember("id"));
	ENSURE_EQUAL(respData1["kind"].GetString(),std::string("Group"),
	             "Kind of result should be correct");
}

TEST(CreateGroupDifferentFieldCapitalization){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto createGroupUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey;
	
	//create a VO
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testgroup1", alloc);
		metadata.AddMember("scienceField", "loGic", alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp1=httpPost(createGroupUrl,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,
				 "Group creation request should succeed");
	
	ENSURE(!createResp1.body.empty());
	rapidjson::Document respData1;
	respData1.Parse(createResp1.body.c_str());
	
	auto schema=loadSchema(getSchemaDir()+"/GroupCreateResultSchema.json");
	ENSURE_CONFORMS(respData1,schema);
	ENSURE_EQUAL(respData1["metadata"]["name"].GetString(),std::string("testgroup1"),
	             "Group name should match");
	ENSURE(respData1["metadata"].HasMember("id"));
	ENSURE_EQUAL(respData1["kind"].GetString(),std::string("Group"),
	             "Kind of result should be correct");
}

TEST(MalformedCreateRequests){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=getPortalToken();
	auto createGroupUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey;

	{ //invalid JSON request body
		auto createResp=httpPost(createGroupUrl, "This is not JSON");
		ENSURE_EQUAL(createResp.status,400,
			     "Invalid JSON requests should be rejected");
	}
	{ //empty JSON
		rapidjson::Document request(rapidjson::kObjectType);
		auto createResp=httpPost(createGroupUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Empty JSON requests should be rejected");
	}
	{ //missing metadata
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		auto createResp=httpPost(createGroupUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests without metadata section should be rejected");
	}
	{ //missing name
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createGroupUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests without a Group name should be rejected");
	}
	{ //wrong name type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", 17, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createGroupUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests the wrong type of Group name should be rejected");
	}
	{ //missing scienceField
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "voname", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createGroupUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests without a field of science should be rejected");
	}
	{ //wrong scienceField type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "voname", alloc);
		metadata.AddMember("scienceField", 11, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createGroupUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests the wrong type of field of science should be rejected");
	}
	{ //unexpected scienceField value
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "voname", alloc);
		metadata.AddMember("scienceField", "Phrenology", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createGroupUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests unknown fields of science should be rejected");
	}
}
