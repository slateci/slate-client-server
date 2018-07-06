#include "ApplicationInstanceCommands.h"

#include <yaml-cpp/exceptions.h>
#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/impl.h>
#include "yaml-cpp/node/convert.h"
#include "yaml-cpp/node/detail/impl.h"
#include <yaml-cpp/node/parse.h>

#include "Logging.h"
#include "Utilities.h"

crow::response listApplicationInstances(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list application instances");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list application instances
	
	auto instances=store.listApplicationInstances();
	
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	//TODO: compose actual result list
	std::vector<crow::json::wvalue> resultItems;
	resultItems.reserve(instances.size());
	for(const ApplicationInstance& instance : instances){
		crow::json::wvalue instanceResult;
		instanceResult["apiVersion"]="v1alpha1";
		instanceResult["kind"]="ApplicationInstance";
		crow::json::wvalue instanceData;
		instanceData["id"]=instance.id;
		instanceData["name"]=instance.name;
		instanceData["application"]=instance.application;
		instanceData["vo"]=instance.owningVO;
		instanceData["cluster"]=instance.cluster;
		instanceData["created"]=instance.ctime;
		instanceResult["metadata"]=std::move(instanceData);
		resultItems.emplace_back(std::move(instanceResult));
		//TODO: query helm to get current status (helm list {instance.name})?
	}
	result["items"]=std::move(resultItems);
	return crow::response(result);
}

struct ServiceInterface{
	//represent IP addresses as strings because
	//1) it's simple
	//2) it easily covers edges case like <none> and <pending>
	//3) we don't use these programmatically, we only report them to a human
	std::string clusterIP;
	std::string externalIP;
	std::string ports;
};

///query helm and kubernetes to find out what services a given instance contains 
///and how to contact them
std::map<std::string,ServiceInterface> getServices(const SharedFileHandle& configPath, 
                                                   const std::string& releaseName, 
                                                   const std::string& nspace){
	//first try to get from helm the list of services in the 'release' (instance)
	std::string helmInfo=runCommand("export KUBECONFIG='"+*configPath+
	                                "'; helm get '"+releaseName+"'");
	if(helmInfo.find("Error:")==0){
		log_error(helmInfo);
		return {};
	}
	std::vector<YAML::Node> parsedHelm;
	try{
		parsedHelm=YAML::LoadAll(helmInfo);
	}catch(const YAML::ParserException& ex){
		log_error("Unable to parse output of `helm get " << releaseName << "`: " << ex.what());
		return {};
	}
	std::vector<std::string> serviceNames;
	for(const auto& document : parsedHelm){
		if(document.IsMap() && document["kind"] && document["kind"].as<std::string>()=="Service"){
			if(!document["metadata"])
				log_error("service document has no metadata");
			else{
				if(!document["metadata"]["name"])
					log_error("service document metadata has no name");
				else
					serviceNames.push_back(document["metadata"]["name"].as<std::string>());
			}
		}
	}
	//next try to find out the interface of each service
	std::map<std::string,ServiceInterface> services;
	for(const auto& serviceName : serviceNames){
		auto listing=runCommand("export KUBECONFIG='"+*configPath+
		                        "'; kubectl get service '"+serviceName+"'"
								" --namespace '"+nspace+"'");
		auto lines=string_split_lines(listing);
		for(std::size_t i=1; i<lines.size(); i++){
			auto tokens=string_split_columns(lines[i], ' ', false);
			if(tokens.size()<6)
				continue;
			ServiceInterface interface;
			interface.clusterIP=tokens[2];
			interface.externalIP=tokens[3];
			interface.ports=tokens[4];
			services.emplace(std::make_pair(serviceName,interface));
		}
	}
	return services;
}

crow::response fetchApplicationInstanceInfo(PersistentStore& store, const crow::request& req, const std::string& instanceID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested information about " << instanceID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	auto instance=store.getApplicationInstance(instanceID);
	if(!instance)
		return crow::response(404,generateError("Application instance not found"));
	
	//only admins or member of the VO which owns an instance may query it
	if(!user.admin && !store.userInVO(user.id,instance.owningVO))
		return crow::response(403,generateError("Not authorized"));
	
	//fetch the full configuration for the instance
	instance.config=store.getApplicationInstanceConfig(instanceID);
	
	//get information on the owning VO, needed to look up services, etc.
	const VO vo=store.getVO(instance.owningVO);
	
	//TODO: serialize the instance configuration as JSON
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	result["kind"]="ApplicationInstance";
	crow::json::wvalue instanceData;
	instanceData["id"]=instance.id;
	instanceData["name"]=instance.name;
	instanceData["application"]=instance.application;
	instanceData["vo"]=instance.owningVO;
	instanceData["cluster"]=instance.cluster;
	instanceData["created"]=instance.ctime;
	instanceData["config"]=instance.config;
	result["metadata"]=std::move(instanceData);
	
	auto configPath=store.configPathForCluster(instance.cluster);
	auto services=getServices(configPath,instance.name,vo.namespaceName());
	std::vector<crow::json::wvalue> serviceData;
	for(const auto& service : services){
		crow::json::wvalue serviceEntry;
		serviceEntry["name"]=service.first;
		serviceEntry["clusterIP"]=service.second.clusterIP;
		serviceEntry["externalIP"]=service.second.externalIP;
		serviceEntry["ports"]=service.second.ports;
		serviceData.emplace_back(std::move(serviceEntry));
	}
	result["services"]=std::move(serviceData);
	
	//TODO: query helm to get current status (helm list {instance.name})
	
	return result;
}

crow::response deleteApplicationInstance(PersistentStore& store, const crow::request& req, const std::string& instanceID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested delete " << instanceID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	auto instance=store.getApplicationInstance(instanceID);
	if(!instance)
		return crow::response(404,generateError("Application instance not found"));
	//only admins or member of the VO which owns an instance may delete it
	if(!user.admin && !store.userInVO(user.id,instance.owningVO))
		return crow::response(403,generateError("Not authorized"));
	
	log_info("Deleting " << instance);
	auto configPath=store.configPathForCluster(instance.cluster);
	std::string helmResult;
	helmResult = runCommand("export KUBECONFIG='"+*configPath+
	                        "'; helm delete --purge " + instance.name);
	
	if(helmResult.find("release \""+instance.name+"\" deleted")==std::string::npos){
		std::string message="helm delete failed: " + helmResult;
		log_error(message);
		return crow::response(500,generateError(message));
	}
	
	if(!store.removeApplicationInstance(instanceID)){
		log_error("Failed to delete " << instance << " from persistent store");
		return(crow::response(500,generateError("Instance deletion from database failed")));
	}
	
	return crow::json::wvalue();
}
