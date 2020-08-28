#include "test.h"

#include <ServerUtilities.h>

TEST(UnauthenticatedListVolumess){
	using namespace httpRequests;
	TestContext tc;

	//try listing volumes with no authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes");
	ENSURE_EQUAL(listResp.status,403,
	             "Requests to list volumes without authentication should be rejected");

	//try listing volumes with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
	            "Requests to list volumes with invalid authentication should be rejected");
}

TEST(ListVolumes){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string volumesURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/VolumeListResultSchema.json");
	
	//create a group
	const std::string groupName="test-list-volumes-group";
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

	//list volumes, ensure none found
	{
		auto listVolumeResponse=httpGet(volumesURL+"&group="+groupName);
		ENSURE_EQUAL(listVolumeResponse.status,200, "Portal admin user should be able to list volumes");

		ENSURE(!listVolumeResponse.body.empty());
		rapidjson::Document data;
		data.Parse(listVolumeResponse.body.c_str());
		ENSURE_CONFORMS(data,schema);

		ENSURE_EQUAL(data["items"].Size(),0,"There should be no volumes returned");
	}

	const std::string firstVolumeName="testvolume1";
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
		// This isn't right
		rapidjson::Value volumeLabalExpressions(rapidjson::kObjectType);
		for(const std::string expression : labelExpressions){
			volumeLabalExpressions.AddMember("expression", expression, alloc);
		}
		metadata.AddMember("selectorLabelExpressions", volumeLabalExpressions, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createVolumeResponse=httpPost(volumesURL, to_string(request));
		ENSURE_EQUAL(createVolumeResponse.status,200,"Volume creation should succeed: "+createVolumeResponse.body);
		rapidjson::Document data;
		data.Parse(createVolumeResponse.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/VolumeCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		firstVolumeID=data["metadata"]["id"].GetString();
	}

	//list volumes, ensure data matches schema and one correct item returned
	{
		auto listVolumesResponse=httpGet(volumesURL+"&group="+groupName);
		ENSURE_EQUAL(listVolumesResponse.status,200, "Portal admin user should be able to list volumes");

		ENSURE(!listVolumesResponse.body.empty());
		rapidjson::Document data;
		data.Parse(listVolumesResponse.body.c_str());
		ENSURE_CONFORMS(data,schema);

		ENSURE_EQUAL(data["items"].Size(),1,"There should be one volume returned");
		ENSURE_EQUAL(data["items"][0]["metadata"]["id"].GetString(),firstVolumeID,"Correct volume ID should be listed");
		ENSURE_EQUAL(data["items"][0]["metadata"]["name"].GetString(),firstVolumeName,"Correct volume name should be listed");
	}

	const std::string secondVolumeName="testvolume2";
	std::string secondVolumeID;
	//create a second volume
	{
		// build up the request
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", secondVolumeName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		metadata.AddMember("storageRequest", "5Mi", alloc);
		metadata.AddMember("accessMode", "ReadWriteOnce", alloc);
		metadata.AddMember("volumeMode", "Filesystem", alloc);
		// minikube provides a default storage class called 'standard'
		metadata.AddMember("storageClass", "standard", alloc);
		metadata.AddMember("selectorMatchLabel", "application: \"nginx\"", alloc);
		rapidjson::Value volumeLabalExpressions(rapidjson::kObjectType);
		// This isn't right
		for(const std::string expression : labelExpressions){
			volumeLabalExpressions.AddMember("expression", expression, alloc);
		}
		metadata.AddMember("selectorLabelExpressions", volumeLabalExpressions, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createVolumeResponse=httpPost(volumesURL, to_string(request));
		ENSURE_EQUAL(createVolumeResponse.status,200,"Volume creation should succeed: "+createVolumeResponse.body);
		rapidjson::Document data;
		data.Parse(createVolumeResponse.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/VolumeCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		secondVolumeID=data["metadata"]["id"].GetString();
	}

	//list volumes, ensure data matches schema and two correct items returned
	{
		auto listVolumesResponse=httpGet(volumesURL+"&group="+groupName);
		ENSURE_EQUAL(listVolumesResponse.status,200, "Portal admin user should be able to list volumes");

		ENSURE(!listVolumesResponse.body.empty());
		rapidjson::Document data;
		data.Parse(listVolumesResponse.body.c_str());
		ENSURE_CONFORMS(data,schema);

		// check number of volumes
		ENSURE_EQUAL(data["items"].Size(),2,"There should be two volumes returned");

		// check first volume
		ENSURE_EQUAL(data["items"][0]["metadata"]["id"].GetString(),firstVolumeID,"Correct volume ID should be listed");
		ENSURE_EQUAL(data["items"][0]["metadata"]["name"].GetString(),firstVolumeName,"Correct volume name should be listed");

		//check second volume
		ENSURE_EQUAL(data["items"][1]["metadata"]["id"].GetString(),secondVolumeID,"Correct volume ID should be listed");
		ENSURE_EQUAL(data["items"][1]["metadata"]["name"].GetString(),secondVolumeName,"Correct volume name should be listed");
	}

}

TEST(ListVolumessByCluster){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string volumesURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/VolumeListResultSchema.json");
	
	//create a group
	const std::string groupName="test-list-volumes-by-cluster-group";
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

	//register two clusters
	const std::string firstClusterName="firstTestCluster";
	const std::string secondClusterName="secondTestCluster";
	// first cluster
	{
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", firstClusterName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("owningOrganization", "University of Utah", alloc);
		metadata.AddMember("kubeconfig", tc.getKubeConfig(), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto clusterResponse=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(clusterResponse.status,200, "Cluster creation should succeed");
	}
	// second cluster
	{
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", secondClusterName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("owningOrganization", "University of Utah", alloc);
		metadata.AddMember("kubeconfig", tc.getKubeConfig(), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto clusterResponse=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(clusterResponse.status,200, "Cluster creation should succeed");
	}

	//create one volume on each cluster
	const std::string firstVolumeName="testvolume1";
	std::string firstVolumeID;
	const std::string secondVolumeName="testvolume2";
	std::string secondVolumeID;
	std::vector<std::string> labelExpressions;
	labelExpressions[0] = "key: value";
	// first volume
	{
		// build up the request
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", firstVolumeName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("cluster", firstClusterName, alloc);
		metadata.AddMember("storageRequest", "10Mi", alloc);
		metadata.AddMember("accessMode", "ReadWriteOnce", alloc);
		metadata.AddMember("volumeMode", "Filesystem", alloc);
		// minikube provides a default storage class called 'standard'
		metadata.AddMember("storageClass", "standard", alloc);
		metadata.AddMember("selectorMatchLabel", "application: \"nginx\"", alloc);
		rapidjson::Value volumeLabalExpressions(rapidjson::kObjectType);
		for(const std::string expression : labelExpressions){
			volumeLabalExpressions.AddMember("expression", expression, alloc);
		}
		metadata.AddMember("selectorLabelExpressions", volumeLabalExpressions, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createVolumeResponse=httpPost(volumesURL, to_string(request));
		ENSURE_EQUAL(createVolumeResponse.status,200,"Volume creation should succeed: "+createVolumeResponse.body);
		rapidjson::Document data;
		data.Parse(createVolumeResponse.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/VolumeCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		firstVolumeID=data["metadata"]["id"].GetString();
	}

	// second volume
	{
		// build up the request
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", secondVolumeName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("cluster", secondClusterName, alloc);
		metadata.AddMember("storageRequest", "10Mi", alloc);
		metadata.AddMember("accessMode", "ReadWriteOnce", alloc);
		metadata.AddMember("volumeMode", "Filesystem", alloc);
		// minikube provides a default storage class called 'standard'
		metadata.AddMember("storageClass", "standard", alloc);
		metadata.AddMember("selectorMatchLabel", "application: \"nginx\"", alloc);
		rapidjson::Value volumeLabalExpressions(rapidjson::kObjectType);
		for(const std::string expression : labelExpressions){
			volumeLabalExpressions.AddMember("expression", expression, alloc);
		}
		metadata.AddMember("selectorLabelExpressions", volumeLabalExpressions, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createVolumeResponse=httpPost(volumesURL, to_string(request));
		ENSURE_EQUAL(createVolumeResponse.status,200,"Volume creation should succeed: "+createVolumeResponse.body);
		rapidjson::Document data;
		data.Parse(createVolumeResponse.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/VolumeCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		firstVolumeID=data["metadata"]["id"].GetString();
	}

	//list volumes on each cluster, ensure only correct volume appears

	// list volumes on first cluster
	{
		auto listVolumesResponse=httpGet(volumesURL+"&group="+groupName+"&cluster="+firstClusterName);
		ENSURE_EQUAL(listVolumesResponse.status,200, "Portal admin user should be able to list volumes");

		ENSURE(!listVolumesResponse.body.empty());
		rapidjson::Document data;
		data.Parse(listVolumesResponse.body.c_str());
		ENSURE_CONFORMS(data,schema);

		ENSURE_EQUAL(data["items"].Size(),1,"There should be one volume returned");
		ENSURE_EQUAL(data["items"][0]["metadata"]["id"].GetString(),firstVolumeID,"Correct volume ID should be listed");
		ENSURE_EQUAL(data["items"][0]["metadata"]["name"].GetString(),firstVolumeName,"Correct volume name should be listed");
	}


	// list volumes on second cluster
	{
		auto listVolumesResponse=httpGet(volumesURL+"&group="+groupName+"&cluster="+secondClusterName);
		ENSURE_EQUAL(listVolumesResponse.status,200, "Portal admin user should be able to list volumes");

		ENSURE(!listVolumesResponse.body.empty());
		rapidjson::Document data;
		data.Parse(listVolumesResponse.body.c_str());
		ENSURE_CONFORMS(data,schema);

		ENSURE_EQUAL(data["items"].Size(),1,"There should be one volume returned");
		ENSURE_EQUAL(data["items"][0]["metadata"]["id"].GetString(),secondVolumeID,"Correct volume ID should be listed");
		ENSURE_EQUAL(data["items"][0]["metadata"]["name"].GetString(),secondVolumeName,"Correct volume name should be listed");
	}
	
}

TEST(ListVolumessByGroup){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string volumesURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/volumes?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/VolumeListResultSchema.json");
	
	//create two groups

	// first group
	const std::string firstGroupName="test-list-volume-by-group-group1";
	std::string firstGroupID;
	{
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", firstGroupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResponse=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
		                     to_string(createGroup));
		ENSURE_EQUAL(groupResponse.status,200, "Group creation request should succeed");
		rapidjson::Document groupData;
		groupData.Parse(groupResponse.body);
		firstGroupID=groupData["metadata"]["id"].GetString();
	}

	// second group
	const std::string secondGroupName="test-list-volume-by-group-group2";
	std::string secondGroupID;
	{
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", secondGroupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResponse=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
		                     to_string(createGroup));
		ENSURE_EQUAL(groupResponse.status,200, "Group creation request should succeed");
		rapidjson::Document groupData;
		groupData.Parse(groupResponse.body);
		secondGroupID=groupData["metadata"]["id"].GetString();
	}

	//register a cluster
	const std::string clusterName="testcluster";
	std::string clusterID;
	{
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("group", firstGroupName, alloc);
		metadata.AddMember("owningOrganization", "New Mexico State University", alloc);
		metadata.AddMember("kubeconfig", tc.getKubeConfig(), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto clusterResponse=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(clusterResponse.status,200, "Cluster creation should succeed");
		rapidjson::Document clusterData;
		clusterData.Parse(clusterResponse.body);
		clusterID=clusterData["metadata"]["id"].GetString();
	}

	//grant the second group access to the cluster
	{
		auto grantAccessResponse=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								"/allowed_groups/"+secondGroupID+"?token="+adminKey,"");
		ENSURE_EQUAL(grantAccessResponse.status,200, "Group access grant request should succeed: "+
																grantAccessResponse.body);
	}

	//create one volume for each group
	const std::string firstVolumeName="testvolume1";
	std::string firstVolumeID;
	const std::string secondVolumeName="testvolume2";
	std::string secondVolumeID;
	std::vector<std::string> labelExpressions;
	labelExpressions[0] = "key: value";
	// first volume
	{
		// build up the request
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", firstVolumeName, alloc);
		metadata.AddMember("group", firstGroupName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		metadata.AddMember("storageRequest", "10Mi", alloc);
		metadata.AddMember("accessMode", "ReadWriteOnce", alloc);
		metadata.AddMember("volumeMode", "Filesystem", alloc);
		// minikube provides a default storage class called 'standard'
		metadata.AddMember("storageClass", "standard", alloc);
		metadata.AddMember("selectorMatchLabel", "application: \"nginx\"", alloc);
		rapidjson::Value volumeLabalExpressions(rapidjson::kObjectType);
		for(const std::string expression : labelExpressions){
			volumeLabalExpressions.AddMember("expression", expression, alloc);
		}
		metadata.AddMember("selectorLabelExpressions", volumeLabalExpressions, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createVolumeResponse=httpPost(volumesURL, to_string(request));
		ENSURE_EQUAL(createVolumeResponse.status,200,"Volume creation should succeed: "+createVolumeResponse.body);
		rapidjson::Document data;
		data.Parse(createVolumeResponse.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/VolumeCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		firstVolumeID=data["metadata"]["id"].GetString();
	}

	// second volume
	{
		// build up the request
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", secondVolumeName, alloc);
		metadata.AddMember("group", secondGroupName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		metadata.AddMember("storageRequest", "10Mi", alloc);
		metadata.AddMember("accessMode", "ReadWriteOnce", alloc);
		metadata.AddMember("volumeMode", "Filesystem", alloc);
		// minikube provides a default storage class called 'standard'
		metadata.AddMember("storageClass", "standard", alloc);
		metadata.AddMember("selectorMatchLabel", "application: \"nginx\"", alloc);
		rapidjson::Value volumeLabalExpressions(rapidjson::kObjectType);
		for(const std::string expression : labelExpressions){
			volumeLabalExpressions.AddMember("expression", expression, alloc);
		}
		metadata.AddMember("selectorLabelExpressions", volumeLabalExpressions, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createVolumeResponse=httpPost(volumesURL, to_string(request));
		ENSURE_EQUAL(createVolumeResponse.status,200,"Volume creation should succeed: "+createVolumeResponse.body);
		rapidjson::Document data;
		data.Parse(createVolumeResponse.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/VolumeCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		firstVolumeID=data["metadata"]["id"].GetString();
	}	

	//list volumes for each group, ensure only correct volume appears

	// list volumes by first group
	{
		auto listVolumesResponse=httpGet(volumesURL+"&group="+firstGroupName+"&cluster="+clusterName);
		ENSURE_EQUAL(listVolumesResponse.status,200, "Portal admin user should be able to list volumes");

		ENSURE(!listVolumesResponse.body.empty());
		rapidjson::Document data;
		data.Parse(listVolumesResponse.body.c_str());
		ENSURE_CONFORMS(data,schema);

		ENSURE_EQUAL(data["items"].Size(),1,"There should be one volume returned");
		ENSURE_EQUAL(data["items"][0]["metadata"]["id"].GetString(),firstVolumeID,"Correct volume ID should be listed");
		ENSURE_EQUAL(data["items"][0]["metadata"]["name"].GetString(),firstVolumeName,"Correct volume name should be listed");
	}

	// list volumes by second group
	{
		auto listVolumesResponse=httpGet(volumesURL+"&group="+secondGroupName+"&cluster="+clusterName);
		ENSURE_EQUAL(listVolumesResponse.status,200, "Portal admin user should be able to list volumes");

		ENSURE(!listVolumesResponse.body.empty());
		rapidjson::Document data;
		data.Parse(listVolumesResponse.body.c_str());
		ENSURE_CONFORMS(data,schema);

		ENSURE_EQUAL(data["items"].Size(),1,"There should be one volume returned");
		ENSURE_EQUAL(data["items"][0]["metadata"]["id"].GetString(),secondVolumeID,"Correct volume ID should be listed");
		ENSURE_EQUAL(data["items"][0]["metadata"]["name"].GetString(),secondVolumeName,"Correct volume name should be listed");
	}

}
