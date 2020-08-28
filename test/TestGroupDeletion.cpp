#include "test.h"

#include <Archive.h>
#include <ServerUtilities.h>
#include <PersistentStore.h>
#include <Process.h>
#include <KubeInterface.h>
#include <Entities.h>
#include <iostream>


TEST(UnauthenticatedDeleteGroup){
	using namespace httpRequests;
	TestContext tc;
	
	//try deleting a Group with no authentication
	auto createResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/Group_1234567890");
	ENSURE_EQUAL(createResp.status,403,
				 "Requests to delete groups without authentication should be rejected");
	
	//try deleting a Group with invalid authentication
	createResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups/Group_1234567890?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(createResp.status,403,
				 "Requests to delete groups with invalid authentication should be rejected");

}

TEST(DeleteGroup){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=tc.getPortalToken();
	auto baseGroupUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups";
	auto token="?token="+adminKey;
	
	//add a VO
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testgroup1", alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp1=httpPost(baseGroupUrl+token,to_string(request1));
	ENSURE_EQUAL(createResp1.status,200,"Portal admin user should be able to create a Group");

	ENSURE(!createResp1.body.empty());
	rapidjson::Document respData1;
	respData1.Parse(createResp1.body.c_str());
	ENSURE_EQUAL(respData1["metadata"]["name"].GetString(),std::string("testgroup1"),
	             "Group name should match");
	auto id=respData1["metadata"]["id"].GetString();
	
	//delete the just added VO
	auto deleteResp=httpDelete(baseGroupUrl+"/"+id+token);
	ENSURE_EQUAL(deleteResp.status,200,"Portal admin user should be able to delete groups");

	//get list of groups to check if deleted
	auto listResp=httpGet(baseGroupUrl+token);
	ENSURE_EQUAL(listResp.status,200,"Portal admin user should be able to list groups");
	ENSURE(!listResp.body.empty());
	rapidjson::Document data(rapidjson::kObjectType);
	data.Parse(listResp.body.c_str());
	auto schema=loadSchema(getSchemaDir()+"/GroupListResultSchema.json");
	ENSURE_CONFORMS(data,schema);

	//check that there are no groups
	ENSURE_EQUAL(data["items"].Size(),0,"No Group records should be returned");
}

TEST(DeleteNonexistantGroup){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=tc.getPortalToken();
	auto baseGroupUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups";
	auto token="?token="+adminKey;

	//try to delete nonexisting VO
	auto deleteResp2=httpDelete(baseGroupUrl+"/Group_1234567890"+token);
	ENSURE_EQUAL(deleteResp2.status,404,
		     "Requests to delete a Group that doesn't exist should be rejected");

}

TEST(NonmemberDeleteGroup){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	auto baseGroupUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups";
	const std::string groupName="testgroup";
	
	//add a VO
	std::string groupID;
	{
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testgroup1", alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(baseGroupUrl+"?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Portal admin user should be able to create a Group");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		groupID=createData["metadata"]["id"].GetString();
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
	
	{ //have the user attempt to delete the Group, despite not being a member
		auto deleteResp=httpDelete(baseGroupUrl+"/"+groupID+"?token="+tok);
		ENSURE_EQUAL(deleteResp.status,403,
		             "A non-admin user should not be able to delete groups to which it does not belong");
	}
}

TEST(DeletingGroupHasCascadingDeletion){
	// Make a, VO, cluster, instance, and secrets
	// Then verify the latter were deleted as a consequence of deleting the cluster
	using namespace httpRequests;
	TestContext tc;	
	std::string adminKey=tc.getPortalToken();

	// create Group to register cluster with
	const std::string groupName="testgroup1";
	rapidjson::Document createGroup(rapidjson::kObjectType);
	{
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
	}
	auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
			     to_string(createGroup));
	ENSURE_EQUAL(groupResp.status,200,"Group creation request should succeed");
	rapidjson::Document groupData;
	groupData.Parse(groupResp.body.c_str());
	auto groupID=groupData["metadata"]["id"].GetString();	

	auto kubeConfig = tc.getKubeConfig();

	// create the cluster
	const std::string clusterName="testcluster";
	auto createClusterUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey;
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("group", rapidjson::StringRef(groupID), alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp=httpPost(createClusterUrl, to_string(request1));
	ENSURE_EQUAL(createResp.status,200,
		     "Cluster creation request should succeed");
	rapidjson::Document createData;
	createData.Parse(createResp.body);
	auto clusterID=createData["metadata"]["id"].GetString();

	std::string instID;
	{ // install an instance
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		if(data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("id"))
			instID=data["metadata"]["id"].GetString();
	}

	const std::string secretName="createsecret-secret1";
	std::string secretID;
	struct cleanupHelper{
		TestContext& tc;
		const std::string& id, key;
		cleanupHelper(TestContext& tc, const std::string& id, const std::string& key):
		tc(tc),id(id),key(key){}
		~cleanupHelper(){
			if(!id.empty())
				auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets/"+id+"?token="+key);
		}
	} cleanup(tc,secretID,adminKey);
	
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets?token="+adminKey;
	{   // install a secret
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", secretName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", encodeBase64("bar"), alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Secret creation should succeed: "+createResp.body);
		rapidjson::Document data;
		data.Parse(createResp.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/SecretCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		secretID=data["metadata"]["id"].GetString();
	}

	// perform the deletion
	auto baseGroupUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups";
	auto deleteResp=httpDelete(baseGroupUrl+"/"+groupID+"?token="+adminKey);
	ENSURE_EQUAL(deleteResp.status,200,"Portal admin user should be able to delete groups");
	
	// verify that everything else was deleted, too
	DatabaseContext db;
	auto storePtr=db.makePersistentStore();
	auto& store=*storePtr;

	auto instance = store.getApplicationInstance(instID);
	auto secret = store.getSecret(secretID);
	auto cluster = store.getCluster(clusterID);
	ENSURE_EQUAL(instance, ApplicationInstance(), "VO deletion should delete instances");
	ENSURE_EQUAL(secret, Secret(), "VO deletion should delete secrets");
	ENSURE_EQUAL(cluster, Cluster(), "VO deletion should delete clusters");

	// Get kubeconfig, save it to file, and use it to check namespaces
	std::string conf = tc.getKubeConfig();
	std::ofstream out("testgroupconfigdeletion.yaml");
	out << conf;
	out.close();
	std::vector<std::string> args = {"get", "namespaces"};
	auto names = kubernetes::kubectl("./testconfigdeletion.yaml", args);
	ENSURE_EQUAL(names.output.find("slate-group-testgroup1"), std::string::npos, "VO deletion should delete associated namespaces");
}
