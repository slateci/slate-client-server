#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedCreateCluster){
	using namespace httpRequests;
	TestContext tc;

	//try creating a cluster with no authentication
	auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/clusters","");
	ENSURE_EQUAL(createResp.status,403,
		     "Requests to create a cluster without authentication should be rejected.");

	//try creating a cluster with invalid authentication
	createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/clusters?token=00112233-4455-6677-8899-aabbccddeeff","");
	ENSURE_EQUAL(createResp.status,403,
		     "Requests to create a cluster with invalid authentication should be rejected");
}

TEST(CreateCluster){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto createClusterUrl=tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey;

	// create VO to register cluster with
	rapidjson::Document createVO(rapidjson::kObjectType);
	{
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testvo1", alloc);
		createVO.AddMember("metadata", metadata, alloc);
	}
	auto voResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,
			     to_string(createVO));
	ENSURE_EQUAL(voResp.status,200,"VO creation request should succeed");
	ENSURE(!voResp.body.empty());
	rapidjson::Document voData;
	voData.Parse(voResp.body.c_str());
	auto voID=voData["metadata"]["id"].GetString();	

	auto kubeConfig = tc.getKubeConfig();

	//create cluster
	rapidjson::Document request1(rapidjson::kObjectType);
	{
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("vo", rapidjson::StringRef(voID), alloc);
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
	auto createClusterUrl=tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey;

	// create VO to register cluster with
	rapidjson::Document createVO(rapidjson::kObjectType);
	{
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testvo1", alloc);
		createVO.AddMember("metadata", metadata, alloc);
	}
	auto voResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,
			     to_string(createVO));
	ENSURE_EQUAL(voResp.status,200,
		     "VO creation request should succeed");
	ENSURE(!voResp.body.empty());
	rapidjson::Document voData;
	voData.Parse(voResp.body.c_str());
	auto voID=voData["metadata"]["id"].GetString();
       
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
		request.AddMember("apiVersion", "v1alpha1", alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests without metadata section should be rejected");
	}
	{ //missing name
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("vo", rapidjson::StringRef(voID), alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests missing a cluster name should be rejected");
	}
	{ //wrong name type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", 17, alloc);
		metadata.AddMember("vo", rapidjson::StringRef(voID), alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests without a valid cluster name should be rejected");
	}
	{ //missing vo
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests without a VO id should be rejected");
	}
	{ //wrong vo type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("vo", true, alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests with an invalid VO id should be rejected");
	}
	{ //missing kubeconfig
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("vo", rapidjson::StringRef(voID), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests without a kubeconfig should be rejected");
	}
	{ //wrong kubeconfig type
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("vo", rapidjson::StringRef(voID), alloc);
		metadata.AddMember("kubeconfig", 17, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(createClusterUrl, to_string(request));
		ENSURE_EQUAL(createResp.status,400,
			     "Requests with invalid kubeconfig should be rejected");
	}
}

