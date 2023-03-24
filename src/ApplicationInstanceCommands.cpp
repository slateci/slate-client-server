#include "ApplicationInstanceCommands.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"

#include "KubeInterface.h"
#include "Logging.h"
#include "Telemetry.h"
#include "ServerUtilities.h"
#include "ApplicationCommands.h"

#include <chrono>

crow::response listApplicationInstances(PersistentStore& store, const crow::request& req){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);

	using namespace std::chrono;
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	const User user=authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to list application instances from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
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
	span->End();
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
	std::string netPathRef;
};

///query helm and kubernetes to find out what services a given instance contains 
///and how to contact them
std::multimap<std::string,ServiceInterface> getServices(const SharedFileHandle& configPath,
							const std::string &releaseName,
							const std::string &nspace,
							const std::string &systemNamespace) {
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setInternalSpanAttributes(attributes);
	auto options = getInternalSpanOptions();
	auto span = tracer->StartSpan("getServices", attributes, options);

	auto scope = tracer->WithActiveSpan(span);

	using namespace std::chrono;
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	span->AddEvent("kubectl get services");
	auto servicesResult=kubernetes::kubectl(*configPath,{"get","services","-l","release="+releaseName,"--namespace",nspace,"-o=json"});
	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	log_info("kubectl get services completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
	if(servicesResult.status){
		std::ostringstream err;
		err << "kubectl get services failed for instance " << releaseName << ": " << servicesResult.error;
		setSpanError(span, err.str());
		log_error(err.str());
		span->End();
		return {};
	}
	rapidjson::Document servicesData;
	try{
		servicesData.Parse(servicesResult.output.c_str());
	}catch(std::runtime_error& err){
		std::ostringstream  errMsg;
		errMsg << "Unable to parse kubectl get services JSON output for " << nspace << "::" << releaseName << ": " << err.what();
		log_error(errMsg.str());
		setSpanError(span, errMsg.str());
		span->End();
		return {};
	}

	//next try to find out the interface of each service
	std::multimap<std::string,ServiceInterface> services;
	for(const auto& serviceData : servicesData["items"].GetArray()){
		ServiceInterface interface;
	
		std::string serviceName=serviceData["metadata"]["name"].GetString();	
		interface.clusterIP=serviceData["spec"]["clusterIP"].GetString();
		
		std::string serviceType=serviceData["spec"]["type"].GetString();
		
		if(serviceType=="LoadBalancer"){
			if(serviceData["status"]["loadBalancer"].HasMember("ingress")
			   && serviceData["status"]["loadBalancer"]["ingress"].IsArray()
			   && serviceData["status"]["loadBalancer"]["ingress"].GetArray().Size()>0
			   && serviceData["status"]["loadBalancer"]["ingress"][0].IsObject()
			   && serviceData["status"]["loadBalancer"]["ingress"][0].HasMember("ip")){
				interface.externalIP=serviceData["status"]["loadBalancer"]["ingress"][0]["ip"].GetString();
			}
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
			if(podResult.status){
				std::ostringstream  errMsg;
				errMsg << "kubectl get pod -l " << filter << " --namespace " << nspace << " failed: " << podResult.error;
				log_error(errMsg.str());
				setSpanError(span, errMsg.str());
				span->End();
				continue;
			}
			rapidjson::Document podData;
			try{
				podData.Parse(podResult.output.c_str());
			}catch(std::runtime_error& err){
				std::ostringstream  errMsg;
				errMsg << "Unable to parse kubectl get service JSON output for kubectl get pod -l "
				       << filter << " --namespace " << nspace << ": " << err.what();
				log_error(errMsg.str());
				setSpanError(span, errMsg.str());
				span->End();
				continue;
			}
			if(podData["items"].GetArray().Size()==0){
				std::ostringstream  errMsg;
				errMsg << "Did not find any pods matching service selector for " << nspace << "::" << serviceName;
				log_error(errMsg.str());
				setSpanError(span, errMsg.str());
				span->End();
				continue;
			}
			if(podData["items"][0]["status"].HasMember("hostIP")){
				// now we should get the node info, to see if it has a ExternalIP which we ought to preferentially use
				if(podData["items"][0]["spec"].HasMember("nodeName")) {
					auto nodename=podData["items"][0]["spec"]["nodeName"].GetString();

					t1 = high_resolution_clock::now();
					auto nodeResult=kubernetes::kubectl(*configPath,{"get","node",nodename,"-o=json"});
					t2 = high_resolution_clock::now();
					log_info("kubectl get node completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
					if(nodeResult.status){
						std::ostringstream  errMsg;
						errMsg << "kubectl get node " << nodename << " failed: " << nodeResult.error;
						log_error(errMsg.str());
						setSpanError(span, errMsg.str());
						continue;
					}

					// preemptively set the externalIP to hostIP from the pod
					// data, but we'll try to get something more accurate
					interface.externalIP=podData["items"][0]["status"]["hostIP"].GetString();

					rapidjson::Document nodeData;
					try{
						nodeData.Parse(nodeResult.output.c_str());
					}catch(std::runtime_error& err){
						std::ostringstream  errMsg;
						errMsg << "Unable to parse kubectl node JSON output for kubectl get node " << nodename << ": " << err.what();
						log_error(errMsg.str());
						setSpanError(span, errMsg.str());
						continue;
					}

					if(nodeData.HasMember("status")){
						for (auto& addr : nodeData["status"]["addresses"].GetArray()) {
							std::string addrType(addr["type"].GetString());
							// assume that externalIP won't show up more than once
							if (addrType == "ExternalIP") {
								interface.externalIP=addr["address"].GetString();
							}
						}
					} 
				} 
			} else {
				interface.externalIP="<none>";
			}
		} else if(serviceType=="ClusterIP") {
			//Do nothing
		} else {
			log_error("Unexpected service type: " + serviceType);
			setSpanError(span, "Unexpected service type: " + serviceType);
		}
		
		//create a distinct interface entry for each exposed port
		for(const auto& port : serviceData["spec"]["ports"].GetArray()){
			int internalPort=-1, externalPort=-1;
			interface.ports="";
			interface.netPathRef="";
			
			if(port.HasMember("port") && port["port"].IsInt()){
				internalPort=port["port"].GetInt();
				interface.ports+=std::to_string(internalPort);
			}
			interface.ports+=":";
			if(port.HasMember("nodePort") && port["nodePort"].IsInt()){
				externalPort=port["nodePort"].GetInt();
				interface.ports+=std::to_string(externalPort);
			}
			interface.ports+="/";
			if(port.HasMember("protocol") && port["protocol"].IsString())
				interface.ports+=port["protocol"].GetString();
			
			if(serviceType=="LoadBalancer" && internalPort>0)
				interface.netPathRef=interface.externalIP+":"+std::to_string(internalPort);
			else if(serviceType=="NodePort" && externalPort>0)
				interface.netPathRef=interface.externalIP+":"+std::to_string(externalPort);
			
			services.emplace(std::make_pair(serviceName,interface));
		}
	}
	
	t1 = high_resolution_clock::now();
	auto ingressesResult=kubernetes::kubectl(*configPath,{"get","ingresses","-l","release="+releaseName,"--namespace",nspace,"-o=json"});
	t2 = high_resolution_clock::now();
	log_info("kubectl get ingresses completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
	if(ingressesResult.status){
		std::ostringstream  errMsg;
		errMsg << "kubectl get ingresses failed for instance " << releaseName << ": " << ingressesResult.error;
		log_error(errMsg.str());
		setSpanError(span, errMsg.str());
		span->End();
		return {};
	}
	rapidjson::Document ingressesData;
	try{
		ingressesData.Parse(ingressesResult.output.c_str());
	}catch(std::runtime_error& err){
		std::ostringstream  errMsg;
		errMsg << "Unable to parse kubectl get ingresses JSON output for " << nspace << "::" << releaseName << ": " << err.what();
		log_error(errMsg.str());
		setSpanError(span, errMsg.str());
		span->End();
		return {};
	}
	for(const auto& ingressData : ingressesData["items"].GetArray()){
		for(const auto& rule : ingressData["spec"]["rules"].GetArray()){
			if(!rule.HasMember("host"))
				continue;
			std::string hostName=rule["host"].GetString();
			for(const std::string protocol : {"http","https"}){
				if(!rule.HasMember(protocol) || !rule[protocol].HasMember("paths"))
					continue;
				for(const auto& path : rule[protocol]["paths"].GetArray()){
					if(!path.HasMember("backend"))
						continue;

					std::string serviceName = "";
					int servicePort = 0;	
					//kubernetes 1.20+
					if(path["backend"].HasMember("service") &&
						path["backend"]["service"].HasMember("name") &&
						path["backend"]["service"].HasMember("port") &&
						path["backend"]["service"]["port"].HasMember("number")) {
						
						serviceName=path["backend"]["service"]["name"].GetString();
						servicePort=path["backend"]["service"]["port"]["number"].GetInt();

					// kubernetes 1.19 or earlier
					} else if(path["backend"].HasMember("serviceName") &&
						path["backend"].HasMember("servicePort")) {
						serviceName=path["backend"]["serviceName"].GetString();
						servicePort=path["backend"]["servicePort"].GetInt();
					}	

					std::string servicePath=path["path"].GetString();
					//there may be several interfaces defined by this service
					auto matches=services.equal_range(serviceName);
					if(matches.first==services.end())
						continue;
					for(auto it=matches.first, end=matches.second; it!=end; it++){
						ServiceInterface& interface=it->second;
						//skip over interfaces whose port does not match
						auto idx=interface.ports.find(':');
						if(idx==0 || idx==std::string::npos)
							continue;
						if(std::to_string(servicePort)!=interface.ports.substr(0,idx))
							continue;
						interface.netPathRef=protocol+"://"+hostName+servicePath;
					}
				}
			}
		}
	}
	span->End();
	return services;
}

///\pre authorization must have already been checked
///\throws std::runtime_error
rapidjson::Value fetchInstanceDetails(PersistentStore &store,
				      const ApplicationInstance &instance,
				      const std::string &systemNamespace,
				      rapidjson::Document::AllocatorType &alloc) {
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setInternalSpanAttributes(attributes);
	auto options = getInternalSpanOptions();
	auto span = tracer->StartSpan("fetchInstanceDetails", attributes, options);
	auto scope = tracer->WithActiveSpan(span);

	rapidjson::Value instanceDetails(rapidjson::kObjectType);
	rapidjson::Value podDetails(rapidjson::kArrayType);
	
	const Group group=store.getGroup(instance.owningGroup);
	const std::string nspace=group.namespaceName();
	auto configPath=store.configPathForCluster(instance.cluster);
	
	using namespace std::chrono;
	high_resolution_clock::time_point t1,t2;
	
	//find out what pods make up this instance
	t1 = high_resolution_clock::now();
	span->AddEvent("kubectl get pods");
	auto result=kubernetes::kubectl(*configPath,{"get","pods","-l","release="+instance.name,"-n",nspace,"-o=json"});
	t2 = high_resolution_clock::now();
	log_info("kubectl get pods completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
	if(result.status){
		std::ostringstream err;
		err << "Failed to get pod information for " << instance;
		log_error(err.str());
		rapidjson::Value podInfo(rapidjson::kObjectType);
		podInfo.AddMember("kind", "Error", alloc);
		podInfo.AddMember("message", "Failed to get information for pods", alloc);
		podDetails.PushBack(podInfo,alloc);
		instanceDetails.AddMember("pods",podDetails,alloc);
		setSpanError(span, err.str());
		span->End();
		return instanceDetails;
	}	

	rapidjson::Document podData(&alloc);
	std::vector<std::future<std::pair<std::size_t,std::string>>> eventData;
	try{
		podData.Parse(result.output.c_str());
	}
	catch(std::runtime_error& err){
		std::ostringstream errMsg;
		errMsg << "Unable to parse kubectl output for " << instance << " pods";
		log_error(errMsg.str());
		setSpanError(span, errMsg.str());
		span->End();
		throw std::runtime_error("Could not find pods for instance");
	}
	std::size_t podIndex=0;
	for(auto& pod : podData["items"].GetArray()){
		std::string podName=pod["metadata"]["name"].GetString();
		rapidjson::Value podInfo(rapidjson::kObjectType);
		
		if(pod.HasMember("metadata")){
			if(pod["metadata"].HasMember("creationTimestamp"))
				podInfo.AddMember("created",pod["metadata"]["creationTimestamp"],alloc);
			if(pod["metadata"].HasMember("name"))
				podInfo.AddMember("name",pod["metadata"]["name"],alloc);
		}
		if(pod.HasMember("spec")){
			if(pod["spec"].HasMember("nodeName"))
				podInfo.AddMember("hostName",pod["spec"]["nodeName"],alloc);
		}
		//ownerReferences?
		if(pod.HasMember("status")){
			if(pod["status"].HasMember("hostIP"))
				podInfo.AddMember("hostIP",pod["status"]["hostIP"],alloc);
			if(pod["status"].HasMember("phase"))
				podInfo.AddMember("status",pod["status"]["phase"],alloc);
			if(pod["status"].HasMember("conditions"))
				podInfo.AddMember("conditions",pod["status"]["conditions"],alloc);
			if(pod["status"].HasMember("message"))
				podInfo.AddMember("message",pod["status"]["message"],alloc);
			if(pod["status"].HasMember("containerStatuses")){
				rapidjson::Value containers(rapidjson::kArrayType);
				for(auto& item : pod["status"]["containerStatuses"].GetArray()){
					rapidjson::Value container(rapidjson::kObjectType);
					//TODO: when dealing with an image from a non-default
					//registry, we should make sure to capture that somewhere
					if(item.HasMember("image"))
						container.AddMember("image",item["image"],alloc);
					if(item.HasMember("imageID")){
						std::string idStr=item["imageID"].GetString();
						//try to simplify and remove redundant information, 
						//cutting down 
						//docker-pullable://repo/name@sha256:0123456789...
						//to just the hash part, 0123456789...
						auto pos=idStr.rfind(':');
						if(pos!=std::string::npos && (pos+1)<idStr.size())
							idStr=idStr.substr(pos+1);
						container.AddMember("imageID",idStr,alloc);
					}
					if(item.HasMember("name"))
						container.AddMember("name",item["name"],alloc);
					if(item.HasMember("ready"))
						container.AddMember("ready",item["ready"],alloc);
					if(item.HasMember("restartCount"))
						container.AddMember("restartCount",item["restartCount"],alloc);
					if(item.HasMember("state"))
						container.AddMember("state",item["state"],alloc);
					if(item.HasMember("lastState"))
						container.AddMember("lastState",item["lastState"],alloc);
					containers.PushBack(container,alloc);
				}
				podInfo.AddMember("containers",containers,alloc);
			}
		}
		
		//Also try to fetch events associated with the pod
		auto getPodEvents=[&nspace,&configPath](std::size_t podIndex, const std::string podName)->std::pair<std::size_t,std::string>{
			high_resolution_clock::time_point t1 = high_resolution_clock::now();
			auto result=kubernetes::kubectl(*configPath,{"get","event","--field-selector","involvedObject.name="+podName,"-n",nspace,"-o=json"});
			high_resolution_clock::time_point t2 = high_resolution_clock::now();
			log_info("kubectl get event completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
			if(result.status)
				log_warn("kubectl get event failed for pod " << podName << " in namespace " << nspace);
			return std::make_pair(podIndex,std::move(result.output));
		};
		eventData.emplace_back(std::async(std::launch::async,getPodEvents,podIndex++,podName));
		
		podDetails.PushBack(podInfo,alloc);
	}
	for(auto& f : eventData){
		auto p=f.get();
		rapidjson::Document data(rapidjson::kObjectType,&alloc);
		try{
			data.Parse(p.second.c_str());
		}catch(std::runtime_error& err){
			log_warn("Unable to parse event data as JSON");
			continue;
		}
		if(data.HasMember("items") && data["items"].IsArray()){
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
			podDetails[p.first].AddMember("events",events,alloc);
		}
		
	}
	instanceDetails.AddMember("pods",podDetails,alloc);
	span->End();
	return instanceDetails;
}

crow::response fetchApplicationInstanceInfo(PersistentStore& store, const crow::request& req, const std::string& instanceID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user=authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested information about " << instanceID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	auto instance=store.getApplicationInstance(instanceID);
	if(!instance) {
		const std::string& errMsg = "Application instance not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}
	
	//only admins or member of the Group which owns an instance may query it
	if(!user.admin && !store.userInGroup(user.id,instance.owningGroup)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	//fetch the full configuration for the instance
	instance.config=store.getApplicationInstanceConfig(instanceID);
	
	//get information on the owning Group, needed to look up services, etc.
	const Group group=store.getGroup(instance.owningGroup);
	if(!group) {
		const std::string& errMsg = "Invalid Group";
		setWebSpanError(span, errMsg, 500);
		span->End();
		log_error(errMsg);
		return crow::response(500, generateError(errMsg));
	}

	//get cluster and kubeconfig path (for app/chart versions)
	const Cluster cluster=store.getCluster(instance.cluster);
	if(!cluster) {
		const std::string& errMsg = "Invalid Cluster";
		setWebSpanError(span, errMsg, 500);
		span->End();
		log_error(errMsg);
		return crow::response(500, generateError("Invalid Cluster"));
	}
	span->SetAttribute("cluster", cluster.name);
	auto clusterConfig=store.configPathForCluster(cluster.id);
	
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
	// get helm release info
	std::vector<std::string> listArgs={"list",
		"-f","^" + instance.name + "$",
		"-n",group.namespaceName(),
		"--output","json",
	};
	auto commandResult=runCommand("helm",listArgs,{{"KUBECONFIG",*clusterConfig}});
	rapidjson::Document releaseInfo;
	releaseInfo.Parse(commandResult.output.c_str());
	//There should be at most one matching result
	if(releaseInfo.IsArray() && releaseInfo.Size()!=0 && releaseInfo[0].HasMember("app_version"))
		instanceData.AddMember("appVersion", rapidjson::StringRef(releaseInfo[0]["app_version"].GetString()), alloc);
	else
		instanceData.AddMember("appVersion", "Unknown", alloc);
	instanceData.AddMember("group", store.getGroup(instance.owningGroup).name, alloc);
	instanceData.AddMember("cluster", store.getCluster(instance.cluster).name, alloc);
	instanceData.AddMember("created", rapidjson::StringRef(instance.ctime.c_str()), alloc);
	instanceData.AddMember("configuration", rapidjson::StringRef(instance.config.c_str()), alloc);
	if(releaseInfo.IsArray() && releaseInfo.Size()!=0 && releaseInfo[0].HasMember("chart"))
		instanceData.AddMember("chartVersion", rapidjson::StringRef(releaseInfo[0]["chart"].GetString()), alloc);
	else
		instanceData.AddMember("chartVersion", "Unknown", alloc);
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
		serviceEntry.AddMember("url", rapidjson::StringRef(service.second.netPathRef.c_str()), alloc);
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

	span->End();
	return crow::response(to_string(result));
}

crow::response deleteApplicationInstance(PersistentStore& store, const crow::request& req, const std::string& instanceID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user=authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " requested to delete " << instanceID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	auto instance=store.getApplicationInstance(instanceID);
	if(!instance) {
		const std::string& errMsg = "Application instance not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}
	//only admins or member of the Group which owns an instance may delete it
	if(!user.admin && !store.userInGroup(user.id,instance.owningGroup)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	bool force=(req.url_params.get("force")!=nullptr);
	
	auto err=internal::deleteApplicationInstance(store,instance,force);
	if(!err.empty()) {
		setWebSpanError(span, err, 500);
		span->End();
		log_error(err);
		return crow::response(500, generateError(err));
	}

	span->End();
	return crow::response(200);
}

namespace internal{
std::string deleteApplicationInstance(PersistentStore& store, const ApplicationInstance& instance, bool force){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setInternalSpanAttributes(attributes);
	auto options = getInternalSpanOptions();
	auto span = tracer->StartSpan("deleteApplicationInstance", attributes, options);
	auto scope = tracer->WithActiveSpan(span);

	log_info("Deleting " << instance);
	try{
		const Group group=store.getGroup(instance.owningGroup);
		auto configPath=store.configPathForCluster(instance.cluster);
		auto systemNamespace=store.getCluster(instance.cluster).systemNamespace;
		std::vector<std::string> deleteArgs={"delete",instance.name};
		unsigned int helmMajorVersion=kubernetes::getHelmMajorVersion();
		if(helmMajorVersion==2)
			deleteArgs.insert(deleteArgs.begin()+1,"--purge");
		else if(helmMajorVersion==3){
			deleteArgs.push_back("--namespace");
			deleteArgs.push_back(group.namespaceName());
		}
		auto helmResult = kubernetes::helm(*configPath,systemNamespace,deleteArgs);
		
		log_info("helm output: " << helmResult.output);
		if(helmResult.status || 
		   (helmResult.output.find("release \""+instance.name+"\" deleted")==std::string::npos &&
		    helmResult.output.find("release \""+instance.name+"\" uninstalled")==std::string::npos)){
			std::string message="helm delete failed: " + helmResult.error;
			log_error(message);
			setSpanError(span, message);
			if(!force) {
				span->End();
				return message;
			}
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
		std::ostringstream err;
		err << "Failed to delete " << instance << " from persistent store";
		log_error(err.str());
		setSpanError(span, err.str());
		span->End();
		return "Failed to delete instance from database";
	}
	span->End();
	return "";
}
}

crow::response updateApplicationInstance(PersistentStore& store, const crow::request& req, const std::string& instanceID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user=authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to update " << instanceID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}

	auto instance=store.getApplicationInstance(instanceID);
	if(!instance) {
		const std::string& errMsg = "Application instance not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}

	//only admins or members of the Group which owns an instance may restart it
	if(!user.admin && !store.userInGroup(user.id,instance.owningGroup)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}

	const Group group=store.getGroup(instance.owningGroup);
	if(!group) {
		const std::string& errMsg = "Invalid Group";
		setWebSpanError(span, errMsg, 500);
		span->End();
		log_error(errMsg);
		return crow::response(500, generateError(errMsg));
	}
	const Cluster cluster=store.getCluster(instance.cluster);
	if(!cluster) {
		const std::string& errMsg = "Invalid Cluster";
		setWebSpanError(span, errMsg, 500);
		span->End();
		log_error(errMsg);
		return crow::response(500, generateError(errMsg));
	}
	span->SetAttribute("cluster", cluster.name);

	rapidjson::Document body;
	try{
		body.Parse(req.body.c_str());
	}catch(std::runtime_error& err){
		const std::string& errMsg = "Invalid JSON in request body";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(body.IsNull()) {
		const std::string& errMsg = "Invalid JSON in request body";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}

	if (!body.HasMember("configuration")) {
	    const std::string& errMsg = "Configuration for update missing";
	    setWebSpanError(span, errMsg, 400);
	    span->End();
	    log_error(errMsg);
	    return crow::response(400, generateError(errMsg));
	}

	if(!body["configuration"].IsString()) {
		const std::string& errMsg = "Incorrect type for configuration";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}

	instance.config=body["configuration"].GetString();

	std::string chartVersion = "";
	if(body.HasMember("chartVersion") && body["chartVersion"].IsString())
		chartVersion = body["chartVersion"].GetString();

	auto helmSearchResult = runCommand("helm",{"inspect","values",instance.application, "--version", chartVersion});
	if(helmSearchResult.status){
		std::ostringstream errMsg;
		errMsg << "Command failed: helm search " << (instance.application) << ": [exit] " << helmSearchResult.status
		       << " [err] " << helmSearchResult.error << " [out] " << helmSearchResult.output;
		setWebSpanError(span, errMsg.str(), 400);
		span->End();
		log_error(errMsg.str());
		return crow::response(500, generateError("Unable to fetch application version"));
	}

	std::string resultMessage;
	
	auto clusterConfig=store.configPathForCluster(cluster.id);
	//TODO: it would be good to detect if there is nothing to stop and proceed 
	//      with restarting in that case
	log_info("Stopping old " << instance);
	try{
		auto systemNamespace=store.getCluster(instance.cluster).systemNamespace;
		std::vector<std::string> deleteArgs={"delete",instance.name};
		unsigned int helmMajorVersion=kubernetes::getHelmMajorVersion();
		std::string notFoundMsg;
		if(helmMajorVersion==2){
			deleteArgs.insert(deleteArgs.begin()+1,"--purge");
			notFoundMsg="\""+instance.name+"\" not found";
		}
		else if(helmMajorVersion==3){
			deleteArgs.push_back("--namespace");
			deleteArgs.push_back(group.namespaceName());
			notFoundMsg=instance.name+": release: not found";
		}
		auto helmResult=kubernetes::helm(*clusterConfig,systemNamespace,deleteArgs);
	
		if((helmResult.status || 
		    (helmResult.output.find("release \""+instance.name+"\" deleted")==std::string::npos && 
		     helmResult.output.find("release \""+instance.name+"\" uninstalled")==std::string::npos))
		   && helmResult.error.find(notFoundMsg)==std::string::npos){
			std::string message="helm delete failed: " + helmResult.error;
			setWebSpanError(span, message, 500);
			span->End();
			log_error(message);
			return crow::response(500,generateError(message));
		}
	}
	catch(std::runtime_error& e){
		std::string message = std::string("Failed to delete instance using helm: ") + e.what();
		setWebSpanError(span, message, 500);
		span->End();
		log_error(message);
		return crow::response(500,generateError(message));
	}
	
	log_info("Waiting to ensure that all previous objects from " << instance << " have been deleted");
	std::chrono::seconds maxTime(120), elapsedTime(0), maxPollDelay(10), pollDelay(1);
	while(elapsedTime<maxTime){
		pollDelay+=pollDelay;
		if(pollDelay>maxPollDelay)
			pollDelay=maxPollDelay;
		std::this_thread::sleep_for(pollDelay);
		try{
			auto objsResult=kubernetes::kubectl(*clusterConfig,{"get","all","-l release="+instance.name,"-n",group.namespaceName(),"-o=json"});
			if(objsResult.status){
				std::ostringstream errMsg;
				errMsg << "Failed to check for deleted instance objects: " << objsResult.error;
				log_error(errMsg.str());
				resultMessage+="Failed to check whether objects from old instance are fully deleted; reinstall may fail\n";
				setSpanError(span, errMsg.str());
				break;
			}
			rapidjson::Document objData;
			try{
				objData.Parse(objsResult.output.c_str());
			}
			catch(std::runtime_error& err){
				std::ostringstream errMsg;
				errMsg << "Unable to parse kubectl output for " << "get all -l release=" << instance.name << " -n"
				    << group.namespaceName() << " -o=json";
				setSpanError(span, errMsg.str());
				log_error(errMsg.str());
			}
			//check whether any objects remain
			if(objData.HasMember("items") && objData["items"].IsArray() && objData["items"].Empty())
				break;
		}
		catch(std::runtime_error& e){
			const std::string& errMsg = std::string("Failed to check for deleted instance objects: ") + e.what();
			log_error(errMsg);
			setSpanError(span, errMsg);
			resultMessage+="[Warning] Failed to check whether objects from old instance are fully deleted; reinstall may fail\n";
			break;
		}
		//We only count up the time we deliberately waited, which will miss any 
		//latency added by kubernetes. This could be accounted for if necessary.
		elapsedTime+=pollDelay;
	}
	if(elapsedTime>=maxTime){
		log_warn("Object deletion check timeout reached; proceeding with reinstall anyway");
		resultMessage+="[Warning] Object deletion check timeout reached; proceeding with reinstall anyway\n";
	}
	
	log_info("Starting new " << instance);
	//write configuration to a file for helm's benefit
	FileHandle instanceConfig=makeTemporaryFile(instance.id);
	{
		std::ofstream outfile(instanceConfig.path());
		outfile << instance.config;
		if(!outfile){
			std::ostringstream errMsg;
			errMsg << "Failed to write instance configuration to " << instanceConfig.path();
			log_error(errMsg.str());
			setWebSpanError(span, errMsg.str(), 500);
			span->End();
			return crow::response(500,generateError("Failed to write instance configuration to disk"));
		}
	}
	std::string additionalValues=internal::assembleExtraHelmValues(store,cluster,instance,group);
	
	try{
		kubernetes::kubectl_create_namespace(*clusterConfig, group);
	}
	catch(std::runtime_error& err){
		store.removeApplicationInstance(instance.id);
		log_error(err.what());
		setWebSpanError(span, err.what(), 500);
		span->End();
		return crow::response(500,generateError(err.what()));
	}

	std::vector<std::string> installArgs={"install",
	  instance.name,
	  instance.application,
	   "--namespace",group.namespaceName(),
	   "--values",instanceConfig.path(),
	   "--set",additionalValues,
	   "--version",chartVersion,
	   };
	unsigned int helmMajorVersion=kubernetes::getHelmMajorVersion();
	if(helmMajorVersion==2){
		installArgs.insert(installArgs.begin()+1,"--name");
		installArgs.push_back("--tiller-namespace");
		installArgs.push_back(cluster.systemNamespace);
	}

	commandResult commandResult;
	{
		setInternalSpanAttributes(attributes);
		options = getInternalSpanOptions();
		auto helmSpan = tracer->StartSpan("helm install", attributes, options);
		populateSpan(helmSpan, req);
		auto helmScope = tracer->WithActiveSpan(helmSpan);
		commandResult = runCommand("helm", installArgs, {{"KUBECONFIG", *clusterConfig}});
		helmSpan->End();
	}
	if(commandResult.status || 
	   (commandResult.output.find("STATUS: DEPLOYED")==std::string::npos &&
	    commandResult.output.find("STATUS: deployed")==std::string::npos)){
		std::string errMsg="Failed to start application instance with helm:\n"+commandResult.error+"\n system namespace: "+cluster.systemNamespace;
		log_error(errMsg);
		std::ifstream configFile(instanceConfig.path());

		//helm will (unhelpfully) keep broken 'releases' around, so clean up here
		std::vector<std::string> deleteArgs={"delete",instance.name,"--namespace",group.namespaceName()};
		if(kubernetes::getHelmMajorVersion()==2)
			deleteArgs.insert(deleteArgs.begin()+1,"--purge");
		auto helmResult=kubernetes::helm(*clusterConfig,cluster.systemNamespace,deleteArgs);
		//TODO: include any other error information?
		if(!resultMessage.empty())
			errMsg+="\n"+resultMessage;
		setWebSpanError(span, errMsg, 500);
		span->End();
		return crow::response(500,generateError(errMsg));
	}

	// Not sure if this is the best way to handle updating db records
	store.removeApplicationInstance(instanceID);
	store.addApplicationInstance(instance);

	log_info("Updated " << instance << " on " << cluster << " on behalf of " << user);
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "Configuration", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", instance.id, alloc);
	metadata.AddMember("name", instance.name, alloc);
	//TODO: not including this data is non-compliant with the spec, but it is never used
	/*if(lines.size()>1){
		auto cols = string_split_columns(lines[1], '\t');
		if(cols.size()>3){
			metadata.AddMember("revision", cols[1], alloc);
			metadata.AddMember("updated", cols[2], alloc);
		}
	}
	if(!metadata.HasMember("revision")){
		metadata.AddMember("revision", "?", alloc);
		metadata.AddMember("updated", "?", alloc);
	}*/
	metadata.AddMember("application", instance.application, alloc);
	metadata.AddMember("group", group.id, alloc);
	result.AddMember("metadata", metadata, alloc);
	//TODO: not including this data is non-compliant with the spec, but it is never used
	//result.AddMember("status", "DEPLOYED", alloc);
	result.AddMember("message", resultMessage, alloc);
	span->End();
	return crow::response(to_string(result));	
}

crow::response restartApplicationInstance(PersistentStore& store, const crow::request& req, const std::string& instanceID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user=authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to restart " << instanceID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	auto instance=store.getApplicationInstance(instanceID);
	if(!instance) {
		const std::string& errMsg = "Application instance not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}
	//only admins or members of the Group which owns an instance may restart it
	if(!user.admin && !store.userInGroup(user.id,instance.owningGroup)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
		
	const Group group=store.getGroup(instance.owningGroup);
	if(!group) {
		const std::string& errMsg = "Invalid Group";
		setWebSpanError(span, errMsg, 500);
		span->End();
		log_error(errMsg);
		return crow::response(500, generateError(errMsg));
	}
	const Cluster cluster=store.getCluster(instance.cluster);
	if(!cluster) {
		const std::string& errMsg = "Invalid Cluster";
		setWebSpanError(span, errMsg, 500);
		span->End();
		log_error(errMsg);
		return crow::response(500, generateError(errMsg));
	}
	span->SetAttribute("cluster", cluster.name);
	instance.config=store.getApplicationInstanceConfig(instance.id);

	std::string resultMessage;
	
	auto clusterConfig=store.configPathForCluster(cluster.id);
	//TODO: it would be good to detect if there is nothing to stop and proceed 
	//      with restarting in that case
	log_info("Stopping old " << instance);
	try{
		auto systemNamespace=store.getCluster(instance.cluster).systemNamespace;
		std::vector<std::string> deleteArgs={"delete",instance.name};
		unsigned int helmMajorVersion=kubernetes::getHelmMajorVersion();
		std::string notFoundMsg;
		if(helmMajorVersion==2){
			deleteArgs.insert(deleteArgs.begin()+1,"--purge");
			notFoundMsg="\""+instance.name+"\" not found";
		}
		else if(helmMajorVersion==3){
			deleteArgs.push_back("--namespace");
			deleteArgs.push_back(group.namespaceName());
			notFoundMsg=instance.name+": release: not found";
		}
		auto helmResult=kubernetes::helm(*clusterConfig,systemNamespace,deleteArgs);
	
		if((helmResult.status || 
		    (helmResult.output.find("release \""+instance.name+"\" deleted")==std::string::npos && 
		     helmResult.output.find("release \""+instance.name+"\" uninstalled")==std::string::npos))
		   && helmResult.error.find(notFoundMsg)==std::string::npos){
			const std::string& message = "helm delete failed: " + helmResult.error;
			log_error(message);
			setWebSpanError(span, message, 500);
			span->End();
			return crow::response(500,generateError(message));
		}
	}
	catch(std::runtime_error& e){
		const std::string& message = std::string("Failed to delete instance using helm: ")+e.what();
		log_error(message);
		setWebSpanError(span, message, 500);
		span->End();
		return crow::response(500,generateError(message));
	}
	
	log_info("Waiting to ensure that all previous objects from " << instance << " have been deleted");
	std::chrono::seconds maxTime(120), elapsedTime(0), maxPollDelay(10), pollDelay(1);
	while(elapsedTime<maxTime){
		pollDelay+=pollDelay;
		if(pollDelay>maxPollDelay)
			pollDelay=maxPollDelay;
		std::this_thread::sleep_for(pollDelay);
		try{
			auto objsResult=kubernetes::kubectl(*clusterConfig,{"get","all","-l release="+instance.name,"-n",group.namespaceName(),"-o=json"});
			if(objsResult.status){
				std::ostringstream  errMsg;
				errMsg << "Failed to check for deleted instance objects: " << objsResult.error;
				log_error(errMsg.str());
				setWebSpanError(span, errMsg.str(), 500);
				resultMessage+="Failed to check whether objects from old instance are fully deleted; reinstall may fail\n";
				break;
			}
			rapidjson::Document objData;
			try{
				objData.Parse(objsResult.output.c_str());
			}
			catch(std::runtime_error& err){
				std::ostringstream errMsg;
				errMsg << "Unable to parse kubectl output for " << "get all -l release=" << instance.name << " -n"
				       << group.namespaceName() << " -o=json";
				errMsg << std::endl << "Exception: " << err.what();
				log_error(errMsg.str());
				setWebSpanError(span, errMsg.str(), 500);
			}
			//check whether any objects remain
			if(objData.HasMember("items") && objData["items"].IsArray() && objData["items"].Empty())
				break;
		}
		catch(std::runtime_error& e){
			std::ostringstream errMsg;
			errMsg << "Failed to check for deleted instance objects: " << e.what();
			log_error(errMsg.str());
			setWebSpanError(span, errMsg.str(), 500);
			resultMessage+="[Warning] Failed to check whether objects from old instance are fully deleted; reinstall may fail\n";
			break;
		}
		//We only count up the time we deliberately waited, which will miss any 
		//latency added by kubernetes. This could be accounted for if necessary.
		elapsedTime+=pollDelay;
	}
	if(elapsedTime>=maxTime){
		log_warn("Object deletion check timeout reached; proceeding with reinstall anyway");
		resultMessage+="[Warning] Object deletion check timeout reached; proceeding with reinstall anyway\n";
	}
	
	log_info("Starting new " << instance);
	//write configuration to a file for helm's benefit
	FileHandle instanceConfig=makeTemporaryFile(instance.id);
	{
		std::ofstream outfile(instanceConfig.path());
		outfile << instance.config;
		if(!outfile){
			std::ostringstream errMsg;
			errMsg << "Failed to write instance configuration to " << instanceConfig.path();
			log_error(errMsg.str());
			setWebSpanError(span, errMsg.str(), 500);
			span->End();
			return crow::response(500,generateError("Failed to write instance configuration to disk"));
		}
	}
	std::string additionalValues=internal::assembleExtraHelmValues(store,cluster,instance,group);
	
	try{
		kubernetes::kubectl_create_namespace(*clusterConfig, group);
	}
	catch(std::runtime_error& err){
		store.removeApplicationInstance(instance.id);
		setWebSpanError(span, err.what(), 500);
		span->End();
		return crow::response(500,generateError(err.what()));
	}

	std::vector<std::string> installArgs={"install",
	  instance.name,
	  instance.application,
	   "--namespace",group.namespaceName(),
	   "--values",instanceConfig.path(),
	   "--set",additionalValues,
	   };
	unsigned int helmMajorVersion=kubernetes::getHelmMajorVersion();
	if(helmMajorVersion==2){
		installArgs.insert(installArgs.begin()+1,"--name");
		installArgs.push_back("--tiller-namespace");
		installArgs.push_back(cluster.systemNamespace);
	}
	   
	auto commandResult=runCommand("helm",installArgs,{{"KUBECONFIG",*clusterConfig}});
	if(commandResult.status || 
	   (commandResult.output.find("STATUS: DEPLOYED")==std::string::npos &&
	    commandResult.output.find("STATUS: deployed")==std::string::npos)){
		std::string errMsg="Failed to start application instance with helm:\n"+commandResult.error+"\n system namespace: "+cluster.systemNamespace;
		log_error(errMsg);
		//helm will (unhelpfully) keep broken 'releases' around, so clean up here
		std::vector<std::string> deleteArgs={"delete",instance.name,"--namespace",group.namespaceName()};
		if(kubernetes::getHelmMajorVersion()==2)
			deleteArgs.insert(deleteArgs.begin()+1,"--purge");
		auto helmResult=kubernetes::helm(*clusterConfig,cluster.systemNamespace,deleteArgs);
		//TODO: include any other error information?
		if(!resultMessage.empty())
			errMsg+="\n"+resultMessage;
		setWebSpanError(span, errMsg, 500);
		span->End();
		return crow::response(500,generateError(errMsg));
	}
	log_info("Restarted " << instance << " on " << cluster << " on behalf of " << user);
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "Configuration", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", instance.id, alloc);
	metadata.AddMember("name", instance.name, alloc);
	//TODO: not including this data is non-compliant with the spec, but it is never used
	/*if(lines.size()>1){
		auto cols = string_split_columns(lines[1], '\t');
		if(cols.size()>3){
			metadata.AddMember("revision", cols[1], alloc);
			metadata.AddMember("updated", cols[2], alloc);
		}
	}
	if(!metadata.HasMember("revision")){
		metadata.AddMember("revision", "?", alloc);
		metadata.AddMember("updated", "?", alloc);
	}*/
	metadata.AddMember("application", instance.application, alloc);
	metadata.AddMember("group", group.id, alloc);
	result.AddMember("metadata", metadata, alloc);
	//TODO: not including this data is non-compliant with the spec, but it is never used
	//result.AddMember("status", "DEPLOYED", alloc);
	result.AddMember("message", resultMessage, alloc);
	span->End();
	return crow::response(to_string(result));
}

crow::response getApplicationInstanceScale(PersistentStore& store, const crow::request& req, const std::string& instanceID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user=authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to check the scale of " << instanceID << " from " << req.remote_endpoint);
	if (!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}

	auto instance=store.getApplicationInstance(instanceID);
	if(!instance) {
		const std::string& errMsg = "Application instance not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}

	//only admins or member of the Group which owns an instance examine it
	if(!user.admin && !store.userInGroup(user.id,instance.owningGroup)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}

	const Group group=store.getGroup(instance.owningGroup);
	const std::string nspace=group.namespaceName();

	std::string depName;
	if(req.url_params.get("deployment"))
		depName=req.url_params.get("deployment");

	auto configPath=store.configPathForCluster(instance.cluster);

	const std::string name=instance.name;
	auto deploymentResult=kubernetes::kubectl(*configPath,{"get","deployment","-l","release="+name,"--namespace",nspace,"-o=json"});
	if (deploymentResult.status) {
		std::ostringstream errMsg;
		errMsg << "kubectl get deployment -l release=" << name << " --namespace "
		       << nspace << "failed :" << deploymentResult.error;
		log_error(errMsg.str());
		setWebSpanError(span, errMsg.str(), 500);
	}

	rapidjson::Document deploymentData;
	try{
		deploymentData.Parse(deploymentResult.output.c_str());
	}catch(std::runtime_error& err){
		std::ostringstream errMsg;
		errMsg << "Unable to parse kubectl get deployment JSON output for " << name << ": " << err.what();
		log_error(errMsg.str());
		setWebSpanError(span, errMsg.str(), 500);
	}
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "ApplicationInstanceScale", alloc);
	rapidjson::Value deploymentScales(rapidjson::kObjectType);
	bool deploymentFound=false; //whether we found an explicitly selected deployment
	for(const auto& deployment : deploymentData["items"].GetArray()){
		if(!deployment.IsObject()){
			log_warn("Deployment result is not an object? Skipping");
			continue;
		}
		if(!deployment.HasMember("metadata") || !deployment["metadata"].IsObject()
		  || !deployment["metadata"].HasMember("deploymentName") || !deployment["metadata"]["deploymentName"].IsString()
		  || !deployment.HasMember("spec") || !deployment["spec"].IsObject()
		  || !deployment["spec"].HasMember("replicas") || !deployment["spec"]["replicas"].IsUint64()){
			log_warn("Deployment result does not have expected structure. Skipping");
			continue;
		}
		std::string deploymentName=deployment["metadata"]["deploymentName"].GetString();
		uint64_t replicas=deployment["spec"]["replicas"].GetUint64();
		//if the user requested information on a specific deployment, ignore 
		//others and keep track of whether we found the requested one.
		if(!depName.empty()){
			if(deploymentName == depName)
				deploymentFound=true;
			else	
				continue;
		}
		deploymentScales.AddMember(rapidjson::Value(deploymentName, alloc), rapidjson::Value(replicas), alloc);
	}
	if(!depName.empty() && !deploymentFound) {
		const std::string& errMsg = "Deployment " + depName + " not found in " + instanceID;
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	result.AddMember("deployments", deploymentScales, alloc);
	span->End();
	return crow::response(to_string(result));
}

crow::response scaleApplicationInstance(PersistentStore& store, const crow::request& req, const std::string& instanceID){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " requested to rescale " << instanceID << " from " << req.remote_endpoint);
	if (!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}

	auto instance=store.getApplicationInstance(instanceID);
	if(!instance) {
		const std::string& errMsg = "Application instance not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}

	//only admins or member of the Group which owns an instance may scale it
	if(!user.admin && !store.userInGroup(user.id,instance.owningGroup)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}

	const Group group=store.getGroup(instance.owningGroup);
	const std::string nspace=group.namespaceName();

	uint64_t replicas;
	const char* reqReplicas=req.url_params.get("replicas");
	if(reqReplicas){
		try{
			replicas=std::stoull(reqReplicas);
		}
		catch(std::runtime_error& err){
			const std::string& errMsg = "Invalid number of replicas";
			setWebSpanError(span, errMsg, 400);
			span->End();
			log_error(errMsg);
			return crow::response(400, generateError(errMsg));
		}
	}
	else{
		const std::string& errMsg = "Missing number of replicas";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	std::string depName;
	if(req.url_params.get("deployment"))
		depName=req.url_params.get("deployment");

	auto configPath=store.configPathForCluster(instance.cluster);

	const std::string name=instance.name;
	//collect all of the current deployment info
	auto deploymentResult=kubernetes::kubectl(*configPath,{"get","deployment","-l","release="+name,"--namespace",nspace,"-o=json"});
	if (deploymentResult.status) {
		std::ostringstream errMsg;
		errMsg << "kubectl get deployment -l release=" << name << " --namespace "
		       << nspace << "failed :" << deploymentResult.error;
		log_error(errMsg.str());
		setWebSpanError(span, errMsg.str(), 500);
	}

	rapidjson::Document deploymentData;
	try{
		deploymentData.Parse(deploymentResult.output.c_str());
	}catch(std::runtime_error& err){
		std::ostringstream errMsg;
		errMsg << "Unable to parse kubectl get deployment JSON output for " << name << ": " << err.what();
		log_error(errMsg.str());
		setWebSpanError(span, errMsg.str(), 500);
	}
	
	if(depName.empty() && deploymentData["items"].GetArray().Size()!=1) {
		const std::string& errMsg =
				instanceID + " does not expose exactly one deployment, and no deployment was specified to be scaled.";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	
	//Iterate through deployments to either check that the user requested one 
	//exists, or to pick which to use (if there is only one). At the same time, 
	//we can start compiling our response data. 
	bool deploymentFound=false;
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "ApplicationInstanceScale", alloc);
	rapidjson::Value deploymentScales(rapidjson::kObjectType);
	for(const auto& deployment : deploymentData["items"].GetArray()){
		if(!deployment.IsObject()){
			log_warn("Deployment result is not an object? Skipping");
			continue;
		}
		if(!deployment.HasMember("metadata") || !deployment["metadata"].IsObject()
		  || !deployment["metadata"].HasMember("name") || !deployment["metadata"]["name"].IsString()
		  || !deployment.HasMember("spec") || !deployment["spec"].IsObject()
		  || !deployment["spec"].HasMember("replicas") || !deployment["spec"]["replicas"].IsUint64()){
			log_warn("Deployment result does not have expected structure. Skipping");
			continue;
		}
		std::string name=deployment["metadata"]["name"].GetString();
		uint64_t depReplicas=deployment["spec"]["replicas"].GetUint64();
		if(depName.empty() && deploymentData["items"].GetArray().Size()==1)
			depName=name;
		if(name!=depName) //if the deployment is not the selected one, add its current information
			deploymentScales.AddMember(rapidjson::Value(name,alloc),rapidjson::Value(depReplicas),alloc);
		else{ //for the selected deployment, use the new target value
			deploymentScales.AddMember(rapidjson::Value(name,alloc),rapidjson::Value(replicas),alloc);
			deploymentFound=true;
		}
	}
	if(!deploymentFound) {
		const std::string& errMsg = "Deployment " + depName + " not found in " + instanceID;
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}
	result.AddMember("deployments", deploymentScales, alloc);

	auto scaleResult=kubernetes::kubectl(*configPath,{"scale","deployment",depName,"--replicas",std::to_string(replicas),"--namespace",nspace,"-o=json"});
	if(scaleResult.status){
		log_error("kubectl scale deployment" << "--replicas " << std::to_string(replicas) << "-l release=" 
		  << name << " --namespace " << nspace << "failed :" << scaleResult.error);
		const std::string& errMsg = "Scaling deployment "+depName+" to "+std::to_string(replicas)+" replicas failed: "+scaleResult.error;
		setWebSpanError(span, errMsg, 500);
		span->End();
		log_error(errMsg);
		return crow::response(500, generateError(errMsg));
	}
	span->End();
	return crow::response(to_string(result));
}

crow::response getApplicationInstanceLogs(PersistentStore &store,
					  const crow::request &req,
					  const std::string &instanceID) {
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user = authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " requested logs from " << instanceID << " from " << req.remote_endpoint);
	if(!user) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));
	}
	
	auto instance=store.getApplicationInstance(instanceID);
	if(!instance) {
		const std::string& errMsg = "Application instance not found";
		setWebSpanError(span, errMsg, 404);
		span->End();
		log_error(errMsg);
		return crow::response(404, generateError(errMsg));
	}
	
	//only admins or member of the Group which owns an instance may delete it
	if(!user.admin && !store.userInGroup(user.id,instance.owningGroup)) {
		const std::string& errMsg = "User not authorized";
		setWebSpanError(span, errMsg, 403);
		span->End();
		log_error(errMsg);
		return crow::response(403, generateError(errMsg));

	}

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
	std::string selectedContainer;
	{
		const char* reqContainer=req.url_params.get("container");
		if(reqContainer)
			selectedContainer=reqContainer;
	}
	bool previousLogs=req.url_params.get("previous");
	
	log_info("Sending logs from " << instance << " to " << user);
	auto configPath=store.configPathForCluster(instance.cluster);
	auto systemNamespace=store.getCluster(instance.cluster).systemNamespace;
	
	const Group group=store.getGroup(instance.owningGroup);
	const std::string nspace=group.namespaceName();
	
	//Make a list of all containers in all pods, including any filtering requested by the user
	std::vector<std::pair<std::string,std::string>> allContainers;
	auto podsResult=kubernetes::kubectl(*configPath,{"get","pods","-l release="+instance.name,"-n",nspace,"-o=json"});
	if(podsResult.status){
		std::ostringstream errMsg;
		errMsg << "Failed to look up pods for " << instance << ": " << podsResult.error;
		setWebSpanError(span, errMsg.str(), 500);
		span->End();
		log_error(errMsg.str());
		return crow::response(500, generateError("Failed to look up pods"));
	}
	rapidjson::Document podData;
	try{
		podData.Parse(podsResult.output.c_str());
	}
	catch(std::runtime_error& err){
		std::ostringstream errMsg;
		errMsg << "Unable to parse kubectl output for " << instance << " pods";
		setWebSpanError(span, errMsg.str(), 500);
		span->End();
		log_error(errMsg.str());
		throw std::runtime_error("Could not find pods for instance");
	}
	for(const auto& pod : podData["items"].GetArray()){
		if(!pod["spec"].HasMember("containers"))
			continue;
		std::string podName=pod["metadata"]["name"].GetString();
		for(const auto& container : pod["spec"]["containers"].GetArray()){
			std::string containerName=container["name"].GetString();
			if(selectedContainer.empty() || containerName==selectedContainer)
				allContainers.push_back(std::make_pair(podName,containerName));
		}
	}
	
	//check whether the user specified a container name, but we didn't find any matches
	if(allContainers.empty() && !selectedContainer.empty()) {
		const std::string& errMsg = "No containers found matching the name '" + selectedContainer + "'";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	
	std::string logData;
	auto collectLog=[&](const std::string& pod, const std::string& container)->std::string{
		using namespace std::chrono;
		high_resolution_clock::time_point t1,t2;
		t1 = high_resolution_clock::now();
		std::string logData=std::string(40,'=')+"\nPod: "+pod+" Container: "+container+'\n';
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
		t2 = high_resolution_clock::now();
		log_info("Log fetch completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
		return logData;
	};

	std::vector<std::future<std::string>> logBlocks;
	for(const auto& container : allContainers)
		logBlocks.emplace_back(std::async(std::launch::async,collectLog,container.first,container.second));
	for(auto& result : logBlocks)
		logData+=result.get();
	
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

	span->End();
	return crow::response(to_string(result));
}
