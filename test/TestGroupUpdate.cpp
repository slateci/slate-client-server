#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedGroupUpdate){
	using namespace httpRequests;
	TestContext tc;
	
	//try fetching Group information with no authentication
	auto updateResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/some-group","");
	ENSURE_EQUAL(updateResp.status,403,
				 "Requests to update Group info without authentication should be rejected");
	
	//try fetching Group information with invalid authentication
	updateResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/some-group?token=00112233-4455-6677-8899-aabbccddeeff","");
	ENSURE_EQUAL(updateResp.status,403,
				 "Requests to update Group info with invalid authentication should be rejected");
}

TEST(UpdateGroup){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=tc.getPortalToken();
	
	//add a VO
	const std::string groupName="testgroup1";
	const std::string originalEmail="group1@place.edu";
	const std::string originalPhone="555-5555";
	const std::string originalScienceField="Logic";
	const std::string originalDescription="A Group";
	const std::string newEmail="group1@someplace.edu";
	const std::string newPhone="555-5556";
	const std::string newScienceField="Botany";
	const std::string newDescription="A science Group";
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("email", originalEmail, alloc);
		metadata.AddMember("phone", originalPhone, alloc);
		metadata.AddMember("scienceField", originalScienceField, alloc);
		metadata.AddMember("description", originalDescription, alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp1=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,"Portal admin user should be able to create a Group");
	
	auto groupUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/"+groupName+"?token="+adminKey;
	
	{ //change email
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("email", newEmail, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(groupUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,200,"Group email update should succeed");
		
		//get the user info to see if the change was recorded
		auto infoResp=httpGet(groupUrl);
		ENSURE_EQUAL(infoResp.status,200,"Getting Group info should succeed");
		ENSURE(!infoResp.body.empty());
		rapidjson::Document infoData;
		infoData.Parse(infoResp.body.c_str());
		ENSURE_EQUAL(infoData["metadata"]["name"].GetString(),groupName,
					 "Group name should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["email"].GetString(),newEmail,
		             "Group email should match updated value");
		ENSURE_EQUAL(infoData["metadata"]["phone"].GetString(),originalPhone,
		             "Group phone should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["scienceField"].GetString(),originalScienceField,
		             "Group field of science should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["description"].GetString(),originalDescription,
		             "Group description should remain unchanged");
	}
	{ //change phone
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("phone", newPhone, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(groupUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,200,"Group phone update should succeed");
		
		//get the user info to see if the change was recorded
		auto infoResp=httpGet(groupUrl);
		ENSURE_EQUAL(infoResp.status,200,"Getting Group info should succeed");
		ENSURE(!infoResp.body.empty());
		rapidjson::Document infoData;
		infoData.Parse(infoResp.body.c_str());
		ENSURE_EQUAL(infoData["metadata"]["name"].GetString(),groupName,
					 "Group name should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["email"].GetString(),newEmail,
		             "Group email should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["phone"].GetString(),newPhone,
		             "Group phone should match updated value");
		ENSURE_EQUAL(infoData["metadata"]["scienceField"].GetString(),originalScienceField,
		             "Group field of science should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["description"].GetString(),originalDescription,
		             "Group description should remain unchanged");
	}
	{ //change field of science
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("scienceField", newScienceField, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(groupUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,200,"Group science field update should succeed");
		
		//get the user info to see if the change was recorded
		auto infoResp=httpGet(groupUrl);
		ENSURE_EQUAL(infoResp.status,200,"Getting Group info should succeed");
		ENSURE(!infoResp.body.empty());
		rapidjson::Document infoData;
		infoData.Parse(infoResp.body.c_str());
		ENSURE_EQUAL(infoData["metadata"]["name"].GetString(),groupName,
					 "Group name should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["email"].GetString(),newEmail,
		             "Group email should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["phone"].GetString(),newPhone,
		             "Group phone should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["scienceField"].GetString(),newScienceField,
		             "Group field of science should match updated value");
		ENSURE_EQUAL(infoData["metadata"]["description"].GetString(),originalDescription,
		             "Group description should remain unchanged");
	}
	{ //change description
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("description", newDescription, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(groupUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,200,"Group science field update should succeed");
		
		//get the user info to see if the change was recorded
		auto infoResp=httpGet(groupUrl);
		ENSURE_EQUAL(infoResp.status,200,"Getting Group info should succeed");
		ENSURE(!infoResp.body.empty());
		rapidjson::Document infoData;
		infoData.Parse(infoResp.body.c_str());
		ENSURE_EQUAL(infoData["metadata"]["name"].GetString(),groupName,
					 "Group name should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["email"].GetString(),newEmail,
		             "Group email should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["phone"].GetString(),newPhone,
		             "Group phone should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["scienceField"].GetString(),newScienceField,
		             "Group field of science should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["description"].GetString(),newDescription,
		             "Group description should match updated value");
	}
}

TEST(MalformedUpdateGroup){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=tc.getPortalToken();
	
	//add a VO
	const std::string groupName="testgroup1";
	const std::string originalEmail="group1@place.edu";
	const std::string originalPhone="555-5555";
	const std::string originalScienceField="Logic";
	const std::string originalDescription="A Group";
	const std::string newEmail="group1@someplace.edu";
	const std::string newPhone="555-5556";
	const std::string newScienceField="Botany";
	const std::string newDescription="A science Group";
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("email", originalEmail, alloc);
		metadata.AddMember("phone", originalPhone, alloc);
		metadata.AddMember("scienceField", originalScienceField, alloc);
		metadata.AddMember("description", originalDescription, alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp1=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,"Portal admin user should be able to create a Group");
	
	auto groupUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/"+groupName+"?token="+adminKey;
	
	{ //invalid JSON request body
		auto updateResp=httpPut(groupUrl,"This is not JSON");
		ENSURE_EQUAL(updateResp.status,400,"Invalid JSON requests should be rejected");
	}
	{ //empty JSON
		rapidjson::Document request(rapidjson::kObjectType);
		auto updateResp=httpPut(groupUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Empty JSON requests should be rejected");
	}
	{ //missing metadata
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		auto updateResp=httpPut(groupUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Requests without metadata section should be rejected");
	}
	{ //change email
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("email", 4, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(groupUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Update with wrong type for email should fail");
	}
	{ //change phone
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("phone", false, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(groupUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Update with wrong type for phone should fail");
	}
	{ //change field of science
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("scienceField", 22, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(groupUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Update with wrong type for science field should fail");
	}
	{ //change field of science
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("scienceField", "Phrenology", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(groupUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Update with unrecognized science field should fail");
	}
	{ //change description of science
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("description", 89, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(groupUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Update with wrong type for description should fail");
	}
}
