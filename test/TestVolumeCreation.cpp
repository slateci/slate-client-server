#include "test.h"

#include <ServerUtilities.h>
#include <KubeInterface.h>
#include <Logging.h>

TEST(UnauthenticatedCreateVolume){
	using namespace httpRequests;
	TestContext tc;

	//try creating a volume claim with no authentication
	auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes","");
	ENSURE_EQUAL(createResp.status,403,
	            "Requests to create volumes without authentication should be rejected");

	//try creating a volume with invalid authentication
	createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token=00112233-4455-6677-8899-aabbccddeeff","");
	ENSURE_EQUAL(createResp.status,403,
	            "Requests to create volumes with invalid authentication should be rejected");
}

TEST(CreateVolume){

	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string volumesURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/VolumeCreateResultSchema.json");
	
	//create a group
	const std::string groupName="test-create-volumes-group";
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
	//check that 
	//	- the volume claim request is successful
	//	- the result matches the required schema
	//	- the PVC actually exists on the cluster
	const std::string firstVolumeName="createvolumetest";
	std::string firstVolumeID;
	std::vector<std::string> labelExpressions;
	labelExpressions[0] = "key: value";
	//create a volume
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
		metadata.AddMember("selectorMatchLabel", "application: \"nginx\"", alloc);
		rapidjson::Value selectorLabelExpressions(rapidjson::kArrayType);
		for(const std::string selectorLabelExpression : labelExpressions){
			rapidjson::Value expression(selectorLabelExpression.c_str(), alloc);
			selectorLabelExpressions.PushBack(expression, alloc);
		}
		metadata.AddMember("selectorLabelExpressions", selectorLabelExpressions, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createVolumeResponse=httpPost(volumesURL, to_string(request));
		ENSURE_EQUAL(createVolumeResponse.status,200,"Volume creation should succeed: "+createVolumeResponse.body);
		rapidjson::Document data;
		data.Parse(createVolumeResponse.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/VolumeCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		firstVolumeID=data["metadata"]["id"].GetString();
	}

	//verify pvc exists on cluster
	auto tempPath=makeTemporaryFile("kubeconfig_");
	{
		{
			std::ofstream outFile(tempPath.path());
			if(!outFile)
				log_fatal("Failed to open " << tempPath.path() << " for writing");
			std::string kubeconfig=tc.getKubeConfig();
			outFile.write(kubeconfig.c_str(),kubeconfig.size());
		}
		startReaper();
		auto result=kubernetes::kubectl(tempPath,{"get","pvc",firstVolumeName,
												"-n","slate-group-"+groupName,
												"-o=jsonpath={.items[0].metadata.name}"});
		stopReaper();
		ENSURE_EQUAL(result.status,0,"Should be get PVC by name");
		ENSURE_EQUAL(firstVolumeName,result.output,
		"PVC name exists in cluster");
	}


}

TEST(CreateVolumeMalformedRequests){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/VolumeCreateResultSchema.json");
	
	//create a group
	//register a cluster
	//attempt to create a volume with no metadata in the request
	//attempt to create a volume with each of the required metadata fields missing
	//attempt to create a volume with each of the required metadata fields haivng the wrong type
}