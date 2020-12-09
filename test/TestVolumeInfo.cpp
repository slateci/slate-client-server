#include "test.h"

#include <ServerUtilities.h>
#include <KubeInterface.h>
#include <Logging.h>

TEST(UnauthenticatedGetVolumeInfo){
	using namespace httpRequests;
	TestContext tc;

	//try creating a volume claim with no authentication
	auto createResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/some-volume");
	ENSURE_EQUAL(createResp.status,403,
	            "Requests to delete volumes without authentication should be rejected");

	//try creating a secret with invalid authentication
	createResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/some-volume?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(createResp.status,403,
	            "Requests to delete volumes with invalid authentication should be rejected");
}

TEST(GetVolumeInfo){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string volumesURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/VolumeInfoResultSchema.json");
	
	//create a group
	const std::string groupName="test-info-volumes-group";
	{
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResponse=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
		                     to_string(createGroup));
		ENSURE_EQUAL(groupResponse.status,200, "Group creation request should succeed");

	}

	//register a cluster
	const std::string clusterName="testcluster";
	{
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("owningOrganization", "University of Utah", alloc);
		metadata.AddMember("kubeconfig", tc.getKubeConfig(), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto clusterResponse=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(clusterResponse.status,200, "Cluster creation should succeed");
	}

	//create a volume claim
	const std::string firstVolumeName="createvolumetest";
	std::string volumeID;
	std::vector<std::string> labelExpressions;
	labelExpressions.push_back("key: value");
	// labelExpressions.push_back("operator: In");
	// labelExpressions.push_back("production");
	
	{
		// build up the request
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", firstVolumeName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		metadata.AddMember("storageRequest", "10Mi", alloc);
		metadata.AddMember("accessMode", "ReadWriteOnce", alloc);
		metadata.AddMember("volumeMode", "Filesystem", alloc);
		// minikube provides a default storage class called 'standard'
		metadata.AddMember("storageClass", "standard", alloc);
		metadata.AddMember("selectorMatchLabel", "application: nginx", alloc);
		rapidjson::Value selectorLabelExpressions(rapidjson::kArrayType);
		for(const std::string selectorLabelExpression : labelExpressions){
			rapidjson::Value expression(selectorLabelExpression.c_str(), alloc);
			selectorLabelExpressions.PushBack(expression, alloc);
		}
		metadata.AddMember("selectorLabelExpressions", selectorLabelExpressions, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createVolumeResponse=httpPost(volumesURL, to_string(request));
		ENSURE_EQUAL(createVolumeResponse.status,200,"Volume creation should succeed");
		rapidjson::Document data;
		data.Parse(createVolumeResponse.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/VolumeCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		volumeID=data["metadata"]["id"].GetString();
	}


	//fetch the volume info
	//check that
	//	- the reuslt matches the required schema
	//	- the result data is correct
	{ //get the volume 
		auto getResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/"+volumeID+"?token="+adminKey);
		ENSURE_EQUAL(getResp.status,200,"Getting volume should succeed");
		ENSURE(!getResp.body.empty());
		rapidjson::Document data;
		data.Parse(getResp.body.c_str());
		ENSURE_CONFORMS(data,schema);
		ENSURE(data["details"].HasMember("status"), "returned volume claim should have a status");
		ENSURE_EQUAL(data["metadata"]["name"].GetString(), firstVolumeName, "returned volume claim name should match");
		std::cout << "GROUP AS RECORDED IN DB: " << data["metadata"]["group"].GetString() << std::endl;
	}
	
}

TEST(MalformedGetVolumeInfo){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string volumesURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/VolumeInfoResultSchema.json");
	
	//create a group
	// needed to change group name for back to back tests (one is deleting while the other is being setup)
	const std::string groupName="test-malformed-volumes-group";
	{
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResponse=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
		                     to_string(createGroup));
		ENSURE_EQUAL(groupResponse.status,200, "Group creation request should succeed");

	}

	//register a cluster
	const std::string clusterName="testcluster";
	{
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("owningOrganization", "University of Utah", alloc);
		metadata.AddMember("kubeconfig", tc.getKubeConfig(), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto clusterResponse=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(clusterResponse.status,200, "Cluster creation should succeed");
	}

	//create a volume claim
	const std::string firstVolumeName="createvolumetest";
	std::string volumeID;
	std::vector<std::string> labelExpressions;
	labelExpressions.push_back("key: value");
	// labelExpressions.push_back("operator: In");
	// labelExpressions.push_back("production");
	
	{
		// build up the request
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", firstVolumeName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		metadata.AddMember("storageRequest", "10Mi", alloc);
		metadata.AddMember("accessMode", "ReadWriteOnce", alloc);
		metadata.AddMember("volumeMode", "Filesystem", alloc);
		// minikube provides a default storage class called 'standard'
		metadata.AddMember("storageClass", "standard", alloc);
		metadata.AddMember("selectorMatchLabel", "application: nginx", alloc);
		rapidjson::Value selectorLabelExpressions(rapidjson::kArrayType);
		for(const std::string selectorLabelExpression : labelExpressions){
			rapidjson::Value expression(selectorLabelExpression.c_str(), alloc);
			selectorLabelExpressions.PushBack(expression, alloc);
		}
		metadata.AddMember("selectorLabelExpressions", selectorLabelExpressions, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createVolumeResponse=httpPost(volumesURL, to_string(request));
		ENSURE_EQUAL(createVolumeResponse.status,200,"Volume creation should succeed");
		rapidjson::Document data;
		data.Parse(createVolumeResponse.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/VolumeCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		volumeID=data["metadata"]["id"].GetString();
	}


	//create another user which does not belong to the group
	std::string uid;
	std::string otherToken;
	{ //create an unrelated user
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
		uid=createData["metadata"]["id"].GetString();
		otherToken=createData["metadata"]["access_token"].GetString();
	}

	
	//ensure that the second user cannot get the volume info
	{ //get the volume 
		auto getResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/"+volumeID+"?token="+otherToken);
		ENSURE_EQUAL(getResp.status,403,"Requests to fetch volume claims by non-members of the owning Group should be rejected");
	}
}