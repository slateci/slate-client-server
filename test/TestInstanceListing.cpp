#include "test.h"

#include <ServerUtilities.h>

#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/writer.h>

TEST(UnauthenticatedInstanceList){
	using namespace httpRequests;
	TestContext tc;
	
	//try listing instances with no authentication
	auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list instances without authentication should be rejected");
	
	//try listing instances with invalid authentication
	listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(listResp.status,403,
				 "Requests to list instances with invalid authentication should be rejected");
}

TEST(InstanceList){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=tc.getPortalToken();
	auto schema=loadSchema(getSchemaDir()+"/InstanceListResultSchema.json");
	
	std::string groupName="test-inst-list";
	std::string clusterName="testcluster";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Group creation request should succeed");
	}
	
	{ //create a cluster
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
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("serverAddress", serverAddress, alloc);
		metadata.AddMember("caData", caData, alloc);
		metadata.AddMember("token", token, alloc);
		metadata.AddMember("namespace", kubeNamespace, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200,
					 "Cluster creation request should succeed");
		ENSURE(!createResp.body.empty());
	}
	
	{ //list when nothing is installed
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances?token="+adminKey);
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
				auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances/"+id+"?token="+key);
		}
	} cleanup(tc,instID,adminKey);
	
	{ //install something
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName, alloc);
		request.AddMember("cluster", clusterName, alloc);
		request.AddMember("configuration", "Instance: install1", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/test-app?test&token="+adminKey,to_string(request));
		ENSURE_EQUAL(instResp.status,200,"Application install request should succeed");
		rapidjson::Document data;
		data.Parse(instResp.body);
		if(data.HasMember("metadata") && data["metadata"].IsObject() && data["metadata"].HasMember("id"))
			instID=data["metadata"]["id"].GetString();
	}
	
	{ //list again
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances?token="+adminKey);
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
	
	std::string adminKey=tc.getPortalToken();
	auto schema=loadSchema(getSchemaDir()+"/InstanceListResultSchema.json");
	
	const std::string groupName1="test-scoped-inst-list1";
	const std::string groupName2="test-scoped-inst-list2";
	const std::string clusterName1="testcluster1";
	const std::string clusterName2="testcluster2";
	
	{ //create a VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName1, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Group creation request should succeed");
	}
	
	{ //create another VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName2, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"Group creation request should succeed");
	}
	
	{ //create a cluster
		auto kubeConfig = tc.getKubeConfig();
		auto caData = tc.getServerCAData();
		auto token = tc.getUserToken();
		auto kubeNamespace = tc.getKubeNamespace();
		auto serverAddress = tc.getServerAddress();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName1, alloc);
		metadata.AddMember("group", groupName1, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("serverAddress", serverAddress, alloc);
		metadata.AddMember("caData", caData, alloc);
		metadata.AddMember("token", token, alloc);
		metadata.AddMember("namespace", kubeNamespace, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200,
					 "Cluster creation request should succeed");
		ENSURE(!createResp.body.empty());
	}
	
	{ //create another cluster
		//slightly evil hack: this is the same cluster, but as long as we 
		//manually avoid name collisions, SLATE won't notice
		auto kubeConfig = tc.getKubeConfig();
		auto caData = tc.getServerCAData();
		auto token = tc.getUserToken();
		auto kubeNamespace = tc.getKubeNamespace();
		auto serverAddress = tc.getServerAddress();
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", clusterName2, alloc);
		metadata.AddMember("group", groupName1, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("serverAddress", serverAddress, alloc);
		metadata.AddMember("caData", caData, alloc);
		metadata.AddMember("token", token, alloc);
		metadata.AddMember("namespace", kubeNamespace, alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, to_string(request));
		ENSURE_EQUAL(createResp.status,200,
					 "Cluster creation request should succeed");
		ENSURE(!createResp.body.empty());
	}
	
	{//grant second Group access to both clusters
		auto accessResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterName1+
		                        "/allowed_groups/"+groupName2+"?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "Group access grant request should succeed: "+accessResp.body);
		accessResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterName2+
		                        "/allowed_groups/"+groupName2+"?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "Group access grant request should succeed: "+accessResp.body);
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
				auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances/"+id+"?token="+key);
		}
	} cleanup(tc,instIDs,adminKey);
	
	//install one application instance on each cluster for each VO
	unsigned int instIdx=0;
	for(const auto& groupName : {groupName1, groupName2}){
		for(const auto& clusterName : {clusterName1, clusterName2}){
			{ //install something
				rapidjson::Document request(rapidjson::kObjectType);
				auto& alloc = request.GetAllocator();
				request.AddMember("apiVersion", currentAPIVersion, alloc);
				request.AddMember("group", groupName, alloc);
				request.AddMember("cluster", clusterName, alloc);
				request.AddMember("configuration", "Instance: inst"+std::to_string(instIdx++), alloc);
				auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/test-app?test&token="
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
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,
		             "Listing application instances should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),4,"All four instances should be returned in the listing");
	}
	
	{ //list things on cluster 1
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances?cluster="
		                      +clusterName1+"&token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,
		             "Listing application instances should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),2,"Two instances should be returned in the listing");
		std::stringstream temp;
		rapidjson::OStreamWrapper os(temp);

		rapidjson::Writer<rapidjson::OStreamWrapper> writer(os);
		data.Accept(writer);
		std::cout << temp.str() << std::endl;
		for(const auto& item : data["items"].GetArray()){
			std::cout << "item metadata:" << item["metadata"]["cluster"].GetString() << std::endl;
			std::cout << "cluster name:" << clusterName1 << std::endl;
			ENSURE_EQUAL(item["metadata"]["cluster"].GetString(),clusterName1,
			             "Only instances on the first cluster should be returned");
		}
		ENSURE(std::string(data["items"][0]["metadata"]["group"].GetString())!=data["items"][1]["metadata"]["group"].GetString());
	}
	
	{ //list things on cluster 2
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances?cluster="
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
		ENSURE(std::string(data["items"][0]["metadata"]["group"].GetString())!=data["items"][1]["metadata"]["group"].GetString());
	}
	
	{ //list things on belonging to Group 1
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances?group="
		                      +groupName1+"&token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,
		             "Listing application instances should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),2,"Two instances should be returned in the listing");
		for(const auto& item : data["items"].GetArray()){
			ENSURE_EQUAL(item["metadata"]["group"].GetString(),groupName1,
			             "Only instances belonging to the first Group should be returned");
		}
		ENSURE(std::string(data["items"][0]["metadata"]["cluster"].GetString())!=data["items"][1]["metadata"]["cluster"].GetString());
	}
	
	{ //list things on belonging to Group 2
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances?group="
		                      +groupName2+"&token="+adminKey);
		ENSURE_EQUAL(listResp.status,200,
		             "Listing application instances should succeed");
		rapidjson::Document data;
		data.Parse(listResp.body);
		ENSURE_CONFORMS(data,schema);
		ENSURE_EQUAL(data["items"].Size(),2,"Two instances should be returned in the listing");
		for(const auto& item : data["items"].GetArray()){
			ENSURE_EQUAL(item["metadata"]["group"].GetString(),groupName2,
			             "Only instances belonging to the second Group should be returned");
		}
		ENSURE(std::string(data["items"][0]["metadata"]["cluster"].GetString())!=data["items"][1]["metadata"]["cluster"].GetString());
	}
	
	//List things on each combination of one cluster and one VO
	for(const auto groupName : {groupName1, groupName2}){
		for(const auto clusterName : {clusterName1, clusterName2}){
			auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances?cluster="
			                      +clusterName+"&group="+groupName+"&token="+adminKey);
			ENSURE_EQUAL(listResp.status,200,
			             "Listing application instances should succeed");
			rapidjson::Document data;
			data.Parse(listResp.body);
			ENSURE_CONFORMS(data,schema);
			ENSURE_EQUAL(data["items"].Size(),1,"One instances should be returned in the listing");
			ENSURE_EQUAL(data["items"][0]["metadata"]["cluster"].GetString(),clusterName,
			             "Only instances on the correct cluster should be returned");
			ENSURE_EQUAL(data["items"][0]["metadata"]["group"].GetString(),groupName,
			             "Only instances belonging to the correct Group should be returned");
		}
	}
}
