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
#include "ServerUtilities.h"

#include <chrono>

crow::response listApplicationInstances(PersistentStore& store, const crow::request& req){
	using namespace std::chrono;
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list application instances");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list application instances

	std::vector<ApplicationInstance> instances;

	auto group = req.url_params.get("group");
	auto cluster = req.url_params.get("cluster");
	
	if (group || cluster) {
		std::string groupFilter = "";
		std::string clusterFilter = "";		  

		if (group)
		  groupFilter = group;
		if (cluster)
		  clusterFilter = cluster;
		
		instances=store.listApplicationInstancesByClusterOrGroup(groupFilter, clusterFilter);
	} else
		instances=store.listApplicationInstances();
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(instances.size(), alloc);
	for(const ApplicationInstance& instance : instances){
		rapidjson::Value instanceResult(rapidjson::kObjectType);
		instanceResult.AddMember("apiVersion", "v1alpha3", alloc);
		instanceResult.AddMember("kind", "ApplicationInstance", alloc);
		rapidjson::Value instanceData(rapidjson::kObjectType);
		instanceData.AddMember("id", instance.id, alloc);
		instanceData.AddMember("name", instance.name, alloc);
		std::string application=instance.application;
		if(application.find('/')!=std::string::npos && application.find('/')<application.size()-1)
			application=application.substr(application.find('/')+1);
		instanceData.AddMember("application", application, alloc);
		instanceData.AddMember("group", store.getGroup(instance.owningGroup).name, alloc);
		instanceData.AddMember("cluster", store.getCluster(instance.cluster).name, alloc);
		instanceData.AddMember("created", instance.ctime, alloc);
		instanceResult.AddMember("metadata", instanceData, alloc);
		resultItems.PushBack(instanceResult, alloc);
		//TODO: query helm to get current status (helm list {instance.name})?
	}
	result.AddMember("items", resultItems, alloc);

	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	log_info("instance listing completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
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
	using namespace std::chrono;
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	auto helmInfo=runCommand("helm",
	  {"get",releaseName,"--tiller-namespace",systemNamespace},
	  {{"KUBECONFIG",*configPath}});
	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	log_info("helm get completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
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
		ServiceInterface interface;
		
		t1 = high_resolution_clock::now();
		auto serviceResult=kubernetes::kubectl(*configPath,{"get","service",serviceName,"--namespace",nspace,"-o=json"});
		t2 = high_resolution_clock::now();
		log_info("kubectl get service completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
		if(serviceResult.status){
			log_error("kubectl get service '" << serviceName << "' --namespace '" 
			          << nspace << "' failed: " << serviceResult.error);
			continue;
		}
		rapidjson::Document serviceData;
		try{
			serviceData.Parse(serviceResult.output.c_str());
		}catch(std::runtime_error& err){
			log_error("Unable to parse kubectl get service JSON output for " << nspace << "::" << serviceName << ": " << err.what());
			continue;
		}
		
		interface.clusterIP=serviceData["spec"]["clusterIP"].GetString();
		
		//TODO: generalize to lift this limitation?
		if(serviceData["spec"]["ports"].GetArray().Size()!=1){
			log_error(nspace << "::" << serviceName << " does not expose exactly one port");
			continue;
		}
		interface.ports="";
		if(serviceData["spec"]["ports"][0].HasMember("port")
		   && serviceData["spec"]["ports"][0]["port"].IsInt())
			interface.ports+=std::to_string(serviceData["spec"]["ports"][0]["port"].GetInt());
		interface.ports+=":";
		if(serviceData["spec"]["ports"][0].HasMember("nodePort")
		   && serviceData["spec"]["ports"][0]["nodePort"].IsInt())
			interface.ports+=std::to_string(serviceData["spec"]["ports"][0]["nodePort"].GetInt());
		interface.ports+="/";
		if(serviceData["spec"]["ports"][0].HasMember("protocol")
		   && serviceData["spec"]["ports"][0]["protocol"].IsString())
			interface.ports+=serviceData["spec"]["ports"][0]["protocol"].GetString();
		
		std::string serviceType=serviceData["spec"]["type"].GetString();
		
		if(serviceType=="LoadBalancer"){
			if(serviceData["status"]["loadBalancer"].HasMember("ingress")
			   && serviceData["status"]["loadBalancer"]["ingress"].IsArray()
			   && serviceData["status"]["loadBalancer"]["ingress"].GetArray().Size()>0
			   && serviceData["status"]["loadBalancer"]["ingress"][0].IsObject()
			   && serviceData["status"]["loadBalancer"]["ingress"][0].HasMember("ip"))
				interface.externalIP=serviceData["status"]["loadBalancer"]["ingress"][0]["ip"].GetString();
			else
				interface.externalIP="<pending>";
		}
		else if(serviceType=="NodePort"){
			//need to track down the pod to which the service is connected in order to find out the IP of its host (node)
			//first accumulate the selector expression used to identify the pod
			std::string filter;
			for(const auto& selector : serviceData["spec"]["selector"].GetObject()){
				if(!filter.empty())
					filter+=",";
				filter+=selector.name.GetString()+std::string("=")+selector.value.GetString();
			}
			//now try to locate the pod in question
			t1 = high_resolution_clock::now();
			auto podResult=kubernetes::kubectl(*configPath,{"get","pod","-l",filter,"--namespace",nspace,"-o=json"});
			t2 = high_resolution_clock::now();
			log_info("kubectl get pod completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
			if(serviceResult.status){
				log_error("kubectl get pod -l " << filter << " --namespace " 
				          << nspace << " failed: " << podResult.error);
				continue;
			}
			rapidjson::Document podData;
			try{
				podData.Parse(podResult.output.c_str());
			}catch(std::runtime_error& err){
				log_error("Unable to parse kubectl get service JSON output for kubectl get pod -l " 
				          << filter << " --namespace " << nspace << ": " << err.what());
				continue;
			}
			if(podData["items"].GetArray().Size()==0){
				log_error("Did not find any pods matching service selector for " << nspace << "::" << serviceName);
				continue;
			}
			if(podData["items"][0]["status"].HasMember("hostIP"))
				interface.externalIP=podData["items"][0]["status"]["hostIP"].GetString();
			else
				interface.externalIP="<none>";
		}
		else if(serviceType=="ClusterIP"){
			log_info("Not reporting internal service " << serviceName);
			continue;
		}
		else{
			log_error("Unexpected service type: "+serviceType);
		}
		services.emplace(std::make_pair(serviceName,interface));
	}
	return services;
}

namespace internal{
///Find out what pods make up an instance
std::vector<std::string> findInstancePods(const ApplicationInstance& instance, 
                                          const std::string& systemNamespace, 
                                          const std::string& clusterConfig){
	std::vector<std::string> pods;
	//This is awful an should be replaced if possible. 
	using namespace std::chrono;
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	auto helmInfo=runCommand("helm",
							 {"status",instance.name,"--tiller-namespace",systemNamespace},
							 {{"KUBECONFIG",clusterConfig}});
	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	log_info("helm status completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
	if(helmInfo.status){
		log_error("Failed to get helm status for instance " << instance << ": " << helmInfo.error);
		throw std::runtime_error("Failed to get helm status for instance: " + helmInfo.error);
	}
	bool foundPods=false;
	for(const auto& line : string_split_lines(helmInfo.output)){
		if(line.find("==> v1/Pod")==0)
			foundPods=true;
		else if(foundPods){
			if(line.find("==>")==0 || line.find("NOTES")==0){
				foundPods=false;
				break;
			}
			auto items=string_split_columns(line, ' ', false);
			if(items.empty() || items.front()=="NAME")
				continue;
			pods.push_back(items.front());
		}
	}
	if(pods.empty()){
		log_error("Found no pods for instance " << instance);
		throw std::runtime_error("Found no pods for instance");
	}
	return pods;
}
}

///\pre authorization must have already been checked
///\throws std::runtime_error
rapidjson::Value fetchInstanceDetails(PersistentStore& store, 
                                      const ApplicationInstance& instance, 
                                      const std::string& systemNamespace, 
                                      rapidjson::Document::AllocatorType& alloc){
	rapidjson::Value instanceDetails(rapidjson::kObjectType);
	
	const Group group=store.getGroup(instance.owningGroup);
	const std::string nspace=group.namespaceName();
	auto configPath=store.configPathForCluster(instance.cluster);
	//find out what pods make up this instance
	std::vector<std::string> pods;
	pods=internal::findInstancePods(instance, systemNamespace, *configPath);
	
	using namespace std::chrono;
	high_resolution_clock::time_point t1,t2;
	
	rapidjson::Value podDetails(rapidjson::kArrayType);
	for(const auto& pod : pods){
		rapidjson::Value podInfo(rapidjson::kObjectType);
		t1 = high_resolution_clock::now();
		auto result=kubernetes::kubectl(*configPath,{"get","pod",pod,"-n",nspace,"-o=json"});
		t2 = high_resolution_clock::now();
		log_info("kubectl get pod completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
		if(result.status){
			podInfo.AddMember("kind", "Error", alloc);
			podInfo.AddMember("message", "Failed to get information for pod "+pod, alloc);
			podDetails.PushBack(podInfo,alloc);
			continue;
		}
		
		rapidjson::Document data(rapidjson::kObjectType,&alloc);
		try{
			data.Parse(result.output.c_str());
		}catch(std::runtime_error& err){
			podInfo.AddMember("kind", "Error", alloc);
			podInfo.AddMember("message", "Failed to parse information for pod "+pod, alloc);
			podDetails.PushBack(podInfo,alloc);
			continue;
		}
		
		if(data.HasMember("metadata")){
			if(data["metadata"].HasMember("creationTimestamp"))
				podInfo.AddMember("created",data["metadata"]["creationTimestamp"],alloc);
			if(data["metadata"].HasMember("name"))
				podInfo.AddMember("name",data["metadata"]["name"],alloc);
		}
		if(data.HasMember("spec")){
			if(data["spec"].HasMember("nodeName"))
				podInfo.AddMember("hostName",data["spec"]["nodeName"],alloc);
		}
		//ownerReferences?
		if(data.HasMember("status")){
			if(data["status"].HasMember("hostIP"))
				podInfo.AddMember("hostIP",data["status"]["hostIP"],alloc);
			if(data["status"].HasMember("phase"))
				podInfo.AddMember("status",data["status"]["phase"],alloc);
			if(data["status"].HasMember("conditions"))
				podInfo.AddMember("conditions",data["status"]["conditions"],alloc);
			if(data["status"].HasMember("containerStatuses")){
				rapidjson::Value containers(rapidjson::kArrayType);
				for(auto& item : data["status"]["containerStatuses"].GetArray()){
					rapidjson::Value container(rapidjson::kObjectType);
					if(item.HasMember("image"))
						container.AddMember("image",item["image"],alloc);
					if(item.HasMember("name"))
						container.AddMember("name",item["name"],alloc);
					if(item.HasMember("ready"))
						container.AddMember("ready",item["ready"],alloc);
					if(item.HasMember("restartCount"))
						container.AddMember("restartCount",item["restartCount"],alloc);
					if(item.HasMember("state"))
						container.AddMember("state",item["state"],alloc);
					containers.PushBack(container,alloc);
				}
				podInfo.AddMember("containers",containers,alloc);
			}
		}
		
		//Also try to fetch events associated with the pod
		result=kubernetes::kubectl(*configPath,{"get","event","--field-selector","involvedObject.name="+pod,"-n",nspace,"-o=json"});
		if(result.status)
			log_warn("kubectl get event failed for pod " << pod << " in namespace " << nspace);
		else{
			bool haveEventData=false;
			try{
				data.Parse(result.output.c_str());
				haveEventData=true;
			}catch(std::runtime_error& err){
				log_warn("Unable to parse event data as JSON");
			}
			if(haveEventData && data.HasMember("items") && data["items"].IsArray()){
				rapidjson::Value events(rapidjson::kArrayType);
				for(auto& item : data["items"].GetArray()){
					rapidjson::Value eventInfo(rapidjson::kObjectType);
					if(item.HasMember("count"))
						eventInfo.AddMember("count",item["count"],alloc);
					if(item.HasMember("firstTimestamp"))
						eventInfo.AddMember("firstTimestamp",item["firstTimestamp"],alloc);
					if(item.HasMember("lastTimestamp"))
						eventInfo.AddMember("lastTimestamp",item["lastTimestamp"],alloc);
					if(item.HasMember("reason"))
						eventInfo.AddMember("reason",item["reason"],alloc);
					if(item.HasMember("message"))
						eventInfo.AddMember("message",item["message"],alloc);
					events.PushBack(eventInfo,alloc);
				}
				podInfo.AddMember("events",events,alloc);
			}
		}
		
		podDetails.PushBack(podInfo,alloc);
	}
	instanceDetails.AddMember("pods",podDetails,alloc);
	
	return instanceDetails;
}

crow::response fetchApplicationInstanceInfo(PersistentStore& store, const crow::request& req, const std::string& instanceID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested information about " << instanceID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	auto instance=store.getApplicationInstance(instanceID);
	if(!instance)
		return crow::response(404,generateError("Application instance not found"));
	
	//only admins or member of the Group which owns an instance may query it
	if(!user.admin && !store.userInGroup(user.id,instance.owningGroup))
		return crow::response(403,generateError("Not authorized"));
	
	//fetch the full configuration for the instance
	instance.config=store.getApplicationInstanceConfig(instanceID);
	
	//get information on the owning Group, needed to look up services, etc.
	const Group group=store.getGroup(instance.owningGroup);
	
	//TODO: serialize the instance configuration as JSON
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "ApplicationInstance", alloc);
	rapidjson::Value instanceData(rapidjson::kObjectType);
	instanceData.AddMember("id", rapidjson::StringRef(instance.id.c_str()), alloc);
	instanceData.AddMember("name", rapidjson::StringRef(instance.name.c_str()), alloc);
	std::string application=instance.application;
	if(application.find('/')!=std::string::npos && application.find('/')<application.size()-1)
			application=application.substr(application.find('/')+1);
	instanceData.AddMember("application", application, alloc);
	instanceData.AddMember("group", store.getGroup(instance.owningGroup).name, alloc);
	instanceData.AddMember("cluster", store.getCluster(instance.cluster).name, alloc);
	instanceData.AddMember("created", rapidjson::StringRef(instance.ctime.c_str()), alloc);
	instanceData.AddMember("configuration", rapidjson::StringRef(instance.config.c_str()),
			       alloc);
	result.AddMember("metadata", instanceData, alloc);

	
	auto configPath=store.configPathForCluster(instance.cluster);
	auto systemNamespace=store.getCluster(instance.cluster).systemNamespace;
	auto services=getServices(configPath,instance.name,group.namespaceName(),systemNamespace);
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
	
	if(req.url_params.get("detailed")){
		try{
			result.AddMember("details",fetchInstanceDetails(store,instance,systemNamespace,alloc),alloc);
		}catch(std::runtime_error& err){
			rapidjson::Value error(rapidjson::kObjectType);
			error.AddMember("kind", "Error", alloc);
			error.AddMember("message", std::string("Failed to detailed information for instance: ")+err.what(), alloc);
			result.AddMember("details",error,alloc);
		}
	}

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
	//only admins or member of the Group which owns an instance may delete it
	if(!user.admin && !store.userInGroup(user.id,instance.owningGroup))
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
			return (std::string("Failed to delete instance using helm: ")+e.what());
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

crow::response restartApplicationInstance(PersistentStore& store, const crow::request& req, const std::string& instanceID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to restart " << instanceID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	auto instance=store.getApplicationInstance(instanceID);
	if(!instance)
		return crow::response(404,generateError("Application instance not found"));
	//only admins or members of the Group which owns an instance may restart it
	if(!user.admin && !store.userInGroup(user.id,instance.owningGroup))
		return crow::response(403,generateError("Not authorized"));
		
	const Group group=store.getGroup(instance.owningGroup);
	if(!group)
		return crow::response(500,generateError("Invalid Group"));
	const Cluster cluster=store.getCluster(instance.cluster);
	if(!cluster)
		return crow::response(500,generateError("Invalid Cluster"));
	
	//TODO: it would be good to detect if there is nothing to stop and proceed 
	//      with restarting in that case
	log_info("Stopping old " << instance);
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
			return crow::response(500,generateError(message));
		}
	}
	catch(std::runtime_error& e){
		return crow::response(500,generateError(std::string("Failed to delete instance using helm: ")+e.what()));
	}
	log_info("Starting new " << instance);
	//write configuration to a file for helm's benefit
	FileHandle instanceConfig=makeTemporaryFile(instance.id);
	{
		std::ofstream outfile(instanceConfig.path());
		outfile << instance.config;
		if(!outfile){
			log_error("Failed to write instance configuration to " << instanceConfig.path());
			return crow::response(500,generateError("Failed to write instance configuration to disk"));
		}
	}
	std::string additionalValues;
	if(!store.getAppLoggingServerName().empty()){
		additionalValues+="SLATE.Logging.Enabled=true";
		additionalValues+=",SLATE.Logging.Server.Name="+store.getAppLoggingServerName();
		additionalValues+=",SLATE.Logging.Server.Port="+std::to_string(store.getAppLoggingServerPort());
	}
	else
		additionalValues+="SLATE.Logging.Enabled=false";
	additionalValues+=",SLATE.Cluster.Name="+cluster.name;

	auto clusterConfig=store.configPathForCluster(cluster.id);
	
	try{
		kubernetes::kubectl_create_namespace(*clusterConfig, group);
	}
	catch(std::runtime_error& err){
		store.removeApplicationInstance(instance.id);
		return crow::response(500,generateError(err.what()));
	}
	
	auto commandResult=runCommand("helm",
	  {"install",instance.application,"--name",instance.name,
	   "--namespace",group.namespaceName(),"--values",instanceConfig.path(),
	   "--set",additionalValues,
	   "--tiller-namespace",cluster.systemNamespace},
	  {{"KUBECONFIG",*clusterConfig}});
	if(commandResult.status || 
	   commandResult.output.find("STATUS: DEPLOYED")==std::string::npos){
		std::string errMsg="Failed to start application instance with helm:\n"+commandResult.error+"\n system namespace: "+cluster.systemNamespace;
		log_error(errMsg);
		//helm will (unhelpfully) keep broken 'releases' around, so clean up here
		runCommand("helm",
		  {"delete","--purge",instance.name,"--tiller-namespace",cluster.systemNamespace},
		  {{"KUBECONFIG",*clusterConfig}});
		//TODO: include any other error information?
		return crow::response(500,generateError(errMsg));
	}
	log_info("Restarted " << instance << " on " << cluster << " on behalf of " << user);
	return crow::response(200);
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
	
	//only admins or member of the Group which owns an instance may delete it
	if(!user.admin && !store.userInGroup(user.id,instance.owningGroup))
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
	bool previousLogs=req.url_params.get("previous");
	
	log_info("Sending logs from " << instance << " to " << user);
	auto configPath=store.configPathForCluster(instance.cluster);
	auto systemNamespace=store.getCluster(instance.cluster).systemNamespace;
	
	const Group group=store.getGroup(instance.owningGroup);
	const std::string nspace=group.namespaceName();
	//find out what pods make up this instance
	std::vector<std::string> pods;
	try{
		pods=internal::findInstancePods(instance, systemNamespace, *configPath);
	}catch(std::runtime_error& err){
		return crow::response(500,generateError(err.what()));
	}
	
	std::string logData;
	auto collectLog=[&](const std::string& pod, const std::string& container){
		logData+=std::string(40,'=')+"\nPod: "+pod+" Container: "+container+'\n';
		std::vector<std::string> args={"logs",pod,"-c",container,"-n",nspace};
		if(maxLines)
			args.push_back("--tail="+std::to_string(maxLines));
		if(previousLogs)
			args.push_back("-p");
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
			if(std::find(containers.begin(),containers.end(),container)!=containers.end())
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
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "ApplicationInstance", alloc);
	rapidjson::Value instanceData(rapidjson::kObjectType);
	instanceData.AddMember("id", instance.id, alloc);
	instanceData.AddMember("name", instance.name, alloc);
	instanceData.AddMember("application", instance.application, alloc);
	instanceData.AddMember("group", store.getGroup(instance.owningGroup).name, alloc);
	instanceData.AddMember("cluster", store.getCluster(instance.cluster).name, alloc);
	instanceData.AddMember("created", instance.ctime, alloc);
	instanceData.AddMember("configuration", instance.config, alloc);
	result.AddMember("metadata", instanceData, alloc);
	result.AddMember("logs", rapidjson::StringRef(logData.c_str()), alloc);
	
	return crow::response(to_string(result));
}
