#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedVOUpdate){
	using namespace httpRequests;
	TestContext tc;
	
	//try fetching VO information with no authentication
	auto updateResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos/some-vo","");
	ENSURE_EQUAL(updateResp.status,403,
				 "Requests to update VO info without authentication should be rejected");
	
	//try fetching VO information with invalid authentication
	updateResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos/some-vo?token=00112233-4455-6677-8899-aabbccddeeff","");
	ENSURE_EQUAL(updateResp.status,403,
				 "Requests to update VO info with invalid authentication should be rejected");
}

TEST(UpdateVO){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=getPortalToken();
	
	//add a VO
	const std::string voName="testvo1";
	const std::string originalEmail="vo1@place.edu";
	const std::string originalPhone="555-5555";
	const std::string originalScienceField="Logic";
	const std::string originalDescription="A VO";
	const std::string newEmail="vo1@someplace.edu";
	const std::string newPhone="555-5556";
	const std::string newScienceField="Botany";
	const std::string newDescription="A science VO";
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		metadata.AddMember("email", originalEmail, alloc);
		metadata.AddMember("phone", originalPhone, alloc);
		metadata.AddMember("scienceField", originalScienceField, alloc);
		metadata.AddMember("description", originalDescription, alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp1=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,"Portal admin user should be able to create a VO");
	
	auto voUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos/"+voName+"?token="+adminKey;
	
	{ //change email
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("email", newEmail, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(voUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,200,"VO email update should succeed");
		
		//get the user info to see if the change was recorded
		auto infoResp=httpGet(voUrl);
		ENSURE_EQUAL(infoResp.status,200,"Getting VO info should succeed");
		ENSURE(!infoResp.body.empty());
		rapidjson::Document infoData;
		infoData.Parse(infoResp.body.c_str());
		ENSURE_EQUAL(infoData["metadata"]["name"].GetString(),voName,
					 "VO name should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["email"].GetString(),newEmail,
		             "VO email should match updated value");
		ENSURE_EQUAL(infoData["metadata"]["phone"].GetString(),originalPhone,
		             "VO phone should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["scienceField"].GetString(),originalScienceField,
		             "VO field of science should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["description"].GetString(),originalDescription,
		             "VO description should remain unchanged");
	}
	{ //change phone
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("phone", newPhone, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(voUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,200,"VO phone update should succeed");
		
		//get the user info to see if the change was recorded
		auto infoResp=httpGet(voUrl);
		ENSURE_EQUAL(infoResp.status,200,"Getting VO info should succeed");
		ENSURE(!infoResp.body.empty());
		rapidjson::Document infoData;
		infoData.Parse(infoResp.body.c_str());
		ENSURE_EQUAL(infoData["metadata"]["name"].GetString(),voName,
					 "VO name should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["email"].GetString(),newEmail,
		             "VO email should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["phone"].GetString(),newPhone,
		             "VO phone should match updated value");
		ENSURE_EQUAL(infoData["metadata"]["scienceField"].GetString(),originalScienceField,
		             "VO field of science should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["description"].GetString(),originalDescription,
		             "VO description should remain unchanged");
	}
	{ //change field of science
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("scienceField", newScienceField, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(voUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,200,"VO science field update should succeed");
		
		//get the user info to see if the change was recorded
		auto infoResp=httpGet(voUrl);
		ENSURE_EQUAL(infoResp.status,200,"Getting VO info should succeed");
		ENSURE(!infoResp.body.empty());
		rapidjson::Document infoData;
		infoData.Parse(infoResp.body.c_str());
		ENSURE_EQUAL(infoData["metadata"]["name"].GetString(),voName,
					 "VO name should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["email"].GetString(),newEmail,
		             "VO email should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["phone"].GetString(),newPhone,
		             "VO phone should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["scienceField"].GetString(),newScienceField,
		             "VO field of science should match updated value");
		ENSURE_EQUAL(infoData["metadata"]["description"].GetString(),originalDescription,
		             "VO description should remain unchanged");
	}
	{ //change description
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("description", newDescription, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(voUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,200,"VO science field update should succeed");
		
		//get the user info to see if the change was recorded
		auto infoResp=httpGet(voUrl);
		ENSURE_EQUAL(infoResp.status,200,"Getting VO info should succeed");
		ENSURE(!infoResp.body.empty());
		rapidjson::Document infoData;
		infoData.Parse(infoResp.body.c_str());
		ENSURE_EQUAL(infoData["metadata"]["name"].GetString(),voName,
					 "VO name should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["email"].GetString(),newEmail,
		             "VO email should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["phone"].GetString(),newPhone,
		             "VO phone should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["scienceField"].GetString(),newScienceField,
		             "VO field of science should remain unchanged");
		ENSURE_EQUAL(infoData["metadata"]["description"].GetString(),newDescription,
		             "VO description should match updated value");
	}
}

TEST(MalformedUpdateVO){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=getPortalToken();
	
	//add a VO
	const std::string voName="testvo1";
	const std::string originalEmail="vo1@place.edu";
	const std::string originalPhone="555-5555";
	const std::string originalScienceField="Logic";
	const std::string originalDescription="A VO";
	const std::string newEmail="vo1@someplace.edu";
	const std::string newPhone="555-5556";
	const std::string newScienceField="Botany";
	const std::string newDescription="A science VO";
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		metadata.AddMember("email", originalEmail, alloc);
		metadata.AddMember("phone", originalPhone, alloc);
		metadata.AddMember("scienceField", originalScienceField, alloc);
		metadata.AddMember("description", originalDescription, alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp1=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,"Portal admin user should be able to create a VO");
	
	auto voUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos/"+voName+"?token="+adminKey;
	
	{ //invalid JSON request body
		auto updateResp=httpPut(voUrl,"This is not JSON");
		ENSURE_EQUAL(updateResp.status,400,"Invalid JSON requests should be rejected");
	}
	{ //empty JSON
		rapidjson::Document request(rapidjson::kObjectType);
		auto updateResp=httpPut(voUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Empty JSON requests should be rejected");
	}
	{ //missing metadata
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		auto updateResp=httpPut(voUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Requests without metadata section should be rejected");
	}
	{ //change email
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("email", 4, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(voUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Update with wrong type for email should fail");
	}
	{ //change phone
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("phone", false, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(voUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Update with wrong type for phone should fail");
	}
	{ //change field of science
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("scienceField", 22, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(voUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Update with wrong type for science field should fail");
	}
	{ //change field of science
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("scienceField", "Phrenology", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(voUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Update with unrecognized science field should fail");
	}
	{ //change description of science
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("description", 89, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto updateResp=httpPut(voUrl,to_string(request));
		ENSURE_EQUAL(updateResp.status,400,"Update with wrong type for description should fail");
	}
}
