#include "test.h"

#include <Utilities.h>

TEST(UnauthenticatedInstanceList){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing instances with no authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/instances");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list instances without authentication should be rejected");
	
	//try listing instances with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/instances?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list instances with invalid authentication should be rejected");
}

TEST(InstanceList){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto schema=loadSchema("../../slate-portal-api-spec/InstanceListResultSchema.json");
	
	std::string voName="test-inst-list";
	std::string clusterName="testcluster";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	{ //create a cluster
		auto kubeConfig = getKubeConfig();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName, alloc);
		metadata.AddMember("vo", voName, alloc);
		metadata.AddMember("kubeconfig", kubeConfig, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200,
					 "Cluster creation request should succeed");
		ENSURE(!createResp.body.empty());
	}
	
	{ //list when nothing is installed
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/instances?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,
		             "Listing application instances should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),0,"No instances should be returned in the listing");
	}
	
	std::string instID;
	struct cleanupHelper{
		TestContext& tc;
		const std::string& id, key;
		cleanupHelper(TestContext& tc, const std::string& id, const std::string& key):
		tc(tc),id(id),key(key){}
		~cleanupHelper(){
			if(!id.empty())
				auto delResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/instances/"+id+"?token="+key);
		}
	} cleanup(tc,instID,adminKey);
	
	{ //install something
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		request.AddMember("vo", voName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		if(data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("id"))
			instID=data["metadata"]["id"].GetString();
	}
	
	{ //list again
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/instances?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,
		             "Listing application instances should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),1,"One instance should be returned in the listing");
		ENSURE_EQUAL(data["items"][0]["metadata"]["id"].GetString(),instID,
		             "Instance listing should report the correct instance ID");
	}
}

TEST(ScopedInstanceList){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	auto schema=loadSchema("../../slate-portal-api-spec/InstanceListResultSchema.json");
	
	const std::string voName1="test-scoped-inst-list1";
	const std::string voName2="test-scoped-inst-list2";
	const std::string clusterName1="testcluster1";
	const std::string clusterName2="testcluster2";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName1, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	{ //create another VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", voName2, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/vos?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"VO creation request should succeed");
	}
	
	{ //create a cluster
		auto kubeConfig = getKubeConfig();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName1, alloc);
		metadata.AddMember("vo", voName1, alloc);
		metadata.AddMember("kubeconfig", kubeConfig, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200,
					 "Cluster creation request should succeed");
		ENSURE(!createResp.body.empty());
	}
	
	{ //create another cluster
		//slightly evil hack: this is the same cluster, but as long as we 
		//manually avoid name collisions, SLATE won't notice
		auto kubeConfig = getKubeConfig();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName2, alloc);
		metadata.AddMember("vo", voName1, alloc);
		metadata.AddMember("kubeconfig", kubeConfig, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200,
					 "Cluster creation request should succeed");
		ENSURE(!createResp.body.empty());
	}
	
	{//grant second VO access to both clusters
		auto accessResp=httpPut(tc.getAPIServerURL()+"/v1alpha1/clusters/"+clusterName1+
		                        "/allowed_vos/"+voName2+"?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "VO access grant request should succeed: "+accessResp.body);
		accessResp=httpPut(tc.getAPIServerURL()+"/v1alpha1/clusters/"+clusterName2+
		                        "/allowed_vos/"+voName2+"?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "VO access grant request should succeed: "+accessResp.body);
	}
	
	std::vector<std::string> instIDs;
	struct cleanupHelper{
		TestContext& tc;
		const std::vector<std::string>& ids;
		const std::string& key;
		cleanupHelper(TestContext& tc, const std::vector<std::string>& ids, const std::string& key):
		tc(tc),ids(ids),key(key){}
		~cleanupHelper(){
			for(const auto& id : ids)
				auto delResp=httpDelete(tc.getAPIServerURL()+"/v1alpha1/instances/"+id+"?token="+key);
		}
	} cleanup(tc,instIDs,adminKey);
	
	//install one application instance on each cluster for each VO
	unsigned int instIdx=0;
	for(const auto& voName : {voName1, voName2}){
		for(const auto& clusterName : {clusterName1, clusterName2}){
			{ //install something
				rapidjson::Document request(rapidjson::kObjectType);
				auto& alloc = request.GetAllocator();
				request.AddMember("apiVersion", "v1alpha1", alloc);
				request.AddMember("vo", voName, alloc);
				request.AddMember("cluster", clusterName, alloc);
				request.AddMember("tag", "inst"+std::to_string(instIdx++), alloc);
				request.AddMember("configuration", "", alloc);
				auto instResp=httpPost(tc.getAPIServerURL()+"/v1alpha1/apps/test-app?test&token="
				                       +adminKey,to_string(request));
				ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
				rapidjson::Document data;
				data.Parse(instResp.body);
				if(data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("id"))
					instIDs.push_back(data["metadata"]["id"].GetString());
			}
		}
	}
	
	{ //list everything
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/instances?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,
		             "Listing application instances should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),4,"All four instances should be returned in the listing");
	}
	
	{ //list things on cluster 1
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/instances?cluster="
		                      +clusterName1+"&token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,
		             "Listing application instances should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),2,"Two instances should be returned in the listing");
		for(const auto& item : data["items"].GetArray()){
			ENSURE_EQUAL(item["metadata"]["cluster"].GetString(),clusterName1,
			             "Only instances on the first cluster should be returned");
		}
		ENSURE(std::string(data["items"][0]["metadata"]["vo"].GetString())!=data["items"][1]["metadata"]["vo"].GetString());
	}
	
	{ //list things on cluster 2
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/instances?cluster="
		                      +clusterName2+"&token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,
		             "Listing application instances should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),2,"Two instances should be returned in the listing");
		for(const auto& item : data["items"].GetArray()){
			ENSURE_EQUAL(item["metadata"]["cluster"].GetString(),clusterName2,
			             "Only instances on the second cluster should be returned");
		}
		ENSURE(std::string(data["items"][0]["metadata"]["vo"].GetString())!=data["items"][1]["metadata"]["vo"].GetString());
	}
	
	{ //list things on belonging to VO 1
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/instances?vo="
		                      +voName1+"&token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,
		             "Listing application instances should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),2,"Two instances should be returned in the listing");
		for(const auto& item : data["items"].GetArray()){
			ENSURE_EQUAL(item["metadata"]["vo"].GetString(),voName1,
			             "Only instances belonging to the first VO should be returned");
		}
		ENSURE(std::string(data["items"][0]["metadata"]["cluster"].GetString())!=data["items"][1]["metadata"]["cluster"].GetString());
	}
	
	{ //list things on belonging to VO 2
		auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/instances?vo="
		                      +voName2+"&token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,
		             "Listing application instances should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),2,"Two instances should be returned in the listing");
		for(const auto& item : data["items"].GetArray()){
			ENSURE_EQUAL(item["metadata"]["vo"].GetString(),voName2,
			             "Only instances belonging to the second VO should be returned");
		}
		ENSURE(std::string(data["items"][0]["metadata"]["cluster"].GetString())!=data["items"][1]["metadata"]["cluster"].GetString());
	}
	
	//List things on each combination of one cluster and one VO
	for(const auto voName : {voName1, voName2}){
		for(const auto clusterName : {clusterName1, clusterName2}){
			auto listResp=httpGet(tc.getAPIServerURL()+"/v1alpha1/instances?cluster="
			                      +clusterName+"&vo="+voName+"&token="+adminKey);
			ENSURE_EQUAL(listResp.status,200,
			             "Listing application instances should succeed");
			rapidjson::Document data;
			data.Parse(listResp.body);
			ENSURE_CONFORMS(data,schema);
			ENSURE_EQUAL(data["items"].Size(),1,"One instances should be returned in the listing");
			ENSURE_EQUAL(data["items"][0]["metadata"]["cluster"].GetString(),clusterName,
			             "Only instances on the correct cluster should be returned");
			ENSURE_EQUAL(data["items"][0]["metadata"]["vo"].GetString(),voName,
			             "Only instances belonging to the correct VO should be returned");
		}
	}
}