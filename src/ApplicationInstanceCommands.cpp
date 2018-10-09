#include "ApplicationInstanceCommands.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include <yaml-cpp/exceptions.h>
#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/impl.h>
#include "yaml-cpp/node/convert.h"
#include "yaml-cpp/node/detail/impl.h"
#include <yaml-cpp/node/parse.h>

#include "KubeInterface.h"
#include "Logging.h"
#include "Utilities.h"

crow::response listApplicationInstances(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list application instances");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list application instances

	std::vector<ApplicationInstance> instances;

	auto vo = req.url_params.get("vo");
	auto cluster = req.url_params.get("cluster");
	
	if (vo || cluster) {
		std::string voFilter = "";
		std::string clusterFilter = "";		  

		if (vo)
		  voFilter = vo;
		if (cluster)
		  clusterFilter = cluster;
		
		instances=store.listApplicationInstancesByClusterOrVO(voFilter, clusterFilter);
	} else
		instances=store.listApplicationInstances();
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(instances.size(), alloc);
	for(const ApplicationInstance& instance : instances){
		rapidjson::Value instanceResult(rapidjson::kObjectType);
		instanceResult.AddMember("apiVersion", "v1alpha1", alloc);
		instanceResult.AddMember("kind", "ApplicationInstance", alloc);
		rapidjson::Value instanceData(rapidjson::kObjectType);
		instanceData.AddMember("id", instance.id, alloc);
		instanceData.AddMember("name", instance.name, alloc);
		instanceData.AddMember("application", instance.application, alloc);
		instanceData.AddMember("vo", store.getVO(instance.owningVO).name, alloc);
		instanceData.AddMember("cluster", store.getCluster(instance.cluster).name, alloc);
		instanceData.AddMember("created", instance.ctime, alloc);
		instanceResult.AddMember("metadata", instanceData, alloc);
		resultItems.PushBack(instanceResult, alloc);
		//TODO: query helm to get current status (helm list {instance.name})?
	}
	result.AddMember("items", resultItems, alloc);

	return crow::response(to_string(result));
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
                                                   const std::string& nspace,
                                                   const std::string& systemNamespace){
	//first try to get from helm the list of services in the 'release' (instance)
	auto helmInfo=runCommand("helm",
	  {"get",releaseName,"--tiller-namespace",systemNamespace},
	  {{"KUBECONFIG",*configPath}});
	if(helmInfo.status || helmInfo.output.find("Error:")==0){
		log_error(helmInfo.error);
		return {};
	}
	std::vector<YAML::Node> parsedHelm;
	try{
		parsedHelm=YAML::LoadAll(helmInfo.output);
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
		auto listing=kubernetes::kubectl(*configPath,{"get","service",serviceName,"--namespace",nspace});
		if(listing.status){
			log_error("kubectl get service '" << serviceName << "' --namespace '" 
			          << nspace << "' failed: " << listing.error);
		}
		auto lines=string_split_lines(listing.output);
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
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	result.AddMember("kind", "ApplicationInstance", alloc);
	rapidjson::Value instanceData(rapidjson::kObjectType);
	instanceData.AddMember("id", rapidjson::StringRef(instance.id.c_str()), alloc);
	instanceData.AddMember("name", rapidjson::StringRef(instance.name.c_str()), alloc);
	instanceData.AddMember("application", rapidjson::StringRef(instance.application.c_str()),
			       alloc);
	instanceData.AddMember("vo", store.getVO(instance.owningVO).name, alloc);
	instanceData.AddMember("cluster", store.getCluster(instance.cluster).name, alloc);
	instanceData.AddMember("created", rapidjson::StringRef(instance.ctime.c_str()), alloc);
	instanceData.AddMember("configuration", rapidjson::StringRef(instance.config.c_str()),
			       alloc);
	result.AddMember("metadata", instanceData, alloc);

	
	auto configPath=store.configPathForCluster(instance.cluster);
	auto systemNamespace=store.getCluster(instance.cluster).systemNamespace;
	auto services=getServices(configPath,instance.name,vo.namespaceName(),systemNamespace);
	rapidjson::Value serviceData(rapidjson::kArrayType);
	for(const auto& service : services){
		rapidjson::Value serviceEntry(rapidjson::kObjectType);
		serviceEntry.AddMember("name", rapidjson::StringRef(service.first.c_str()), alloc);
		serviceEntry.AddMember("clusterIP", rapidjson::StringRef(service.second.clusterIP.c_str()),
				       alloc);
		serviceEntry.AddMember("externalIP", rapidjson::StringRef(service.second.externalIP.c_str()),
				       alloc);
		serviceEntry.AddMember("ports", rapidjson::StringRef(service.second.ports.c_str()), alloc);
		serviceData.PushBack(serviceEntry, alloc);
	}
	result.AddMember("services", serviceData, alloc);
	
	//TODO: query helm to get current status (helm list {instance.name})

	return crow::response(to_string(result));
}

crow::response deleteApplicationInstance(PersistentStore& store, const crow::request& req, const std::string& instanceID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to delete " << instanceID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	auto instance=store.getApplicationInstance(instanceID);
	if(!instance)
		return crow::response(404,generateError("Application instance not found"));
	//only admins or member of the VO which owns an instance may delete it
	if(!user.admin && !store.userInVO(user.id,instance.owningVO))
		return crow::response(403,generateError("Not authorized"));
	bool force=(req.url_params.get("force")!=nullptr);
	
	
	auto err=internal::deleteApplicationInstance(store,instance,force);
	if(!err.empty())
		return crow::response(500,generateError(err));
	
	return crow::response(200);
}

namespace internal{
std::string deleteApplicationInstance(PersistentStore& store, const ApplicationInstance& instance, bool force){
	log_info("Deleting " << instance);
	try{
		auto configPath=store.configPathForCluster(instance.cluster);
		auto systemNamespace=store.getCluster(instance.cluster).systemNamespace;
		auto helmResult = runCommand("helm",
		  {"delete","--purge",instance.name,"--tiller-namespace",systemNamespace},
		  {{"KUBECONFIG",*configPath}});
		
		if(helmResult.status || 
		   helmResult.output.find("release \""+instance.name+"\" deleted")==std::string::npos){
			std::string message="helm delete failed: " + helmResult.error;
			log_error(message);
			if(!force)
				return message;
			else
				log_info("Forcing deletion of " << instance << " in spite of helm error");
		}
	}
	catch(std::runtime_error& e){
		if(!force)
			return "Failed to delete instance using helm: "+e.what();
		else
			log_info("Forcing deletion of " << instance << " in spite of error");
	}
	
	if(!store.removeApplicationInstance(instance.id)){
		log_error("Failed to delete " << instance << " from persistent store");
		return "Failed to delete instance from database";
	}
	return "";
}
}

crow::response getApplicationInstanceLogs(PersistentStore& store, 
                                          const crow::request& req, 
                                          const std::string& instanceID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested logs from " << instanceID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	auto instance=store.getApplicationInstance(instanceID);
	if(!instance)
		return crow::response(404,generateError("Application instance not found"));
	
	//only admins or member of the VO which owns an instance may delete it
	if(!user.admin && !store.userInVO(user.id,instance.owningVO))
		return crow::response(403,generateError("Not authorized"));
	
	unsigned long maxLines=20; //default is 20
	{
		const char* reqMaxLines=req.url_params.get("max_lines");
		if(reqMaxLines){
			try{
				maxLines=std::stoul(reqMaxLines);
			}
			catch(...){
				//do nothing; leaving maxLines at default is fine
			}
		}
	}
	std::string container;
	{
		const char* reqContainer=req.url_params.get("container");
		if(reqContainer)
			container=reqContainer;
	}
	
	log_info("Sending logs from " << instance << " to " << user);
	auto configPath=store.configPathForCluster(instance.cluster);
	auto systemNamespace=store.getCluster(instance.cluster).systemNamespace;
	
	const VO vo=store.getVO(instance.owningVO);
	const std::string nspace=vo.namespaceName();
	//find out what pods make up this instance
	//This is awful an should be replaced if possible. 
	std::vector<std::string> pods;
	{
		auto helmInfo=runCommand("helm",
		  {"status",instance.name,"--tiller-namespace",systemNamespace},
		  {{"KUBECONFIG",*configPath}});
		if(helmInfo.status){
			log_error("Failed to get helm status for instance " << instance << ": " << helmInfo.error);
			return crow::response(500,generateError("Failed to get instance status"));
		}
		bool foundPods=false, foundDeployments=false;
		for(const auto& line : string_split_lines(helmInfo.output)){
			if(line.find("==> v1/Pod")==0)
				foundPods=true;
			else if(foundPods){
				if(line.empty()){
					foundPods=false;
					continue;
				}
				auto items=string_split_columns(line, ' ', false);
				if(items.empty() || items.front()=="NAME")
					continue;
				pods.push_back(items.front());
			}
		}
		if(pods.empty()){
			log_error("Found no pods for instance " << instance);
			return crow::response(500,generateError("Found no pods for instance"));
		}
	}
	
	std::string logData;
	auto collectLog=[&](const std::string& pod, const std::string& container){
		logData+=std::string(40,'=')+"\nPod: "+pod+" Container: "+container+'\n';
		std::vector<std::string> args={"logs",pod,"-c",container,"-n",nspace};
		if(maxLines)
			args.push_back("--tail="+std::to_string(maxLines));
		auto logResult=kubernetes::kubectl(*configPath,args);
		if(logResult.status){
			logData+="Failed to get logs: ";
			logData+=logResult.error;
			logData+='\n';
		}
		else
			logData+=logResult.output;
	};
	for(const auto& pod : pods){
		//find out what containers are in the pod
		auto containersResult=kubernetes::kubectl(*configPath,{"get","pod",pod,
			"-o=jsonpath={.spec.containers[*].name}","-n",nspace});
		if(containersResult.status){
			log_error("Failed to get pod " << pod << " instance " << instance << ": " << containersResult.error);
			logData+="Failed to get pod "+pod+"\n";
		}
		const auto containers=string_split_columns(containersResult.output, ' ', false);
		
		if(!container.empty()){
			if(std::find(containers.begin(),containers.end(),container)==containers.end())
				collectLog(pod,container);
			else
				logData+="(Pod "+pod+" has no container "+container+")";
		}
		else{ //if not specified iterate over all containers
			for(const auto& container : containers)
				collectLog(pod,container);
		}
	}
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	result.AddMember("kind", "ApplicationInstance", alloc);
	rapidjson::Value instanceData(rapidjson::kObjectType);
	instanceData.AddMember("id", instance.id, alloc);
	instanceData.AddMember("name", instance.name, alloc);
	instanceData.AddMember("application", instance.application, alloc);
	instanceData.AddMember("vo", store.getVO(instance.owningVO).name, alloc);
	instanceData.AddMember("cluster", store.getCluster(instance.cluster).name, alloc);
	instanceData.AddMember("created", instance.ctime, alloc);
	instanceData.AddMember("configuration", instance.config, alloc);
	result.AddMember("metadata", instanceData, alloc);
	result.AddMember("logs", rapidjson::StringRef(logData.c_str()), alloc);
	
	return crow::response(to_string(result));
}
