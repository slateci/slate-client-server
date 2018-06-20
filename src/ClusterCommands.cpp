#include "ClusterCommands.h"

#include "Utilities.h"

crow::response listClusters(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list clusters
	
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	//TODO: compose actual result list
	result["items"]=std::vector<std::string>{};
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
		return crow::response(400,generateError("Incorrect type for configuration"));
	
	if(!user.admin) //only administrators can create new users
		return crow::response(403,generateError("Not authorized"));
	
	if(!body["metadata"].has("name"))
		return crow::response(400,generateError("Missing cluster ID in request"));
	if(body["metadata"]["name"].t()!=crow::json::type::String)
		return crow::response(400,generateError("Incorrect type for cluster ID"));
	if(!body["metadata"].has("vo"))
		return crow::response(400,generateError("Missing VO ID in request"));
	if(body["metadata"]["vo"].t()!=crow::json::type::String)
		return crow::response(400,generateError("Incorrect type for VO ID"));
	
	Cluster cluster;
	cluster.id=idGenerator.generateClusterID();
	cluster.name=body["metadata"]["name"].s();
	cluster.valid=true;
	std::string owningVO=body["metadata"]["vo"].s();
	
	//TODO: create the cluster here!
	bool created=false;
	
	if(!created)
		return crow::response(500,generateError("Cluster registration failed"));
	
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	result["kind"]="Cluster";
	crow::json::wvalue metadata;
	metadata["id"]=cluster.id;
	metadata["name"]=cluster.name;
	result["metadata"]=std::move(metadata);
	return crow::response(result);
	
	//TODO: implement this, instead of pretending success every time
	return(crow::response(200));
}

crow::response deleteCluster(PersistentStore& store, const crow::request& req, const std::string& clusterID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//TODO: Only cluster admins for a given cluster are allowed to delete it
	//TODO: What other information is required to register a cluster?
	
	//TODO: implement this, instead of pretending success every time
	//if(!clusterExists(clusterID))
	//	return(crow::response(404,generateError("Cluster not found")));
	return(crow::response(200));
}
