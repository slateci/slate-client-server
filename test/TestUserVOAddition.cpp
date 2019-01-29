#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedAddUserToVO){
	using namespace httpRequests;
	TestContext tc;
	
	//try adding a user to a VO with no authentication
	//doesn't matter whether request body is correct since this should be rejected on other grounds
	auto addResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/User_ABC/vos/VO_123","");
	ENSURE_EQUAL(addResp.status,403,
				 "Requests to add users to VOs without authentication should be rejected");
	
	//try adding a user to a VO with invalid authentication
	addResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/User_ABC/vos/VO_123?token=00112233-4455-6677-8899-aabbccddeeff","");
	ENSURE_EQUAL(addResp.status,403,
				 "Requests to add users to VOs with invalid authentication should be rejected");
}

TEST(AddUserToVO){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName="some-org";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	std::string uid;
	{ //create a user
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		uid=createData["metadata"]["id"].GetString();
	}
	
	{ //add the user to the VO
		auto addResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos/"+voName+"?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,200,"User addition to VO request should succeed");
	}
	
	{ //check that the user is in the VO
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"?token="+adminKey);
		ENSURE_EQUAL(infoResp.status,200,"Getting user's information should succeed");
		std::cout << infoResp.body << std::endl;
		rapidjson::Document data;
		data.Parse(infoResp.body);
		auto schema=loadSchema(getSchemaDir()+"/UserInfoResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["metadata"]["VOs"].Size(),1,"User should belong to one VO");
		ENSURE_EQUAL(data["metadata"]["VOs"][0].GetString(),voName,"User should belong to the correct VO");
	}
}

TEST(AddUserToNonexistentVO){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	
	std::string uid;
	{ //create a user
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		uid=createData["metadata"]["id"].GetString();
	}
	
	std::string voName="some-org";
	
	{ //attempt to add the user to a VO which does not exist
		auto addResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos/"+voName+"?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,404,
		             "User addition to non-existent VO request should be rejected");
	}
}

TEST(AddNonexistentUserToVO){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	
	std::string uid="User_2375627864987598275";
	std::string voName="some-org";
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	{ //attempt to add a nonexistent user to the VO
		auto addResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos/"+voName+"?token="+adminKey,"");
		ENSURE_EQUAL(addResp.status,404,
		             "Request to add non-existent user to a VO should be rejected");
	}
}

TEST(NonmemberAddUserToVO){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName="some-org";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	std::string uid;
	std::string tok;
	{ //create a user
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		uid=createData["metadata"]["id"].GetString();
		tok=createData["metadata"]["access_token"].GetString();
	}
	
	{ //have the new user attempt to add itself to the VO
		auto addResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"/vos/"+voName+"?token="+tok,"");
		ENSURE_EQUAL(addResp.status,403,
		             "User addition to VO request by non-member should be rejected");
	}
	
	{ //check that the user is not in the VO
		auto infoResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users/"+uid+"?token="+adminKey);
		ENSURE_EQUAL(infoResp.status,200,"Getting user's information should succeed");
		rapidjson::Document data;
		data.Parse(infoResp.body);
		auto schema=loadSchema(getSchemaDir()+"/UserInfoResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["metadata"]["VOs"].Size(),0,"User should not belong to the VO");
	}
}
