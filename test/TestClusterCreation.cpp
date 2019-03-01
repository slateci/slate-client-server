#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedCreateCluster){
	using namespace httpRequests;
	TestContext tc;

	//try creating a cluster with no authentication
	auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters","");
	ENSURE_EQUAL(createResp.status,403,
		     "Requests to create a cluster without authentication should be rejected.");

	//try creating a cluster with invalid authentication
	createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token=00112233-4455-6677-8899-aabbccddeeff","");
	ENSURE_EQUAL(createResp.status,403,
		     "Requests to create a cluster with invalid authentication should be rejected");
}

TEST(CreateCluster){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto createClusterUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey;

	// create Group to register cluster with
	rapidjson::Document createGroup(rapidjson::kObjectType);
	{
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testgroup1", alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
	}
	auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
			     to_string(createGroup));
	ENSURE_EQUAL(groupResp.status,200,"Group creation request should succeed");
	ENSURE(!groupResp.body.empty());
	rapidjson::Document groupData;
	groupData.Parse(groupResp.body.c_str());
	auto groupID=groupData["metadata"]["id"].GetString();	

	auto kubeConfig = tc.getKubeConfig();

	//create cluster
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", rapidjson::StringRef(groupID), alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
	}
	auto createResp=httpPost(createClusterUrl, to_string(request1));
	ENSURE_EQUAL(createResp.status,200,
		     "Cluster creation request should succeed");
	ENSURE(!createResp.body.empty());
	rapidjson::Document createData;
	createData.Parse(createResp.body.c_str());

	auto schema=loadSchema(getSchemaDir()+"/ClusterCreateResultSchema.json");
	ENSURE_CONFORMS(createData,schema);
	ENSURE_EQUAL(createData["metadata"]["name"].GetString(),std::string("testcluster"),
		     "Cluster name should match");
	ENSURE(createData["metadata"].HasMember("id"));
	ENSURE_EQUAL(createData["kind"].GetString(),std::string("Cluster"),
		     "Kind of result should be Cluster");
}

TEST(MalformedCreateRequests){
	using namespace httpRequests;
	TestContext tc;

	std::string adminKey=getPortalToken();
	auto createClusterUrl=tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey;

	// create Group to register cluster with
	rapidjson::Document createGroup(rapidjson::kObjectType);
	{
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testgroup1", alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
	}
	auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
			     to_string(createGroup));
	ENSURE_EQUAL(groupResp.status,200,
		     "Group creation request should succeed");
	ENSURE(!groupResp.body.empty());
	rapidjson::Document groupData;
	groupData.Parse(groupResp.body.c_str());
	auto groupID=groupData["metadata"]["id"].GetString();
       
	auto kubeConfig="";
	
	{ //invalid JSON request body
		auto createResp=httpPost(createClusterUrl, "This is not JSON");
		ENSURE_EQUAL(createResp.status,400, "Invalid JSON requests should be rejected");
	}
	{ //empty JSON
		rapidjson::Document request(rapidjson::kObjectType);
		auto createResp=httpPost(createClusterUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Empty JSON requests should be rejected");
	}
	{ //missing metadata
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests without metadata section should be rejected");
	}
	{ //missing name
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("group", rapidjson::StringRef(groupID), alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests missing a cluster name should be rejected");
	}
	{ //wrong name type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", 17, alloc);
		metadata.AddMember("group", rapidjson::StringRef(groupID), alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests without a valid cluster name should be rejected");
	}
	{ //missing vo
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests without a Group id should be rejected");
	}
	{ //wrong group type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", true, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests with an invalid Group id should be rejected");
	}
	{ //missing organization
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", rapidjson::StringRef(groupID), alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests without an organization should be rejected");
	}
	{ //wrong organization type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", rapidjson::StringRef(groupID), alloc);
		metadata.AddMember("owningOrganization", 18, alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests with an invalid organization should be rejected");
	}
	{ //missing kubeconfig
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", rapidjson::StringRef(groupID), alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests without a kubeconfig should be rejected");
	}
	{ //wrong kubeconfig type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", rapidjson::StringRef(groupID), alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", 17, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests with invalid kubeconfig should be rejected");
	}
}

