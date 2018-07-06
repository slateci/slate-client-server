#include "ClusterCommands.h"

#include "KubeInterface.h"
#include "Logging.h"
#include "Utilities.h"

crow::response listClusters(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list clusters");
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
		clusterResult["kind"]="Cluster";
		crow::json::wvalue clusterData;
		clusterData["id"]=cluster.id;
		clusterData["name"]=cluster.name;
		clusterResult["metadata"]=std::move(clusterData);
		resultItems.emplace_back(std::move(clusterResult));
	}
	result["items"]=std::move(resultItems);
	return result;
}

crow::response createCluster(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to create a cluster");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//TODO: Are all users allowed to create/register clusters?
	//TODO: What other information is required to register a cluster?
	
	//unpack the target cluster info
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
	
	if(cluster.name.find('/')!=std::string::npos)
		return crow::response(400,generateError("Cluster names may not contain slashes"));
	if(cluster.name.find(IDGenerator::clusterIDPrefix)==0)
		return crow::response(400,generateError("Cluster names may not begin with "+IDGenerator::clusterIDPrefix));
	if(store.findClusterByName(cluster.name))
		return crow::response(409,generateError("Cluster name is already in use"));
	
	log_info("Creating " << cluster);
	bool created=store.addCluster(cluster);
	
	if(!created){
		log_error("Failed to create " << cluster);
		return crow::response(500,generateError("Cluster registration failed"));
	}
	
	auto configPath=store.configPathForCluster(cluster.id);
	log_info("Attempting to access " << cluster);
	std::string clusterInfo=kubernetes::kubectl(*configPath,"","cluster-info");
	if(clusterInfo.find("KubeDNS is running")!=std::string::npos)
		log_info("Success contacting " << cluster);
	else{
		log_info("Failure contacting " << cluster << "; deleting its record");
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

crow::response deleteCluster(PersistentStore& store, const crow::request& req, 
                             const std::string& clusterID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to delete " << clusterID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	const Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	//Users can only delete clusters which belong to VOs of which they are members
	if(!store.userInVO(user.id,cluster.owningVO))
		return crow::response(403,generateError("Not authorized"));
	 //TODO: other restrictions on cluster deletions?
	
	log_info("Deleting " << cluster);
	if(!store.removeCluster(cluster.id))
		return(crow::response(500,generateError("Cluster deletion failed")));
	return(crow::response(200));
}

crow::response updateCluster(PersistentStore& store, const crow::request& req, 
                             const std::string& clusterID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to update " << clusterID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	//Users can only edit clusters which belong to VOs of which they are members
	if(!store.userInVO(user.id,cluster.owningVO))
		return crow::response(403,generateError("Not authorized"));
	 //TODO: other restrictions on cluster alterations?
	
	//unpack the new cluster info
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
	
	//the only thing which can be changed is the kubeconfig, so it is required
	if(!body["metadata"].has("kubeconfig"))
		return crow::response(400,generateError("Missing kubeconfig in request"));
	if(body["metadata"]["kubeconfig"].t()!=crow::json::type::String)
		return crow::response(400,generateError("Incorrect type for kubeconfig"));
	
	cluster.config=body["metadata"]["kubeconfig"].s();
	
	log_info("Updating " << cluster);
	bool created=store.updateCluster(cluster);
	
	if(!created){
		log_error("Failed to update " << cluster);
		return crow::response(500,generateError("Cluster update failed"));
	}
	
	return(crow::response(200));
}
