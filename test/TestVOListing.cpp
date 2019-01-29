#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedListVOs){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing VOs with no authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list VOs without authentication should be rejected");
	
	//try listing VOs with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list VOs with invalid authentication should be rejected");
}

TEST(ListVOs){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=getPortalToken();
	std::string voURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey;

	auto listResp=httpGet(voURL);
	ENSURE_EQUAL(listResp.status,200,"Portal admin user should be able to list VOs");

	ENSURE(!listResp.body.empty());
	rapidjson::Document data;
	data.Parse(listResp.body.c_str());

	auto schema=loadSchema(getSchemaDir()+"/VOListResultSchema.json");
	ENSURE_CONFORMS(data,schema);

	//should be no VOs
	ENSURE_EQUAL(data["items"].Size(),0,"No VO records should be returned");
	
	//add a VO
	rapidjson::Document request1(rapidjson::kObjectType);
	{
	  auto& alloc = request1.GetAllocator();
	  request1.AddMember("apiVersion", currentAPIVersion, alloc);
	  rapidjson::Value metadata(rapidjson::kObjectType);
	  metadata.AddMember("name", "testvo1", alloc);
	  request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp1=httpPost(voURL,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,"Portal admin user should be able to create a VO");
	
	ENSURE(!createResp1.body.empty());
	rapidjson::Document respData;
	respData.Parse(createResp1.body.c_str());
	auto respSchema = loadSchema(getSchemaDir()+"/VOCreateResultSchema.json");
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
	  request2.AddMember("apiVersion", currentAPIVersion, alloc);
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

TEST(ListVOsForUser){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=getPortalToken();

	auto schema=loadSchema(getSchemaDir()+"/VOListResultSchema.json");

	//add a regular user
	rapidjson::Document createUser(rapidjson::kObjectType);
	{
		auto& alloc = createUser.GetAllocator();
	        createUser.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		createUser.AddMember("metadata", metadata, alloc);
	}
	auto userResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(createUser));
	ENSURE_EQUAL(userResp.status,200,"Portal admin user should be able to create a regular user");
	ENSURE(!userResp.body.empty());
	rapidjson::Document userData;
	userData.Parse(userResp.body.c_str());
	
	auto userSchema=loadSchema(getSchemaDir()+"/UserInfoResultSchema.json");
	ENSURE_CONFORMS(userData, userSchema);

	auto userKey=userData["metadata"]["access_token"].GetString();
	
	//add a VO
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testvo1", alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp1=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,"Portal admin user should be able to create a VO");
	
	ENSURE(!createResp1.body.empty());
	rapidjson::Document respData;
	respData.Parse(createResp1.body.c_str());
	auto respSchema = loadSchema(getSchemaDir()+"/VOCreateResultSchema.json");
	ENSURE_CONFORMS(respData,respSchema);

	//list VOs for all users
	auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey+"&user=true");
	ENSURE_EQUAL(listResp.status,200,"Portal admin user should be able to list VOs");

	ENSURE(!listResp.body.empty());
	rapidjson::Document data;
	data.Parse(listResp.body.c_str());
	ENSURE_CONFORMS(data,schema);
	
	//should be only one VO
	ENSURE_EQUAL(data["items"].Size(),1,"One VO record should be returned");
	
	const auto& metadata=data["items"][0]["metadata"];
	ENSURE_EQUAL(metadata["name"].GetString(),std::string("testvo1"),
		     "VO name should match");
	ENSURE(metadata.HasMember("id"));
	
	//list VOs for just the admin user
	listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+adminKey+"&user=true");
	ENSURE_EQUAL(listResp.status,200,"Portal admin user should be able to list VOs");

	ENSURE(!listResp.body.empty());
	data.Parse(listResp.body.c_str());
	ENSURE_CONFORMS(data,schema);
	
	//should be only one VO
	ENSURE_EQUAL(data["items"].Size(),1,"One VO record should be returned");
	
	const auto& adminMetadata=data["items"][0]["metadata"];
	ENSURE_EQUAL(adminMetadata["name"].GetString(),std::string("testvo1"),
		     "VO name should match");
	ENSURE(adminMetadata.HasMember("id"));

	//list VOs for just the regular user
	listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/vos?token="+userKey+"&user=true");
	ENSURE_EQUAL(listResp.status,200,"Regular user should be able to list VOs");

	ENSURE(!listResp.body.empty());
	data.Parse(listResp.body.c_str());
	ENSURE_CONFORMS(data,schema);

	//should be no VOs
	ENSURE_EQUAL(data["items"].Size(),0,"No VO records should be returned for regular user");
}
