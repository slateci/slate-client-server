#include "test.h"

#include <PersistentStore.h>
#include <ServerUtilities.h>

TEST(UnauthenticatedListSecrets){
	using namespace httpRequests;
	TestContext tc;

	//try listing secrets with no authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets");
	ENSURE_EQUAL(listResp.status,403,
	             "Requests to list secrets without authentication should be rejected");

	//try listing secrets with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
	            "Requests to list secrets with invalid authentication should be rejected");
}

TEST(ListSecrets){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/SecretListResultSchema.json");
	
	//create a VO
	const std::string groupName="test-list-secrets-group";
	{
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
		                     to_string(createGroup));
		ENSURE_EQUAL(groupResp.status,200, "Group creation request should succeed");
	}

	const std::string clusterName="testcluster";
	{ //add a cluster
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", tc.getKubeConfig(), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
	}

	{ //list
		auto listResp=httpGet(secretsURL+"&group="+groupName);
		ENSURE_EQUAL(listResp.status,200, "Portal admin user should be able to list clusters");

		ENSURE(!listResp.body.empty());
		rapidjson::Document data;
		data.Parse(listResp.body.c_str());
		ENSURE_CONFORMS(data,schema);

		//should be no secrets
		ENSURE_EQUAL(data["items"].Size(),0,"There should be no secrets returned");
	}
	
	const std::string secretName="listsecrets-secret1";
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
	
	{ //install a secret
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", secretName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Secret creation should succeed: "+createResp.body);
		rapidjson::Document data;
		data.Parse(createResp.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/SecretCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		secretID=data["metadata"]["id"].GetString();
	}
	
	{ //list again
		auto listResp=httpGet(secretsURL+"&group="+groupName);
		ENSURE_EQUAL(listResp.status,200, "Portal admin user should be able to list clusters");

		ENSURE(!listResp.body.empty());
		rapidjson::Document data;
		data.Parse(listResp.body.c_str());
		ENSURE_CONFORMS(data,schema);

		//should be one secrets
		ENSURE_EQUAL(data["items"].Size(),1,"There should be one secret returned");
		ENSURE_EQUAL(data["items"][0]["metadata"]["id"].GetString(),secretID,"Correct secret ID should be listed");
		ENSURE_EQUAL(data["items"][0]["metadata"]["name"].GetString(),secretName,"Correct secret name should be listed");
	}
}

TEST(ListSecretsByCluster){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets?token="+adminKey;
	auto schema=loadSchema(getSchemaDir()+"/SecretListResultSchema.json");
	
	//create a VO
	const std::string groupName="test-list-secrets-by-cluster-group";
	{
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
		                     to_string(createGroup));
		ENSURE_EQUAL(groupResp.status,200, "Group creation request should succeed");
	}

	const std::string clusterName1="testcluster1";
	const std::string clusterName2="testcluster2";
	{ //add a cluster
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName1, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", tc.getKubeConfig(), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
	}
	{ //add another cluster
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName2, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", tc.getKubeConfig(), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
	}
	
	const std::string secretName1="listsecretsbycluster-secret1";
	const std::string secretName2="listsecretsbycluster-secret2";
	std::vector<std::string> secretIDs;
	struct cleanupHelper{
		TestContext& tc;
		const std::vector<std::string>& ids;
		const std::string&key;
		cleanupHelper(TestContext& tc, const std::vector<std::string>& ids, const std::string& key):
		tc(tc),ids(ids),key(key){}
		~cleanupHelper(){
			for(const auto& id : ids)
				auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets/"+id+"?token="+key);
		}
	} cleanup(tc,secretIDs,adminKey);
	
	//install a secret on each cluster
	{ 
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", secretName1, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("cluster", clusterName1, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Secret creation should succeed: "+createResp.body);
		rapidjson::Document data;
		data.Parse(createResp.body.c_str());
		secretIDs.push_back(data["metadata"]["id"].GetString());
	}
	{ 
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", secretName2, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("cluster", clusterName2, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Secret creation should succeed: "+createResp.body);
		rapidjson::Document data;
		data.Parse(createResp.body.c_str());
		secretIDs.push_back(data["metadata"]["id"].GetString());
	}
	
	{ //list all
		auto listResp=httpGet(secretsURL+"&group="+groupName);
		ENSURE_EQUAL(listResp.status,200, "Portal admin user should be able to list clusters");

		ENSURE(!listResp.body.empty());
		rapidjson::Document data;
		data.Parse(listResp.body.c_str());
		ENSURE_CONFORMS(data,schema);

		//should be two secrets
		ENSURE_EQUAL(data["items"].Size(),2,"There should be two secrets returned");
		ENSURE(data["items"][0]["metadata"]["id"].GetString()==secretIDs[0] ||
		       data["items"][0]["metadata"]["id"].GetString()==secretIDs[1],"Correct secret ID should be listed");
		ENSURE(data["items"][1]["metadata"]["id"].GetString()==secretIDs[0] ||
		       data["items"][1]["metadata"]["id"].GetString()==secretIDs[1],"Correct secret ID should be listed");
		ENSURE(data["items"][0]["metadata"]["id"].GetString()
		       !=data["items"][1]["metadata"]["id"].GetString(),
		       "Secrets muct have distinct IDs");
		ENSURE(data["items"][0]["metadata"]["name"].GetString()==secretName1 ||
		       data["items"][0]["metadata"]["name"].GetString()==secretName2,"Correct secret name should be listed");
		ENSURE(data["items"][1]["metadata"]["name"].GetString()==secretName1 ||
		       data["items"][1]["metadata"]["name"].GetString()==secretName2,"Correct secret name should be listed");
		ENSURE(data["items"][0]["metadata"]["name"].GetString()
		       !=data["items"][1]["metadata"]["name"].GetString(),
		       "Secrets muct have distinct names");
	}
	
	{ //list on cluster 1
		auto listResp=httpGet(secretsURL+"&group="+groupName+"&cluster="+clusterName1);
		ENSURE_EQUAL(listResp.status,200, "Portal admin user should be able to list clusters");

		ENSURE(!listResp.body.empty());
		rapidjson::Document data;
		data.Parse(listResp.body.c_str());
		ENSURE_CONFORMS(data,schema);

		//should be one secret
		ENSURE_EQUAL(data["items"].Size(),1,"There should be one secret returned");
		ENSURE_EQUAL(data["items"][0]["metadata"]["id"].GetString(),secretIDs[0],"Correct secret ID should be listed");
		ENSURE_EQUAL(data["items"][0]["metadata"]["name"].GetString(),secretName1,"Correct secret name should be listed");
	}
	
	{ //list on cluster 2
		auto listResp=httpGet(secretsURL+"&group="+groupName+"&cluster="+clusterName2);
		ENSURE_EQUAL(listResp.status,200, "Portal admin user should be able to list clusters");

		ENSURE(!listResp.body.empty());
		rapidjson::Document data;
		data.Parse(listResp.body.c_str());
		ENSURE_CONFORMS(data,schema);

		//should be one secret
		ENSURE_EQUAL(data["items"].Size(),1,"There should be one secret returned");
		ENSURE_EQUAL(data["items"][0]["metadata"]["id"].GetString(),secretIDs[1],"Correct secret ID should be listed");
		ENSURE_EQUAL(data["items"][0]["metadata"]["name"].GetString(),secretName2,"Correct secret name should be listed");
	}
}

TEST(ListSecretsByClusterFull){
	//Listing all secrets on a cluster without regard for owning Group is a 
	//privileged operation which should never be exposed in the public API.
	//Testing it therefore requires testing the PersistentStore directly. 
	
	DatabaseContext db;
	auto storePtr=db.makePersistentStore();
	auto& store=*storePtr;
	
	Group group1;
	group1.id=idGenerator.generateGroupID();
	group1.name="group1";
	group1.email="abc@def";
	group1.phone="123";
	group1.scienceField="Stuff";
	group1.description=" ";
	group1.valid=true;
	
	bool success=store.addGroup(group1);
	ENSURE(success,"Group addition should succeed");
	
	Group group2;
	group2.id=idGenerator.generateGroupID();
	group2.name="group2";
	group2.email="ghi@jkl";
	group2.phone="456";
	group2.scienceField="Stuff";
	group2.description=" ";
	group2.valid=true;
	
	success=store.addGroup(group2);
	ENSURE(success,"Group addition should succeed");
	
	Cluster cluster1;
	cluster1.id=idGenerator.generateClusterID();
	cluster1.name="cluster1";
	cluster1.config="-"; //Dynamo will get upset if this is empty, but it will not be used
	cluster1.systemNamespace="-"; //Dynamo will get upset if this is empty, but it will not be used
	cluster1.owningGroup=group1.id;
	cluster1.owningOrganization="aekhcb";
	cluster1.valid=true;
	
	success=store.addCluster(cluster1);
	ENSURE(success,"Cluster addition should succeed");
	
	Cluster cluster2;
	cluster2.id=idGenerator.generateClusterID();
	cluster2.name="cluster2";
	cluster2.config="-"; //Dynamo will get upset if this is empty, but it will not be used
	cluster2.systemNamespace="-"; //Dynamo will get upset if this is empty, but it will not be used
	cluster2.owningGroup=group2.id;
	cluster2.owningOrganization="aekhcb";
	cluster2.valid=true;
	
	success=store.addCluster(cluster2);
	ENSURE(success,"Cluster addition should succeed");
	
	//The persistent store does not currently enforce this, but for completeness
	//grant each Group access to each cluster
	success=store.addGroupToCluster(group2.id,cluster1.id);
	success=store.addGroupToCluster(group1.id,cluster2.id);
	
	Secret s1;
	s1.id=idGenerator.generateSecretID();
	s1.name="secret1";
	s1.group=group1.id;
	s1.cluster=cluster1.id;
	s1.ctime="-"; //not used
	s1.data="scrypt"+std::string(128,' '); //fool the simple checks for a valid encrypted header
	s1.valid=true;
	success=store.addSecret(s1);
	ENSURE(success,"Secret addition should succeed");
	
	Secret s2=s1;
	s2.id=idGenerator.generateSecretID();
	s2.name="secret2";
	s2.group=group2.id;
	s2.cluster=cluster1.id;
	s2.valid=true;
	success=store.addSecret(s2);
	ENSURE(success,"Secret addition should succeed");
	
	Secret s3=s1;
	s3.id=idGenerator.generateSecretID();
	s3.name="secret3";
	s3.group=group1.id;
	s3.cluster=cluster2.id;
	s3.valid=true;
	success=store.addSecret(s3);
	ENSURE(success,"Secret addition should succeed");
	
	Secret s4=s1;
	s4.id=idGenerator.generateSecretID();
	s4.name="secret3";
	s4.group=group2.id;
	s4.cluster=cluster2.id;
	s4.valid=true;
	success=store.addSecret(s4);
	ENSURE(success,"Secret addition should succeed");
	
	for(auto group : {group1.id, group2.id}){
		for(auto cluster : {cluster1.id,cluster2.id}){
			auto secrets=store.listSecrets(group,cluster);
			ENSURE_EQUAL(secrets.size(),1,"Each Group should have one secret per cluster");
			ENSURE_EQUAL(secrets.front().group,group,"Retuned secret should belong to correct Group");
			ENSURE_EQUAL(secrets.front().cluster,cluster,"Retuned secret should be from correct cluster");
		}
	}
	
	for(auto group : {group1.id, group2.id}){
		auto secrets=store.listSecrets(group,"");
		ENSURE_EQUAL(secrets.size(),2,"Each Group should have two secrets");
		ENSURE_EQUAL(secrets[0].group,group,"Returned secrets should belong to the correct Group");
		ENSURE_EQUAL(secrets[1].group,group,"Returned secrets should belong to the correct Group");
	}
	
	for(auto cluster : {cluster1.id, cluster2.id}){
		auto secrets=store.listSecrets("",cluster);
		ENSURE_EQUAL(secrets.size(),2,"Each cluster should have two secrets");
		ENSURE_EQUAL(secrets[0].cluster,cluster,"Returned secrets should be from the correct cluster");
		ENSURE_EQUAL(secrets[1].cluster,cluster,"Returned secrets should be from the correct cluster");
	}
}

TEST(ListSecretsMalformedRequests){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets?token="+adminKey;
	
	//create a VO
	const std::string groupName="test-list-secrets--malformed-group";
	{
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
		                     to_string(createGroup));
		ENSURE_EQUAL(groupResp.status,200, "Group creation request should succeed");
	}
	
	{ //attempt to list without a VO
		auto listResp=httpGet(secretsURL);
		ENSURE_EQUAL(listResp.status,400, "Requests to list secrets without a Group specified should be rejected");
	}
	{ //attempt to list for an invalid VO
		auto listResp=httpGet(secretsURL+"&group=non-existent-group");
		ENSURE_EQUAL(listResp.status,404, "Requests to list secrets for a non-existsent Group should be rejected");
	}
	{ //attempt to list for an invalid cluster
		auto listResp=httpGet(secretsURL+"&group="+groupName+"&cluster=non-existent-cluster");
		//TODO: 
		//ENSURE_EQUAL(listResp.status,404, "Requests to list secrets for a non-existsent Group should be rejected");
	}
}

TEST(ListSecretsGroupNonMember){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	std::string secretsURL=tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets?token="+adminKey;
	
	//create a VO
	const std::string groupName="test-list-secrets-group-nonmember";
	{
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
		                     to_string(createGroup));
		ENSURE_EQUAL(groupResp.status,200, "Group creation request should succeed");
	}

	const std::string clusterName="testcluster";
	{ //add a cluster
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", tc.getKubeConfig(), alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
	}
	
	const std::string secretName="listsecrets-secret1";
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
	
	{ //install a secret
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", secretName, alloc);
		metadata.AddMember("group", groupName, alloc);
		metadata.AddMember("cluster", clusterName, alloc);
		request.AddMember("metadata", metadata, alloc);
		rapidjson::Value contents(rapidjson::kObjectType);
		contents.AddMember("foo", "bar", alloc);
		request.AddMember("contents", contents, alloc);
		auto createResp=httpPost(secretsURL, to_string(request));
		ENSURE_EQUAL(createResp.status,200, "Secret creation should succeed: "+createResp.body);
		rapidjson::Document data;
		data.Parse(createResp.body.c_str());
		auto schema=loadSchema(getSchemaDir()+"/SecretCreateResultSchema.json");
		ENSURE_CONFORMS(data,schema);
		secretID=data["metadata"]["id"].GetString();
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
	
	{ //attempt to list the secret as the unrelated user
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/secrets?token="+otherToken+"&group="+groupName);
		ENSURE_EQUAL(listResp.status,403,"Requests by non-members should not be able to list a Group's secrets");
	}
}
