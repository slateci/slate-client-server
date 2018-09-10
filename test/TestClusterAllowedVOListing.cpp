#include "test.h"

#include <set>
#include <utility>

#include <Utilities.h>

TEST(UnauthenticatedListClusterAllowedVOs){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing allowed VO with no authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/clusters/some-cluster/allowed_vos");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list allowed VOs without authentication should be rejected");
	
	//try listing clusters with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/clusters/some-cluster/allowed_vos?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list allowed VOs with invalid authentication should be rejected");
}

TEST(ListClusterAllowedVOs){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string voName1="first-vo";
	std::string voName2="second-vo";
	
	auto schema=loadSchema(getSchemaDir()+"/VOListResultSchema.json");
	
	//add a VO to register a cluster with
	std::string voID1;
	{
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName1, alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,
							 to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
		ENSURE(!voResp.body.empty());
		rapidjson::Document voData;
		voData.Parse(voResp.body);
		voID1=voData["metadata"]["id"].GetString();
	}
	
	//register a cluster
	std::string clusterID;
	{
		auto kubeConfig=getKubeConfig();
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("vo", voID1, alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey, 
		                         to_string(request1));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document clusterData;
		clusterData.Parse(createResp.body);
		clusterID=clusterData["metadata"]["id"].GetString();
	}
	
	//list the VOs which can use the cluster
	{
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/clusters/"+clusterID+
		                      "/allowed_vos?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200, "VO access list request should succeed");
		ENSURE(!listResp.body.empty());
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		ENSURE_CONFORMS(listData,schema);
		ENSURE_EQUAL(listData["items"].Size(),1,"Only the owning VO should have access to the cluster");
		ENSURE_EQUAL(listData["items"][0]["metadata"]["id"].GetString(),voID1,"ID of VO with access should match ID of the owning VO");
		ENSURE_EQUAL(listData["items"][0]["metadata"]["name"].GetString(),voName1,"Name of VO with access should match name of the owning VO");
	}
	
	//add another VO to give access to the cluster
	std::string voID2;
	{
		rapidjson::Document createVO(rapidjson::kObjectType);
		auto& alloc = createVO.GetAllocator();
		createVO.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName2, alloc);
		createVO.AddMember("metadata", metadata, alloc);
		auto voResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,
							 to_string(createVO));
		ENSURE_EQUAL(voResp.status,200, "VO creation request should succeed");
		ENSURE(!voResp.body.empty());
		rapidjson::Document voData;
		voData.Parse(voResp.body);
		voID2=voData["metadata"]["id"].GetString();
	}
	
	{ //grant the new VO access to the cluster
		auto accessResp=httpPut(tc.getAPIServerURL()+"/v1alpha1/clusters/"+clusterID+
		                    "/allowed_vos/"+voID2+"?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "VO access grant request should succeed: "+accessResp.body);
	}
	
	//list the VOs which can use the cluster again
	{
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/clusters/"+clusterID+
		                      "/allowed_vos?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200, "VO access list request should succeed");
		ENSURE(!listResp.body.empty());
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		ENSURE_CONFORMS(listData,schema);
		std::cout << listResp.body << std::endl;
		ENSURE_EQUAL(listData["items"].Size(),2,"Two VOs should now have access to the cluster");
		std::set<std::pair<std::string,std::string>> vos;
		for(const auto& item : listData["items"].GetArray())
			vos.emplace(item["metadata"]["id"].GetString(),item["metadata"]["name"].GetString());
		ENSURE(vos.count(std::make_pair(voID1,voName1)),"Owning VO should still have access");
		ENSURE(vos.count(std::make_pair(voID2,voName2)),"Additional VO should have access");
	}
}

TEST(MalformedListClusterAllowedVOs){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	
	std::string clusterID1="nonexistent-cluster";
	std::string clusterID2="Cluster_does_not_exist";
	
	{ //attempt to list nonexistent cluster by name
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/clusters/"+clusterID1+
		                      "/allowed_vos?token="+adminKey);
		ENSURE_EQUAL(listResp.status,404, 
		             "VO access list request for nonexistent cluster should be rejected");
	}
	{ //attempt to list nonexistent cluster by 'ID'
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/clusters/"+clusterID2+
		                      "/allowed_vos?token="+adminKey);
		ENSURE_EQUAL(listResp.status,404, 
		             "VO access list request for nonexistent cluster should be rejected");
	}
}