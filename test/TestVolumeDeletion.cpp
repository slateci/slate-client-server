#include "test.h"

#include <ServerUtilities.h>
#include <KubeInterface.h>
#include <chrono>
#include <thread>


TEST(UnauthenticatedDeleteVolume){
	using namespace httpRequests;
	TestContext tc;

	//try creating a volume claim with no authentication
	auto createResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/some-volume");
	ENSURE_EQUAL(createResp.status,403,
	            "Requests to delete volumes without authentication should be rejected");

	//try creating a secret with invalid authentication
	createResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/some-volume?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(createResp.status,403,
	            "Requests to delete volumes with invalid authentication should be rejected");
}

TEST(DeleteVolume){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string volumesURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	
	//create a group
	//register a cluster
	//create a volume claim
	//check that 
	//	- the volume claim request is successful
	//	- the PVC actually exists on the cluster
	//delete the volume claim
	//check that
	//	- listing no longer shows the volume
	//	- the PVC is actually gone from the cluster

	//create a group
	const std::string groupName="test-delete-volumes-group";
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
		auto kubeConfig = tc.getKubeConfig();
		auto caData = tc.getServerCAData();
		auto token = tc.getUserToken();
		auto kubeNamespace = tc.getKubeNamespace();
		auto serverAddress = tc.getServerAddress();

		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("owningOrganization", "University of Utah", alloc);
		metadata.AddMember("serverAddress", serverAddress, alloc);
		metadata.AddMember("caData", caData, alloc);
		metadata.AddMember("token", token, alloc);
		metadata.AddMember("namespace", kubeNamespace, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto clusterResponse=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(clusterResponse.status,200, "Cluster creation should succeed");
	}

	//create a volume claim
	const std::string volumeName="deletevolumetest";
	std::string volumeID;
	std::vector<std::string> labelExpressions;
	labelExpressions.push_back("key: value");
	{
		// build up the request
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", volumeName, alloc);
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
		ENSURE_EQUAL(createVolumeResponse.status,200,"Volume creation should succeed: "+createVolumeResponse.body);
		rapidjson::Document data;
		data.Parse(createVolumeResponse.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/VolumeCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		volumeID=data["metadata"]["id"].GetString();
	}

	//Delete the Persistent Volume Claim
	{
		std::cout << "VOLUME_ID: " << volumeID << std::endl;
		std::cout << "URL: " << tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/"+volumeID+"?token="+adminKey << std::endl;
		auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/"+volumeID+"?token="+adminKey);
		std::cout << "REPONSE_BODY " << delResp.body << std::endl;
		ENSURE_EQUAL(delResp.status,200,"Volume deletion should succeed");
	}

}


TEST(DeleteVolumeWithPods){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string volumesURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	
	//create a group
	//register a cluster
	//create a volume claim
	//mount app to claim
	//check that 
	//	- the volume claim request is successful
	//	- the PVC actually exists on the cluster
	//delete the volume claim
	//check that
	//	- deletion failed
	//delete mounted instance
	//delete the volume claim
	//check that
	//  - deletion now succeeded


	// create a group
	const std::string groupName="test-delete-mounted-volumes-group";
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

	// register a cluster
	const std::string clusterName="testcluster2";
	{
		auto kubeConfig = tc.getKubeConfig();
		auto caData = tc.getServerCAData();
		auto token = tc.getUserToken();
		auto kubeNamespace = tc.getKubeNamespace();
		auto serverAddress = tc.getServerAddress();

		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("owningOrganization", "University of Utah", alloc);
		metadata.AddMember("serverAddress", serverAddress, alloc);
		metadata.AddMember("caData", caData, alloc);
		metadata.AddMember("token", token, alloc);
		metadata.AddMember("namespace", kubeNamespace, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto clusterResponse=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(clusterResponse.status,200, "Cluster creation should succeed");
	}

	// create a volume claim
	const std::string volumeName="test-volume";
	std::string volumeID;
	std::vector<std::string> labelExpressions;
	labelExpressions.push_back("key: value");
	{
		// build up the request
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", volumeName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		metadata.AddMember("storageRequest", "10Mi", alloc);
		metadata.AddMember("accessMode", "ReadWriteOnce", alloc);
		metadata.AddMember("volumeMode", "Filesystem", alloc);
		// minikube provides a default storage class called 'standard'
		metadata.AddMember("storageClass", "standard", alloc);
		metadata.AddMember("selectorMatchLabel", "application: test-app", alloc);
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
		volumeID=data["metadata"]["id"].GetString();
	}
	
	// Change schema
	auto schema=loadSchema(getSchemaDir()+"/AppInstallResultSchema.json");
	std::string instID;
	struct cleanupHelper{
		TestContext& tc;
		const std::string& id, key;
		cleanupHelper(TestContext& tc, const std::string& id, const std::string& key):
		tc(tc),id(id),key(key){}
		~cleanupHelper(){
			if (!id.empty()) {
				auto delResp = httpDelete(
					tc.getAPIServerURL() + "/" + currentAPIVersion + "/instances/" + id +
					"?token=" + key);
			}
		}
	} cleanup(tc,instID,adminKey);
	
	{ // Install the App
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "test-install-thing", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		ENSURE_CONFORMS(data,schema);
		instID=data["metadata"]["id"].GetString();
	}

	// Wait 5 seconds before trying to delete volume
	std::chrono::milliseconds pause(5000);
	std::this_thread::sleep_for(pause);

	// Try to delete the Persistent Volume Claim
	{
		std::cout << "VOLUME_ID: " << volumeID << std::endl;
		std::cout << "URL: " << tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/"+volumeID+"?token="+adminKey << std::endl;
		auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/"+volumeID+"?token="+adminKey);
		std::cout << "REPONSE_BODY " << delResp.body << std::endl;
		ENSURE_EQUAL(delResp.status,500,"Volume deletion should fail as it has mounted pods");
	}

	// Delete App
	{
		auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances/"+instID+"?token="+adminKey);
		ENSURE_EQUAL(delResp.status,200,"Instance deletion request should succeed");
	}
	
	// Wait 30 seconds before trying to delete volume
	std::chrono::milliseconds timespan(30000);
	std::this_thread::sleep_for(timespan);

	// Delete Volume
	{
		std::cout << "VOLUME_ID: " << volumeID << std::endl;
		std::cout << "URL: " << tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/"+volumeID+"?token="+adminKey << std::endl;
		auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/"+volumeID+"?token="+adminKey);
		std::cout << "REPONSE_BODY " << delResp.body << std::endl;
		ENSURE_EQUAL(delResp.status,200,"Volume deletion should succeed after instance deletion.");
	}
}

/*
TEST(DeleteVolumeMalformedRequests){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string volumesURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	
	{ //attempt to delete a volume claim which does not exist
		auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/not-a-valid-volume-claim?token="+adminKey);
		ENSURE_EQUAL(delResp.status,404,"Requests to delete non-existent secrets should be rejected");
	}

	//create a group
	const std::string groupName="test-delete-volumes-group-malformed";
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
	//create a volume claim
	//create another user which does not belong to the group
	//ensure that the second user cannot delete the volum

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
	const std::string volumeName="deletevolumetest";
	std::string volumeID;
	std::vector<std::string> labelExpressions;
	labelExpressions.push_back("key: value");
	{
		// build up the request
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", volumeName, alloc);
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
		ENSURE_EQUAL(createVolumeResponse.status,200,"Volume creation should succeed: "+createVolumeResponse.body);
		rapidjson::Document data;
		data.Parse(createVolumeResponse.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/VolumeCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		volumeID=data["metadata"]["id"].GetString();
	}

	
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

	{ //attempt to delete the volume claim as the unrelated user
		auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes/"+volumeID+"?token="+otherToken);
		ENSURE_EQUAL(delResp.status,403,"Requests to delete volumes by non-members of the owning Group should be rejected");
	}
}
*/