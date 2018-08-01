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

	return crow::response(to_string(result));
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
	try{
		body.Parse(req.body);
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
	
	//normalize owning VO
	if(cluster.owningVO.find(IDGenerator::voIDPrefix)!=0){
		//if a name, find the corresponding VO
		VO vo=store.findVOByName(cluster.owningVO);
		//if no such VO exists, no one can install on its behalf
		if(!vo)
			return crow::response(403,generateError("Not authorized"));
		//otherwise, get the actual VO ID and continue with the lookup
		cluster.owningVO=vo.id;
	}
	
	//users cannot register clusters to VOs to which they do not belong
	if(!store.userInVO(user.id,cluster.owningVO))
		return crow::response(403,generateError("Not authorized"));
	
	if(cluster.name.find('/')!=std::string::npos)
		return crow::response(400,generateError("Cluster names may not contain slashes"));
	if(cluster.name.find(IDGenerator::clusterIDPrefix)==0)
		return crow::response(400,generateError("Cluster names may not begin with "+IDGenerator::clusterIDPrefix));
	if(store.findClusterByName(cluster.name))
		return crow::response(400,generateError("Cluster name is already in use"));
	
	log_info("Creating " << cluster);
	bool created=store.addCluster(cluster);
	
	if(!created){
		log_error("Failed to create " << cluster);
		return crow::response(500,generateError("Cluster registration failed"));
	}
	
	auto configPath=store.configPathForCluster(cluster.id);
	log_info("Attempting to access " << cluster);
	auto clusterInfo=kubernetes::kubectl(*configPath,"","cluster-info");
	if(clusterInfo.status || 
	   clusterInfo.output.find("Kubernetes master is running")==std::string::npos){
		log_info("Failure contacting " << cluster << "; deleting its record");
		//things aren't working, delete our apparently non-functional record
		store.removeCluster(cluster.id);
		return crow::response(500,generateError("Cluster registration failed: "
												"Unable to contact cluster with kubectl"));
	}
	else
		log_info("Success contacting " << cluster);
	//As long as we are stuck with helm 2, we need tiller running on the cluster
	//Make sure that is is.
	auto commandResult = runCommand("export KUBECONFIG='"+*configPath+"'; helm init");
	auto expected="Tiller (the Helm server-side component) has been installed";
	auto already="Tiller is already installed";
	if(commandResult.status || 
	   (commandResult.output.find(expected)==std::string::npos &&
	    commandResult.output.find(already)==std::string::npos)){
		log_info("Problem initializing helm on " << cluster << "; deleting its record");
		//things aren't working, delete our apparently non-functional record
		store.removeCluster(cluster.id);
		return crow::response(500,generateError("Cluster registration failed: "
		                                        "Unable to initialize helm"));
	}
	log_info("Checking for running tiller. . . ");
	int delaySoFar=0;
	const int maxDelay=30000, delay=500;
	bool tillerRunning=false;
	while(!tillerRunning){
		auto commandResult = kubernetes::kubectl(*configPath,"","get pods --namespace kube-system");
		if(commandResult.status){
			log_error("Checking tiller status on " << cluster << " failed");
			break;
		}
		auto lines=string_split_lines(commandResult.output);
		for(const auto& line : lines){
			auto tokens=string_split_columns(line, ' ', false);
			if(tokens.size()<3)
				continue;
			if(tokens[0].find("tiller-deploy")==std::string::npos)
				continue;
			auto slashPos=tokens[1].find('/');
			if(slashPos==std::string::npos || slashPos==0 || slashPos+1==tokens[1].size())
				break;
			std::string numers=tokens[1].substr(0,slashPos);
			std::string denoms=tokens[1].substr(slashPos+1);
			try{
				unsigned long numer=std::stoul(numers);
				unsigned long denom=std::stoul(denoms);
				if(numer>0 && numer==denom){
					tillerRunning=true;
					break;
				}
			}catch(...){
				break;
			}
		}
		
		if(!tillerRunning){
			if(delaySoFar<maxDelay){
				std::this_thread::sleep_for(std::chrono::milliseconds(delay));
				delaySoFar+=delay;
			}
			else{
				log_error("Waiting for tiller readiness on " << cluster << " timed out");
				break;
			}
		}
	}
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	result.AddMember("kind", "Cluster", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", rapidjson::StringRef(cluster.id.c_str()), alloc);
	metadata.AddMember("name", rapidjson::StringRef(cluster.name.c_str()), alloc);
	result.AddMember("metadata", metadata, alloc); 

	return crow::response(to_string(result));
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
			auto helmResult = runCommand("export KUBECONFIG='"+*configPath+
			                             "'; helm delete --purge " + instance.name);
			if(helmResult.status)
				log_error("helm delete --purge " + instance.name << " failed");
		}
	}

	// Delete namespaces remaining on the cluster
	log_info("Deleting namespaces on cluster " << cluster.id);
	auto vos = store.listVOs();
	for (const VO& vo : vos){
		auto deleteResult = runCommand("kubectl --kubeconfig " + *configPath +
					       " delete namespace " + vo.namespaceName() + " 2>&1");
		if(deleteResult.status)
			log_error("kubectl delete namespace " + vo.namespaceName() << " failed");
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

crow::response listClusterAllowedVOs(PersistentStore& store, const crow::request& req, 
                                     const std::string& clusterID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list VOs with access to cluster " << clusterID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list allowed VOs
	
	Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	std::vector<std::string> voIDs=store.listVOsAllowedOnCluster(cluster.id);
	voIDs.push_back(cluster.owningVO); //include the owning VO, which implcitly always has access
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(voIDs.size(), alloc);
	for (const std::string& voID : voIDs){
		VO vo=store.findVOByID(voID);
		if(!vo){
			log_error("Apparently invalid VO ID " << voID 
			          << " listed for access to " << cluster);
			continue;
		}
		
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("id", voID, alloc);
		metadata.AddMember("name", vo.name, alloc);
		
		rapidjson::Value voResult(rapidjson::kObjectType);
		voResult.AddMember("apiVersion", "v1alpha1", alloc);
		voResult.AddMember("kind", "VO", alloc);
		voResult.AddMember("metadata", metadata, alloc);
		resultItems.PushBack(voResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);
	
	return crow::response(to_string(result));
}

crow::response grantVOClusterAccess(PersistentStore& store, const crow::request& req, 
                                    const std::string& clusterID, const std::string& voID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to grant VO " << voID << " access to cluster " << clusterID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//validate input
	const Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	const VO vo=store.getVO(voID);
	if(!vo)
		return crow::response(404,generateError("VO not found"));
	
	//only admins and cluster owners can grant other VOs' access
	if(!user.admin && !store.userInVO(user.id,cluster.owningVO))
		return crow::response(403,generateError("Not authorized"));
	
	log_info("Granting " << vo << " access to " << cluster);
	bool success=store.addVOToCluster(vo.id,cluster.id);
	
	if(!success)
		return crow::response(500,generateError("Granting VO access to cluster failed"));
	return(crow::response(200));
}

crow::response revokeVOClusterAccess(PersistentStore& store, const crow::request& req, 
                                     const std::string& clusterID, const std::string& voID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to revoke VO " << voID << " access to cluster " << clusterID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//validate input
	const Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	const VO vo=store.getVO(voID);
	if(!vo)
		return crow::response(404,generateError("VO not found"));
	
	//only admins and cluster owners can change other VOs' access
	if(!user.admin && !store.userInVO(user.id,cluster.owningVO))
		return crow::response(403,generateError("Not authorized"));
	
	log_info("Removing " << vo << " access to " << cluster);
	bool success=store.removeVOFromCluster(vo.id,cluster.id);
	
	if(!success)
		return crow::response(500,generateError("Removing VO access to cluster failed"));
	return(crow::response(200));
}