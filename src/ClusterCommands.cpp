#include "ClusterCommands.h"

#include "KubeInterface.h"
#include "Utilities.h"

crow::response listClusters(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list clusters
	
	std::vector<Cluster> clusters=store.listClusters();
	
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	std::vector<crow::json::wvalue> resultItems;
	resultItems.reserve(clusters.size());
	for(const Cluster& cluster : clusters){
		crow::json::wvalue clusterResult;
		clusterResult["apiVersion"]="v1alpha1";
		clusterResult["kind"]="user";
		crow::json::wvalue clusterData;
		clusterData["ID"]=cluster.id;
		clusterData["name"]=cluster.name;
		clusterResult["metadata"]=std::move(clusterData);
		resultItems.emplace_back(std::move(clusterResult));
	}
	result["items"]=std::move(resultItems);
	return result;
}

crow::response createCluster(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//TODO: Are all users allowed to create/register clusters?
	//TODO: What other information is required to register a cluster?
	
	//unpack the target user info
	crow::json::rvalue body;
	try{
		body = crow::json::load(req.body);
	}catch(std::runtime_error& err){
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	if(!body)
		return crow::response(400,generateError("Invalid JSON in request body"));
	if(!body.has("metadata"))
		return crow::response(400,generateError("Missing user metadata in request"));
	if(body["metadata"].t()!=crow::json::type::Object)
		return crow::response(400,generateError("Incorrect type for metadata"));
	
	if(!body["metadata"].has("name"))
		return crow::response(400,generateError("Missing cluster name in request"));
	if(body["metadata"]["name"].t()!=crow::json::type::String)
		return crow::response(400,generateError("Incorrect type for cluster name"));
	if(!body["metadata"].has("vo"))
		return crow::response(400,generateError("Missing VO ID in request"));
	if(body["metadata"]["vo"].t()!=crow::json::type::String)
		return crow::response(400,generateError("Incorrect type for VO ID"));
	if(!body["metadata"].has("kubeconfig"))
		return crow::response(400,generateError("Missing kubeconfig in request"));
	if(body["metadata"]["kubeconfig"].t()!=crow::json::type::String)
		return crow::response(400,generateError("Incorrect type for kubeconfig"));
	
	Cluster cluster;
	cluster.id=idGenerator.generateClusterID();
	cluster.name=body["metadata"]["name"].s();
	cluster.config=body["metadata"]["kubeconfig"].s();
	cluster.owningVO=body["metadata"]["vo"].s();
	cluster.valid=true;
	
	//users cannot register clusters to VOs to which they do not belong
	if(!store.userInVO(user.id,cluster.owningVO))
		return crow::response(403,generateError("Not authorized"));
	
	bool created=store.addCluster(cluster);
	
	if(!created)
		return crow::response(500,generateError("Cluster registration failed"));
	
	auto configFile=store.configPathForCluster(cluster.id);
	std::cout << "Attempting to access cluster" << std::endl;
	auto clusterInfo=kubernetes::kubectl(configFile,"","cluster-info");
	if(clusterInfo.find("KubeDNS is running")!=std::string::npos)
		std::cout << "Success contacting cluster" << std::endl;
	else{
		std::cout << "Failure contacting cluster" << std::endl;
		//things aren't working, delete our apparently non-functional record
		store.removeCluster(cluster.id);
		return crow::response(500,generateError("Cluster registration failed: "
												"Unable to contact cluster with kubectl"));
	}
	
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	result["kind"]="Cluster";
	crow::json::wvalue metadata;
	metadata["id"]=cluster.id;
	metadata["name"]=cluster.name;
	result["metadata"]=std::move(metadata);
	return crow::response(result);
}

crow::response deleteCluster(PersistentStore& store, const crow::request& req, const std::string& clusterID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	const Cluster& cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	//Users can only delete clusters which belong to VOs of which they are members
	if(!store.userInVO(user.id,cluster.owningVO))
		return crow::response(403,generateError("Not authorized"));
	 //TODO: other restrictions on cluster deletions?
	
	//TODO: implement this, instead of pretending success every time
	if(!store.removeCluster(cluster.id))
		return(crow::response(500,generateError("Cluster deletion failed")));
	return(crow::response(200));
}
