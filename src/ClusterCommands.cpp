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

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(clusters.size(), alloc);
	for(const Cluster& cluster : clusters){
		rapidjson::Value clusterResult(rapidjson::kObjectType);
		clusterResult.AddMember("apiVersion", "v1alpha1", alloc);
		clusterResult.AddMember("kind", "Cluster", alloc);
		rapidjson::Value clusterData(rapidjson::kObjectType);
		clusterData.AddMember("id", rapidjson::StringRef(cluster.id.c_str()), alloc);
		clusterData.AddMember("name", rapidjson::StringRef(cluster.name.c_str()), alloc);
		clusterResult.AddMember("metadata", clusterData, alloc);
		resultItems.PushBack(clusterResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);

	rapidjson::StringBuffer resultBuffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(resultBuffer);
	result.Accept(writer);
	return crow::response(resultBuffer.GetString());
}

crow::response createCluster(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to create a cluster");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//TODO: Are all users allowed to create/register clusters?
	//TODO: What other information is required to register a cluster?
	
	//unpack the target cluster info
	rapidjson::Document body;
	std::string bodystr = fixInvalidEscapes(req.body);
	try{
		body.Parse(bodystr.c_str());
	}catch(std::runtime_error& err){
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	
	if(body.IsNull()) {
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	if(!body.HasMember("metadata"))
		return crow::response(400,generateError("Missing user metadata in request"));
	if(!body["metadata"].IsObject())
		return crow::response(400,generateError("Incorrect type for metadata"));
	
	if(!body["metadata"].HasMember("name"))
		return crow::response(400,generateError("Missing cluster name in request"));
	if(!body["metadata"]["name"].IsString())
		return crow::response(400,generateError("Incorrect type for cluster name"));
	if(!body["metadata"].HasMember("vo"))
		return crow::response(400,generateError("Missing VO ID in request"));
	if(!body["metadata"]["vo"].IsString())
		return crow::response(400,generateError("Incorrect type for VO ID"));
	if(!body["metadata"].HasMember("kubeconfig"))
		return crow::response(400,generateError("Missing kubeconfig in request"));
	if(!body["metadata"]["kubeconfig"].IsString())
		return crow::response(400,generateError("Incorrect type for kubeconfig"));
	
	Cluster cluster;
	cluster.id=idGenerator.generateClusterID();
	cluster.name=body["metadata"]["name"].GetString();
	cluster.config=body["metadata"]["kubeconfig"].GetString();
	cluster.owningVO=body["metadata"]["vo"].GetString();
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
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	result.AddMember("kind", "Cluster", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", rapidjson::StringRef(cluster.id.c_str()), alloc);
	metadata.AddMember("name", rapidjson::StringRef(cluster.name.c_str()), alloc);
	result.AddMember("metadata", metadata, alloc); 

	rapidjson::StringBuffer resultBuffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(resultBuffer);
	result.Accept(writer);
	return crow::response(resultBuffer.GetString());
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

	// Delete any remaining instances that are present on the cluster
	auto configPath=store.configPathForCluster(cluster.id);
	auto instances=store.listApplicationInstances();
	for (const ApplicationInstance& instance : instances){
		if (instance.cluster == cluster.id) {
			log_info("Deleting instance " << instance.id << " on cluster " << cluster.id);
			store.removeApplicationInstance(instance.id);
			std::string helmResult = runCommand("export KUBECONFIG='"+*configPath+
							    "'; helm delete --purge " + instance.name);
		}
	}

	// Delete namespaces remaining on the cluster
	log_info("Deleting namespaces on cluster " << cluster.id);
	auto vos = store.listVOs();
	for (const VO& vo : vos){
		auto deleteResult = runCommand("kubectl --kubeconfig " + *configPath +
					       " delete namespace " + vo.namespaceName() + " 2>&1");
	}
	
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
	rapidjson::Document body;
	try{
		body.Parse(req.body.c_str());
	}catch(std::runtime_error& err){
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	if(body.IsNull())
		return crow::response(400,generateError("Invalid JSON in request body"));
	if(!body.HasMember("metadata"))
		return crow::response(400,generateError("Missing user metadata in request"));
	if(!body["metadata"].IsObject())
		return crow::response(400,generateError("Incorrect type for metadata"));
	
	//the only thing which can be changed is the kubeconfig, so it is required
	if(!body["metadata"].HasMember("kubeconfig"))
		return crow::response(400,generateError("Missing kubeconfig in request"));
	if(!body["metadata"]["kubeconfig"].IsString())
		return crow::response(400,generateError("Incorrect type for kubeconfig"));
	
	cluster.config=body["metadata"]["kubeconfig"].GetString();
	
	log_info("Updating " << cluster);
	bool created=store.updateCluster(cluster);
	
	if(!created){
		log_error("Failed to update " << cluster);
		return crow::response(500,generateError("Cluster update failed"));
	}
	
	return(crow::response(200));
}
