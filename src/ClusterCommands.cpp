#include "ClusterCommands.h"

#include <algorithm>
#include <iterator>
#include <set>

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
#include "ApplicationInstanceCommands.h"
#include "SecretCommands.h"

crow::response listClusters(PersistentStore& store, const crow::request& req){
	using namespace std::chrono;
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	std::vector<Cluster> clusters;
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list clusters from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list clusters

	if (auto group = req.url_params.get("group"))
		clusters=store.listClustersByGroup(group);
	else
		clusters=store.listClusters();

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(clusters.size(), alloc);
	for(const Cluster& cluster : clusters){
		rapidjson::Value clusterResult(rapidjson::kObjectType);
		clusterResult.AddMember("apiVersion", "v1alpha3", alloc);
		clusterResult.AddMember("kind", "Cluster", alloc);
		rapidjson::Value clusterData(rapidjson::kObjectType);
		clusterData.AddMember("id", cluster.id, alloc);
		clusterData.AddMember("name", cluster.name, alloc);
		clusterData.AddMember("owningGroup", store.findGroupByID(cluster.owningGroup).name, alloc);
		clusterData.AddMember("owningOrganization", cluster.owningOrganization, alloc);
		std::vector<GeoLocation> locations=store.getLocationsForCluster(cluster.id);
		rapidjson::Value clusterLocation(rapidjson::kArrayType);
		clusterLocation.Reserve(locations.size(), alloc);
		for(const auto& location : locations){
			rapidjson::Value entry(rapidjson::kObjectType);
			entry.AddMember("lat",location.lat, alloc);
			entry.AddMember("lon",location.lon, alloc);
			if(!location.description.empty())
				entry.AddMember("desc",location.description, alloc);
			clusterLocation.PushBack(entry, alloc);
		}
		clusterData.AddMember("location", clusterLocation, alloc);
		clusterData.AddMember("hasMonitoring", (bool)cluster.monitoringCredential, alloc);
		clusterResult.AddMember("metadata", clusterData, alloc);
		resultItems.PushBack(clusterResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);

	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	log_info("cluster listing completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
	return crow::response(to_string(result));
}

namespace internal{

///Locate a cluster's ingress controller and set a DNS record to point to it. 
///\return any informative message for the user
std::string setClusterDNSRecord(PersistentStore& store, const Cluster& cluster){
	std::string resultMessage;
	auto configPath=store.configPathForCluster(cluster.id);
	auto icAddress=kubernetes::kubectl(*configPath,{"get","services","-n",cluster.systemNamespace,
	                                   "-l","app.kubernetes.io/name=ingress-nginx",
	                                   "-o","jsonpath={.items[*].status.loadBalancer.ingress[0].ip}"});
	if(icAddress.status){
		log_error("Failed to check ingress controller service status: " << icAddress.error);
		resultMessage+="[Warning] Failed to check ingress controller service status: "+icAddress.error+"\n";
	}
	else if(icAddress.output.empty()){
		log_error("Ingress controller service has not received an IP address.");
		resultMessage+="[Warning] There is either no ingress controller service in the "+cluster.systemNamespace+" namespace, \n" \
		"or it has not received an address.\n" \
		" A DNS record cannot be generated for this cluster until this is resolved.\n";
	}
	else{
		log_info(cluster << " ingress controller has address " << icAddress.output);
		//make a DNS record
		auto name=store.dnsNameForCluster(cluster);
		auto wildcard="*."+name;
		log_info("Cluster wildcard DNS: " << wildcard);
		if(store.canUpdateDNS()){
			bool success=false;
			try{
				success=store.setDNSRecord(wildcard,icAddress.output);
			}
			catch(std::runtime_error& err){
				log_error("Unable to set DNS record for " << cluster << ": " << err.what());
			}
			if(!success)
				log_error("Failed to create DNS record mapping " << wildcard << " to " << icAddress.output);
			else
				resultMessage+="Services using Ingress on this cluster can be assigned subdomains within the "+name+" domain.\n";
		}
		else{
			log_warn("Not able to make DNS records, no wildcard record will be available for " << cluster);
			resultMessage+="[Warning] The SLATE API server is not able to make DNS records, so no DNS name will be available for this cluster.\n";
		}
	}
	return resultMessage;
}

///\return An informational message for the user
///\throw std::runtime_error
std::string ensureClusterSetup(PersistentStore& store, const Cluster& cluster){
	auto configPath=store.configPathForCluster(cluster.id);
	log_info("Attempting to access " << cluster);
	auto clusterInfo=kubernetes::kubectl(*configPath,{"get","serviceaccounts","-o=jsonpath={.items[*].metadata.name}"});
	if(clusterInfo.status || 
	   clusterInfo.output.find("default")==std::string::npos){
		log_info("Failure contacting " << cluster << "; deleting its record");
		log_error("Error was: " << clusterInfo.error);
		//things aren't working, delete our apparently non-functional record
		store.removeCluster(cluster.id);
		throw std::runtime_error("Cluster registration failed: "
		                         "Unable to contact cluster with kubectl");
	}
	else
		log_info("Success contacting " << cluster);
	{
		//check that there is a service account matching our namespace
		auto serviceAccounts=string_split_columns(clusterInfo.output,' ',false);
		if(serviceAccounts.empty()){
			log_error("Found no ServiceAccounts: " << clusterInfo.error);
			//things aren't working, delete our apparently non-functional record
			store.removeCluster(cluster.id);
			throw std::runtime_error("Cluster registration failed: "
			                         "Found no SeviceAccounts in the default namespace");
		}
		if(std::find(serviceAccounts.begin(),serviceAccounts.end(),cluster.systemNamespace)==serviceAccounts.end())
			throw std::runtime_error("Cluster registration failed: "
			                         "Unable to find matching service account in default namespace");
		//now double-check that the namespace name really does match the serviceaccount name
		auto namespaceCheck=kubernetes::kubectl(*configPath,{"describe","serviceaccount",cluster.systemNamespace});
		if(namespaceCheck.status){
			log_error("Failure confirming namespace name: " << namespaceCheck.error);
			store.removeCluster(cluster.id);
			throw std::runtime_error("Cluster registration failed: "
			                         "Checking default namespace name failed");
		}
		bool okay=false;
		std::string badline;
		for(const auto& line : string_split_lines(namespaceCheck.output)){
			auto items=string_split_columns(line,' ',false);
			if(items.size()!=2)
				continue;
			if(items[0]=="Namespace:"){
				if(items[1]==cluster.systemNamespace)
					okay=true;
				else{
					log_error("Default namespace does not appear to match SeviceAccount: " << line);
					badline=line;
				}
			}
		}
		if(!okay){
			std::string error="Default namespace does not appear to match default SeviceAccount: "
			  +badline+", SeviceAccount: "+cluster.systemNamespace;
			log_error(error);
			store.removeCluster(cluster.id);
			throw std::runtime_error("Cluster registration failed: "+error);
		}
	}
	//At this point we should have everything in order for the namespace and ServiceAccount;
	//update our database record to reflect this.
	store.updateCluster(cluster);
	
	//Extra information to be passed back to the user.
	std::string resultMessage;
	
	//As long as we are stuck with helm 2, we need tiller running on the cluster
	unsigned int helmMajorVersion=kubernetes::getHelmMajorVersion();
	//Make sure that is is.
	if(helmMajorVersion==2){
		auto commandResult = runCommand("helm",
		  {"init","--service-account",cluster.systemNamespace,"--tiller-namespace",cluster.systemNamespace},
		  {{"KUBECONFIG",*configPath}});
		auto expected="Tiller (the Helm server-side component) has been installed";
		auto already="Tiller is already installed";
		if(commandResult.status || 
		   (commandResult.output.find(expected)==std::string::npos &&
			commandResult.output.find(already)==std::string::npos)){
			log_info("Problem initializing helm on " << cluster << "; deleting its record");
			//things aren't working, delete our apparently non-functional record
			store.removeCluster(cluster.id);
			throw std::runtime_error("Cluster registration failed: "
									 "Unable to initialize helm");
		}
		if(commandResult.output.find("Warning: Tiller is already installed in the cluster")!=std::string::npos){
			bool okay=false;
			//check whether tiller is already in this namespace, or in some other and helm is just screwing things up.
			auto commandResult = kubernetes::kubectl(*configPath,{"get","deployments","--namespace",cluster.systemNamespace,"-o=jsonpath={.items[*].metadata.name}"});
		
			if(commandResult.status==0){
				for(const auto& deployment : string_split_columns(commandResult.output, ' ', false)){
					if(deployment=="tiller-deploy")
						okay=true;
				}
			}
		
			if(!okay){
				log_info("Cannot install tiller correctly because it is already installed (probably in the kube-system namespace)");
				//things aren't working, delete our apparently non-functional record
				store.removeCluster(cluster.id);
				throw std::runtime_error("Cluster registration failed: "
										 "Unable to initialize helm");
			}
		}
		log_info("Checking for running tiller. . . ");
		int delaySoFar=0;
		const int maxDelay=120000, delay=500;
		bool tillerRunning=false;
		while(!tillerRunning){
			auto commandResult = kubernetes::kubectl(*configPath,{"get","pods","--namespace",cluster.systemNamespace});
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
						log_info("Tiller ready");
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
					log_error("Waiting for tiller readiness on " << cluster << "(" << cluster.systemNamespace << ") timed out");
					resultMessage+="[Warning] Waiting for tiller readiness in the "+cluster.systemNamespace+" namespace timed out.\n";
					resultMessage+=" Applications cannot be installed on this cluster until this pod is running.\n";
					break;
				}
			}
		}
	} //end of tiller handling
	
	//set the convenience DNS record for the cluster
	resultMessage+=internal::setClusterDNSRecord(store,cluster);
	
	return resultMessage;
}

void supplmentLocation(PersistentStore& store, GeoLocation& loc){
	if(store.getGeocoder().canGeocode()){
		auto geoData=store.getGeocoder().reverseLookup(loc);
		if(!geoData)
			log_warn("Failed to look up geographic coordinates " << loc << ": " << geoData.error);
		else{
			if(!geoData.city.empty())
				loc.description=geoData.city;
			if(!geoData.countryName.empty()){
				if(!loc.description.empty())
					loc.description+=", ";
				loc.description+=geoData.countryName;
			}
			log_info("Updated location: " << loc);
			auto locStr=boost::lexical_cast<std::string>(loc);
			auto loc2=boost::lexical_cast<GeoLocation>(locStr);
			log_info("after round-trip: " << loc2);
		}
	}
}

}

crow::response createCluster(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to create a cluster from " << req.remote_endpoint);
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
	if(!body["metadata"].HasMember("group"))
		return crow::response(400,generateError("Missing Group ID in request"));
	if(!body["metadata"]["group"].IsString())
		return crow::response(400,generateError("Incorrect type for Group ID"));
	if(!body["metadata"].HasMember("owningOrganization"))
		return crow::response(400,generateError("Missing organization name in request"));
	if(!body["metadata"]["owningOrganization"].IsString())
		return crow::response(400,generateError("Incorrect type for organization"));
	if(!body["metadata"].HasMember("kubeconfig"))
		return crow::response(400,generateError("Missing kubeconfig in request"));
	if(!body["metadata"]["kubeconfig"].IsString())
		return crow::response(400,generateError("Incorrect type for kubeconfig"));

	std::string sentConfig = body["metadata"]["kubeconfig"].GetString();

	// reverse any escaping done in the config file to ensure valid yaml
	auto config = unescape(sentConfig);
	
	std::string systemNamespace;
	std::vector<YAML::Node> parsedConfig;
	try{
		parsedConfig=YAML::LoadAll(config);
	}catch(const YAML::ParserException& ex){
		return crow::response(400,generateError("Unable to parse kubeconfig as YAML"));
	}
	for(const auto& document : parsedConfig){
		if(document.IsMap() && document["contexts"] && document["contexts"].IsSequence()
		   && document["contexts"].size() && document["contexts"][0].IsMap()
		   && document["contexts"][0]["context"] && document["contexts"][0]["context"].IsMap()
		   && document["contexts"][0]["context"]["namespace"]
		   && document["contexts"][0]["context"]["namespace"].IsScalar()){
			systemNamespace=document["contexts"][0]["context"]["namespace"].as<std::string>();
			break;
		}
	}
	if(systemNamespace.empty())
		return crow::response(400,generateError("Unable to determine kubernetes namespace from kubeconfig"));
	
	Cluster cluster;
	cluster.id=idGenerator.generateClusterID();
	cluster.name=body["metadata"]["name"].GetString();
	cluster.config=config;
	cluster.owningGroup=body["metadata"]["group"].GetString();
	cluster.owningOrganization=body["metadata"]["owningOrganization"].GetString();
	//TODO: parse IP address out of config and attempt to get a location from it by GeoIP look up
	cluster.systemNamespace=systemNamespace;
	cluster.valid=true;
	
	//normalize owning group
	if(cluster.owningGroup.find(IDGenerator::groupIDPrefix)!=0){
		//if a name, find the corresponding group
		Group group=store.findGroupByName(cluster.owningGroup);
		//if no such Group exists, no one can install on its behalf
		if(!group)
			return crow::response(403,generateError("Not authorized"));
		//otherwise, get the actual Group ID and continue with the lookup
		cluster.owningGroup=group.id;
	}
	
	//users cannot register clusters to groups to which they do not belong
	if(!store.userInGroup(user.id,cluster.owningGroup))
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
	
	std::string resultMessage;
	try{
		resultMessage=internal::ensureClusterSetup(store,cluster);
	}
	catch(std::runtime_error& err){
		return crow::response(500,generateError(err.what()));
	}
	
	log_info("Created " << cluster << " owned by " << cluster.owningGroup 
	         << " on behalf of " << user);
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "Cluster", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", rapidjson::StringRef(cluster.id.c_str()), alloc);
	metadata.AddMember("name", rapidjson::StringRef(cluster.name.c_str()), alloc);
	result.AddMember("metadata", metadata, alloc); 
	result.AddMember("message", resultMessage, alloc); 

	return crow::response(to_string(result));
}

namespace internal{
	struct StorageClass{
		std::string name;
		bool isDefault;
		bool allowVolumeExpansion;
		std::string bindingMode;
		std::string reclaimPolicy;
		
		StorageClass():isDefault(false),allowVolumeExpansion(false){}
	};

	std::vector<StorageClass> getClusterStorageClasses(PersistentStore& store, const Cluster& cluster){
		std::vector<StorageClass> storageClasses;
		
		auto configPath=store.configPathForCluster(cluster.id);

		auto classInfoRaw=kubernetes::kubectl(*configPath,{"get","storageclasses","-o=json"});
		if(classInfoRaw.status!=0){
			log_error("Error from kubectl get storageclasses -o=json: " << classInfoRaw.error);
			return storageClasses;
		}
		
		rapidjson::Document classInfo;
		try{
			classInfo.Parse(classInfoRaw.output);
		}catch(std::runtime_error& err){
			log_error("Failed to parse output of kubectl get storageclasses -o=json as JSON");
			return storageClasses;
		}
		
		if(classInfo.HasMember("items") && classInfo["items"].IsArray()){
			for(const auto& item : classInfo["items"].GetArray()){
				StorageClass sc;
				if(item.HasMember("metadata") && item["metadata"].IsObject()){
					if(item["metadata"].HasMember("name") && item["metadata"]["name"].IsString())
						sc.name=item["metadata"]["name"].GetString();
					else
						continue; //not having a name is weird; skip
					if(item["metadata"].HasMember("annotations") && item["metadata"]["annotations"].IsObject()
					  && item["metadata"]["annotations"].HasMember("storageclass.kubernetes.io/is-default-class")){
						if(item["metadata"]["annotations"]["storageclass.kubernetes.io/is-default-class"].IsBool())
							sc.isDefault=item["metadata"]["annotations"]["storageclass.kubernetes.io/is-default-class"].GetBool();
						else if(item["metadata"]["annotations"]["storageclass.kubernetes.io/is-default-class"].IsString())
							sc.isDefault=item["metadata"]["annotations"]["storageclass.kubernetes.io/is-default-class"].GetString()==std::string("true");
					}
				}
				else
					continue; //if it has no metadata, something is very wrong with it
				if(item.HasMember("allowVolumeExpansion") && item["allowVolumeExpansion"].IsBool())
					sc.allowVolumeExpansion=item["allowVolumeExpansion"].GetBool();
				if(item.HasMember("volumeBindingMode") && item["volumeBindingMode"].IsString())
					sc.bindingMode=item["volumeBindingMode"].GetString();
				if(item.HasMember("reclaimPolicy") && item["reclaimPolicy"].IsString())
					sc.reclaimPolicy=item["reclaimPolicy"].GetString();
				storageClasses.push_back(sc);
			}
		}
		
		return storageClasses;
	}
	
	struct PriorityClass{
		std::string name;
		std::string description;
		bool isDefault;
		uint32_t priority;
		
		PriorityClass():isDefault(false),priority(0){}
	};
	
	std::vector<PriorityClass> getClusterPriorityClasses(PersistentStore& store, const Cluster& cluster){
		std::vector<PriorityClass> priorityClasses;
		
		auto configPath=store.configPathForCluster(cluster.id);

		auto classInfoRaw=kubernetes::kubectl(*configPath,{"get","priorityclasses","-o=json"});
		if(classInfoRaw.status!=0){
			log_error("Error from kubectl get priorityclasses -o=json: " << classInfoRaw.error);
			return priorityClasses;
		}
		
		rapidjson::Document classInfo;
		try{
			classInfo.Parse(classInfoRaw.output);
		}catch(std::runtime_error& err){
			log_error("Failed to parse output of kubectl get priorityclasses -o=json as JSON");
			return priorityClasses;
		}
		
		if(classInfo.HasMember("items") && classInfo["items"].IsArray()){
			for(const auto& item : classInfo["items"].GetArray()){
				PriorityClass pc;
				if(item.HasMember("metadata") && item["metadata"].IsObject()){
					if(item["metadata"].HasMember("name") && item["metadata"]["name"].IsString())
						pc.name=item["metadata"]["name"].GetString();
					else
						continue; //not having a name is weird; skip
				}
				else
					continue; //if it has no metadata, something is very wrong with it
				if(item.HasMember("description") && item["description"].IsString())
					pc.description=item["description"].GetString();
				if(item.HasMember("value") && item["value"].IsInt())
					pc.priority=item["value"].GetInt();
				if(item.HasMember("globalDefault") && item["globalDefault"].IsBool())
					pc.isDefault=item["globalDefault"].GetBool();
				priorityClasses.push_back(pc);
			}
		}
		
		return priorityClasses;
	}
}

crow::response getClusterInfo(PersistentStore& store, const crow::request& req,
                              const std::string clusterID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested information about " << clusterID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//all users are allowed to query all clusters?
	
	bool all_nodes = (req.url_params.get("nodes")!=nullptr);

	const Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	rapidjson::Value clusterResult(rapidjson::kObjectType);
	clusterResult.AddMember("apiVersion", "v1alpha3", alloc);
	clusterResult.AddMember("kind", "Cluster", alloc);
	rapidjson::Value clusterData(rapidjson::kObjectType);
	clusterData.AddMember("id", cluster.id, alloc);
	clusterData.AddMember("name", cluster.name, alloc);
	clusterData.AddMember("owningGroup", store.findGroupByID(cluster.owningGroup).name, alloc);
	clusterData.AddMember("owningOrganization", cluster.owningOrganization, alloc);
	// Attempt to find master node address (API server address-- typically the same)
	auto configPath=store.configPathForCluster(cluster.id);
	std::vector<std::string> serverArgs = {"config","view","-o=jsonpath={.clusters[0].cluster.server}"};
	auto server_info = kubernetes::kubectl(*configPath, serverArgs);
	clusterData.AddMember("masterAddress", rapidjson::StringRef(server_info.output), alloc);

	std::vector<GeoLocation> locations=store.getLocationsForCluster(cluster.id);
	rapidjson::Value clusterLocation(rapidjson::kArrayType);
	clusterLocation.Reserve(locations.size(), alloc);
	for(const auto& location : locations){
		rapidjson::Value entry(rapidjson::kObjectType);
		entry.AddMember("lat",location.lat, alloc);
		entry.AddMember("lon",location.lon, alloc);
		if(!location.description.empty())
			entry.AddMember("desc",location.description, alloc);
		clusterLocation.PushBack(entry, alloc);
	}
	clusterData.AddMember("location", clusterLocation, alloc);
	log_info(cluster << " monitoring credential is " << cluster.monitoringCredential);
	clusterData.AddMember("hasMonitoring", (bool)cluster.monitoringCredential, alloc);
	
	auto storageClasses=internal::getClusterStorageClasses(store,cluster);
	rapidjson::Value storageClassData(rapidjson::kArrayType);
	storageClassData.Reserve(storageClasses.size(), alloc);
	for(const auto& storageClass : storageClasses){
		rapidjson::Value entry(rapidjson::kObjectType);
		entry.AddMember("name",storageClass.name, alloc);
		entry.AddMember("isDefault",storageClass.isDefault, alloc);
		entry.AddMember("allowVolumeExpansion",storageClass.allowVolumeExpansion, alloc);
		entry.AddMember("bindingMode",storageClass.bindingMode, alloc);
		entry.AddMember("reclaimPolicy",storageClass.reclaimPolicy, alloc);
		storageClassData.PushBack(entry, alloc);
	}
	clusterData.AddMember("storageClasses", storageClassData, alloc);
	
	auto priorityClasses=internal::getClusterPriorityClasses(store,cluster);
	rapidjson::Value priorityClassData(rapidjson::kArrayType);
	priorityClassData.Reserve(priorityClasses.size(), alloc);
	for(const auto& priorityClass : priorityClasses){
		rapidjson::Value entry(rapidjson::kObjectType);
		entry.AddMember("name",priorityClass.name, alloc);
		entry.AddMember("isDefault",priorityClass.isDefault, alloc);
		entry.AddMember("description",priorityClass.description, alloc);
		entry.AddMember("priority",priorityClass.priority, alloc);
		priorityClassData.PushBack(entry, alloc);
	}
	clusterData.AddMember("priorityClasses", priorityClassData, alloc);
	
	// Collect all node info if requested
	if (all_nodes) {
		rapidjson::Value nodeInfo(rapidjson::kArrayType);
		auto node_info = kubernetes::kubectl(*configPath, {"get", "nodes", "-o", "json"});
		rapidjson::Document cmdOutput;
		cmdOutput.Parse(node_info.output);
		if(cmdOutput.HasMember("items")) {
			for(auto& node : cmdOutput["items"].GetArray()) {
				rapidjson::Value entry(rapidjson::kObjectType);
				if (node.HasMember("metadata") && node["metadata"].HasMember("name")) {
					entry.AddMember("name",  rapidjson::StringRef(node["metadata"]["name"].GetString()), alloc);
					// Try to obtain a list of lists of addresses if the name can be recovered
					// If no addresses are available, omit the node
					if(node.HasMember("status") && node["status"].HasMember("addresses")) {
						rapidjson::Value nodeAddresses(rapidjson::kArrayType);
						for(auto& addr : node["status"]["addresses"].GetArray()) {
							std::string addrType(addr["type"].GetString());
							if (addrType == "InternalIP" || addrType == "ExternalIP") {
								rapidjson::Value address(rapidjson::kObjectType);
								address.AddMember("addressType", rapidjson::StringRef(addr["type"].GetString()), alloc);
								address.AddMember("address", rapidjson::StringRef(addr["address"].GetString()), alloc);
								nodeAddresses.PushBack(address, alloc);
							}
						}
						entry.AddMember("addresses", nodeAddresses, alloc);
						nodeInfo.PushBack(entry, alloc);
					}
				}
			}
			clusterData.AddMember("nodes", nodeInfo, alloc);
		} else {
			log_error("Unable to fetch all node information");
		}
	}
	clusterResult.AddMember("metadata", clusterData, alloc);
	return crow::response(to_string(clusterResult));
}

crow::response deleteCluster(PersistentStore& store, const crow::request& req, 
                             const std::string& clusterID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to delete " << clusterID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	const Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	//Users can only delete clusters which belong to groups of which they are members
	if(!store.userInGroup(user.id,cluster.owningGroup))
		return crow::response(403,generateError("Not authorized"));
	 //TODO: other restrictions on cluster deletions?
	bool force=(req.url_params.get("force")!=nullptr);

	auto err=internal::deleteCluster(store,cluster,force);
	if(!err.empty())
		return crow::response(500,generateError(err));
	
	return(crow::response(200));
}

namespace internal{
std::string removeClusterMonitoringCredential(PersistentStore& store, 
                                              const Cluster& cluster){
	if(cluster.monitoringCredential){
		log_info("Attempting to remove monitoring credential for " << cluster);
		bool removed=store.removeClusterMonitoringCredential(cluster.id);
		if(!removed)
			return "Failed to remove monitoring credential for Cluster "+cluster.name;
		//mark the credential record as revoked so it can be garbage collected
		bool revoked=store.revokeMonitoringCredential(cluster.monitoringCredential.accessKey);
		if(!removed)
			return "Failed to revoke used monitoring credential for Cluster "+cluster.monitoringCredential.accessKey;
	}
	return "";
}
	
std::string deleteCluster(PersistentStore& store, const Cluster& cluster, bool force){
	// Delete any remaining instances that are present on the cluster
	auto configPath=store.configPathForCluster(cluster.id);
	auto instances=store.listApplicationInstances();
	for (const ApplicationInstance& instance : instances){
		if (instance.cluster == cluster.id) {
			std::string result=internal::deleteApplicationInstance(store,instance,force);
			if(!force && !result.empty())
				return "Failed to delete cluster due to failure deleting instance: "+result;
		}
	}
	
	std::vector<std::future<std::string>> secretDeletions;
	std::vector<std::future<void>> namespaceDeletions;
	
	// Delete any remaining secrets present on the cluster
	auto secrets=store.listSecrets("",cluster.id);
	for (const Secret& secret : secrets){
		//std::string result=internal::deleteSecret(store,secret,/*force*/true);
		//if(!force && !result.empty())
		//	return "Failed to delete cluster due to failure deleting secret: "+result;
		secretDeletions.emplace_back(std::async(std::launch::async,[&store,secret](){ return internal::deleteSecret(store,secret,/*force*/true); }));
	}
	
	// Ensure secret deletions are complete before deleting namespaces
	for(auto& item : secretDeletions){
		auto result=item.get();
		if(!force && !result.empty())
			return "Failed to delete cluster due to failure deleting secret: "+result;
	}

	// Delete namespaces remaining on the cluster
	log_info("Deleting namespaces on cluster " << cluster.id);
	auto vos = store.listGroups();
	for (const Group& group : vos){
		namespaceDeletions.emplace_back(std::async(std::launch::async,[&cluster,&configPath,group](){
			//Delete the Group's namespace on the cluster, if it exists
			try{
				kubernetes::kubectl_delete_namespace(*configPath,group);
			}catch(std::exception& ex){
				log_error("Failed to delete namespace " << group.namespaceName() 
						  << " from " << cluster << ": " << ex.what());
			}
		}));
	}
	for(auto& item : namespaceDeletions)
		item.wait();
	
	// Delete our DNS record for the cluster
	auto dnsName="*."+store.dnsNameForCluster(cluster);
	if(store.canUpdateDNS()){
		Aws::Route53::Model::RRType type=Aws::Route53::Model::RRType::A;
		//try for an IPv4 record
		std::vector<std::string> record;
		try{
			record=store.getDNSRecord(type,dnsName);
		}
		catch(std::runtime_error& err){
			log_error("Unable to look up DNS record for " << cluster << ": " << err.what());
		}
		if(record.empty()){
			//try again as IPv6
			type=Aws::Route53::Model::RRType::AAAA;
			try{
				record=store.getDNSRecord(type,dnsName);
			}
			catch(std::runtime_error& err){
				log_error("Unable to look up DNS record for " << cluster << ": " << err.what());
			}
		}
		if(!record.empty()){
			bool success=false;
			try{
				success=store.removeDNSRecord(dnsName,record.front());
			}
			catch(std::runtime_error& err){
				log_error("Unable to remove DNS record for " << cluster << ": " << err.what());
			}
			if(!success)
				log_error("Failed to remove DNS record mapping " << dnsName << " to " << record.front());
		}
	}
	else{
		log_warn("Not able to change DNS records, so the record for " << dnsName << " cannot be deleted if it exists");
	}
	
	auto credRemoval=internal::removeClusterMonitoringCredential(store,cluster);
	if(!credRemoval.empty()){
		log_error(credRemoval);
		return "Cluster deletion failed: "+credRemoval;
	}
	
	log_info("Deleting " << cluster);
	if(!store.removeCluster(cluster.id))
		return "Cluster deletion failed";
	return "";
}
}

crow::response updateCluster(PersistentStore& store, const crow::request& req, 
                             const std::string& clusterID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to update " << clusterID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	//Users can only edit clusters which belong to groups of which they are members
	//unless they are admins
	if(!user.admin && !store.userInGroup(user.id,cluster.owningGroup))
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
		return crow::response(400,generateError("Missing cluster metadata in request"));
	if(!body["metadata"].IsObject())
		return crow::response(400,generateError("Incorrect type for metadata"));
		
	bool updateMainRecord=false;
	bool updateConfig=false;
	if(body["metadata"].HasMember("kubeconfig")){
		if(!body["metadata"]["kubeconfig"].IsString())
			return crow::response(400,generateError("Incorrect type for kubeconfig"));	
		cluster.config=body["metadata"]["kubeconfig"].GetString();
		updateMainRecord=true;
		updateConfig=true;
	}
	if(body["metadata"].HasMember("owningOrganization")){
		if(!body["metadata"]["owningOrganization"].IsString())
			return crow::response(400,generateError("Incorrect type for owningOrganization"));	
		cluster.owningOrganization=body["metadata"]["owningOrganization"].GetString();
		updateMainRecord=true;
	}
	std::vector<GeoLocation> locations;
	bool updateLocation=false;
	if(body["metadata"].HasMember("location")){
		if(!body["metadata"]["location"].IsArray())
			return crow::response(400,generateError("Incorrect type for location"));
		for(const auto& entry : body["metadata"]["location"].GetArray()){
			if(!entry.IsObject() || !entry.HasMember("lat") || !entry.HasMember("lon")
			  || !entry["lat"].IsNumber() || !entry["lon"].IsNumber())
				return crow::response(400,generateError("Incorrect type for location"));
			locations.push_back(GeoLocation{entry["lat"].GetDouble(),entry["lon"].GetDouble()});
			internal::supplmentLocation(store, locations.back());
		}
		updateLocation=true;
	}
	
	//TODO: don't do this unconditionally; figure out an appropriate time or place for it
	internal::setClusterDNSRecord(store,cluster);
	
	if(!updateMainRecord && !updateLocation){
		log_info("Requested update to " << cluster << " is trivial");
		return(crow::response(200));
	}
	
	log_info("Updating " << cluster);
	bool success=true;
	
	if(updateMainRecord)
		success&=store.updateCluster(cluster);
	if(updateLocation)
		success&=store.setLocationsForCluster(cluster.id, locations);
	
	if(!success){
		log_error("Failed to update " << cluster);
		return crow::response(500,generateError("Cluster update failed"));
	}
	
	#warning TODO: after updating config we should re-perform contact and helm initialization
	
	if(updateConfig){
		std::string resultMessage;
		try{
			resultMessage=internal::ensureClusterSetup(store,cluster);
		}
		catch(std::runtime_error& err){
			return crow::response(500,generateError(err.what()));
		}
	}
	
	return(crow::response(200));
}

crow::response listClusterAllowedgroups(PersistentStore& store, const crow::request& req, 
                                     const std::string& clusterID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list groups with access to cluster " << clusterID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list allowed groups
	
	Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	result.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	
	std::vector<std::string> groupIDs=store.listGroupsAllowedOnCluster(cluster.id);
	//if result is a wildcard skip the usual steps
	if(groupIDs.size()==1 && groupIDs.front()==PersistentStore::wildcard){
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("id", PersistentStore::wildcard, alloc);
		metadata.AddMember("name", PersistentStore::wildcardName, alloc);
		
		rapidjson::Value groupResult(rapidjson::kObjectType);
		groupResult.AddMember("apiVersion", "v1alpha3", alloc);
		groupResult.AddMember("kind", "Group", alloc);
		groupResult.AddMember("metadata", metadata, alloc);
		resultItems.PushBack(groupResult, alloc);
	}
	else{
		//include the owning Group, which implcitly always has access
		groupIDs.push_back(cluster.owningGroup); 
		
		resultItems.Reserve(groupIDs.size(), alloc);
		for (const std::string& groupID : groupIDs){
			Group group=store.findGroupByID(groupID);
			if(!group){
				log_error("Apparently invalid Group ID " << groupID 
						  << " listed for access to " << cluster);
				continue;
			}
			
			rapidjson::Value metadata(rapidjson::kObjectType);
			metadata.AddMember("id", groupID, alloc);
			metadata.AddMember("name", group.name, alloc);
			
			rapidjson::Value groupResult(rapidjson::kObjectType);
			groupResult.AddMember("apiVersion", "v1alpha3", alloc);
			groupResult.AddMember("kind", "Group", alloc);
			groupResult.AddMember("metadata", metadata, alloc);
			resultItems.PushBack(groupResult, alloc);
		}
	}
	result.AddMember("items", resultItems, alloc);
	
	return crow::response(to_string(result));
}

crow::response checkGroupClusterAccess(PersistentStore& store, const crow::request& req, 
									   const std::string& clusterID, const std::string& groupID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to check whether Group " << groupID << " access has to cluster " << clusterID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//validate input
	const Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("cluster", clusterID, alloc);
	result.AddMember("group", groupID, alloc);
	
	//handle wildcard requests specially
	if(groupID==PersistentStore::wildcard || groupID==PersistentStore::wildcardName){
		bool allowed=store.clusterAllowsAllGroups(clusterID);
		result.AddMember("accessAllowed", allowed, alloc);
		return crow::response(to_string(result));
	}
	
	const Group group=store.getGroup(groupID);
	if(!group) //more input validation
		return crow::response(404,generateError("Cluster not found"));
	
	//if the group is the owner of the cluster, the answer is yes and we're done
	if(group.id == cluster.owningGroup){
		result.AddMember("accessAllowed", true, alloc);
		return crow::response(to_string(result));
	}
	bool allowed=store.groupAllowedOnCluster(group.id,cluster.id);
	log_info(group << (allowed?" is ":" is not ") << "allowed on " << cluster);
	result.AddMember("accessAllowed", allowed, alloc);
	return crow::response(to_string(result));
}

crow::response grantGroupClusterAccess(PersistentStore& store, const crow::request& req, 
                                    const std::string& clusterID, const std::string& groupID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to grant Group " << groupID << " access to cluster " << clusterID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//validate input
	const Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	//only admins and cluster owners can grant other groups access
	if(!user.admin && !store.userInGroup(user.id,cluster.owningGroup))
		return crow::response(403,generateError("Not authorized"));
	
	bool success=false;
	
	//handle wildcard requests specially
	if(groupID==PersistentStore::wildcard || groupID==PersistentStore::wildcardName){
		log_info("Granting all groups access to " << cluster);
		success=store.addGroupToCluster(PersistentStore::wildcard,cluster.id);
	}
	else{
		const Group group=store.getGroup(groupID);
		if(!group)
			return crow::response(404,generateError("Group not found"));
		if(group.id==cluster.owningGroup)
			//the owning group always implicitly has access, 
			//so return success without making a pointless record
			return crow::response(200);
		
		log_info("Granting " << group << " access to " << cluster);
		success=store.addGroupToCluster(group.id,cluster.id);
	}
	
	if(!success)
		return crow::response(500,generateError("Granting Group access to cluster failed"));
	return(crow::response(200));
}

crow::response revokeGroupClusterAccess(PersistentStore& store, const crow::request& req, 
                                     const std::string& clusterID, const std::string& groupID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to revoke Group " << groupID << " access to cluster " << clusterID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//validate input
	const Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	//only admins and cluster owners can change other groups' access
	if(!user.admin && !store.userInGroup(user.id,cluster.owningGroup))
		return crow::response(403,generateError("Not authorized"));
	bool success=false;
	
	//handle wildcard requests specially
	if(groupID==PersistentStore::wildcard || groupID==PersistentStore::wildcardName){
		log_info("Removing universal Group access to " << cluster);
		success=store.removeGroupFromCluster(PersistentStore::wildcard,cluster.id);
	}
	else{
		const Group group=store.getGroup(groupID);
		if(!group)
			return crow::response(404,generateError("Group not found"));
		
		if(group.id==cluster.owningGroup)
			return crow::response(400,generateError("Cannot deny cluster access to owning Group"));
		
		log_info("Removing " << group << " access to " << cluster);
		success=store.removeGroupFromCluster(group.id,cluster.id);
	}
	
	if(!success)
		return crow::response(500,generateError("Removing Group access to cluster failed"));
	return(crow::response(200));
}

crow::response listClusterGroupAllowedApplications(PersistentStore& store, 
                                                const crow::request& req, 
                                                const std::string& clusterID, 
												const std::string& groupID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list applications Group " << groupID 
	         << " may use on cluster " << clusterID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//validate input
	const Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	const Group group=store.getGroup(groupID);
	if(!group)
		return crow::response(404,generateError("Group not found"));
	
	//only admins, cluster owners, and members of the Group in question can list 
	//the applications a Group is allowed to use
	if(!user.admin && !store.userInGroup(user.id,cluster.owningGroup) 
	   && !store.userInGroup(user.id,group.id))
		return crow::response(403,generateError("Not authorized"));
	
	std::set<std::string> allowed=store.listApplicationsGroupMayUseOnCluster(group.id, cluster.id);
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	result.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	for(const auto& application : allowed)
		resultItems.PushBack(rapidjson::Value(application,alloc), alloc);
	result.AddMember("items", resultItems, alloc);
	
	return crow::response(to_string(result));
}

crow::response allowGroupUseOfApplication(PersistentStore& store, const crow::request& req, 
                                       const std::string& clusterID, const std::string& groupID,
                                       const std::string& applicationName){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to grant Group " << groupID 
	         << " permission to use application " << applicationName 
	         << " on cluster " << clusterID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//validate input
	const Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	const Group group=store.getGroup(groupID);
	if(!group)
		return crow::response(404,generateError("Group not found"));
	
	//only admins and cluster owners may set the applications a Group is allowed to use
	if(!user.admin && !store.userInGroup(user.id,cluster.owningGroup))
		return crow::response(403,generateError("Not authorized"));
	
	log_info("Granting permission for " << group << " to use " << applicationName 
	         << " on " << cluster);
	bool success=store.allowVoToUseApplication(groupID, clusterID, applicationName);
	
	if(!success)
		return crow::response(500,generateError("Granting Group permission to use application failed"));
	return(crow::response(200));
}

crow::response denyGroupUseOfApplication(PersistentStore& store, const crow::request& req, 
                                      const std::string& clusterID, const std::string& groupID,
                                      const std::string& applicationName){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to remove Group " << groupID 
	         << " permission to use application " << applicationName 
	         << " on cluster " << clusterID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//validate input
	const Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	const Group group=store.getGroup(groupID);
	if(!group)
		return crow::response(404,generateError("Group not found"));
	
	//only admins and cluster owners may set the applications a Group is allowed to use
	if(!user.admin && !store.userInGroup(user.id,cluster.owningGroup))
		return crow::response(403,generateError("Not authorized"));
	
	log_info("Revoking permission for " << group << " to use " << applicationName 
	         << " on " << cluster);
	bool success=store.denyGroupUseOfApplication(groupID, clusterID, applicationName);
	
	if(!success)
		return crow::response(500,generateError("Removing Group permission to use application failed"));
	return(crow::response(200));
}

crow::response getClusterMonitoringCredential(PersistentStore& store, 
                                              const crow::request& req,
                                              const std::string& clusterID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to fetch the monitoring credential for Cluster " << clusterID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	//only admins and cluster owners may get the monitoring credential
	if(!user.admin && !store.userInGroup(user.id,cluster.owningGroup))
		return crow::response(403,generateError("Not authorized"));
	
	if(!cluster.monitoringCredential){
		log_info("Attempting to assign monitoring credential for " << cluster);
		S3Credential cred;
		std::string errMsg;
		std::tie(cred,errMsg)=store.allocateMonitoringCredential();
		if(!cred){
			log_error("Failed to allocate monitoring credential for " << cluster);
			
			if(!store.getOpsEmail().empty()){
				//send email notification to the platform Ops team
				EmailClient::Email message;
				message.fromAddress="noreply@slate.io";
				message.toAddresses={store.getOpsEmail()};
				message.subject="API Server: Failed to allocate monitoring credential";
				message.body="Allocation of a monitoring credential for the "+
				cluster.name+" cluster was requested but could not be fulfilled:\n"
				+errMsg;
				store.getEmailClient().sendEmail(message);
			}
			
			return crow::response(500,generateError("Allocating monitoring credential failed"));
		}
		bool set=store.setClusterMonitoringCredential(cluster.id, cred);
		if(!set){
			log_error("Failed to set monitoring credential for " << cluster);
			if(!store.getOpsEmail().empty()){
				//send email notification to the platform Ops team
				EmailClient::Email message;
				message.fromAddress="noreply@slate.io";
				message.toAddresses={store.getOpsEmail()};
				message.subject="API Server: Failed to set monitoring credential";
				message.body="The monitoring credential "+cred.accessKey+" was allocated for"
				+cluster.name+" cluster but adding it to the cluster record in "
				"the persistent store failed. This inconsistent state should be "
				"manually resolved.";
				store.getEmailClient().sendEmail(message);
			}
			return crow::response(500,generateError("Recording monitoring credential failed"));
		}
		cluster.monitoringCredential=cred;
		
		auto allCreds=store.listMonitoringCredentials();
		unsigned int availableCreds=std::count_if(allCreds.begin(),allCreds.end(),
			[](const S3Credential& c){ return !c.inUse && !c.revoked; });
		if(availableCreds<3 && !store.getOpsEmail().empty()){
			//send email notification to the platform Ops team
			EmailClient::Email message;
			message.fromAddress="noreply@slate.io";
			message.toAddresses={store.getOpsEmail()};
			message.subject="API Server: Low number of monitoring credentials available";
			message.body=(availableCreds?"Only ":"")+std::to_string(availableCreds)
			+" credential"+std::string(availableCreds!=1?"":"s")+" remain available for allocation.";
			store.getEmailClient().sendEmail(message);
		}
	}
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "MonitoringCredential", alloc);
	rapidjson::Value credData(rapidjson::kObjectType);
	credData.AddMember("accessKey", cluster.monitoringCredential.accessKey, alloc);
	credData.AddMember("secretKey", cluster.monitoringCredential.secretKey, alloc);
	credData.AddMember("inUse", cluster.monitoringCredential.inUse, alloc);
	credData.AddMember("revoked", cluster.monitoringCredential.revoked, alloc);
	result.AddMember("metadata", credData, alloc);
	
	return crow::response(to_string(result));
}

crow::response removeClusterMonitoringCredential(PersistentStore& store, 
                                                 const crow::request& req,
                                                 const std::string& clusterID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to remove the monitoring credential for Cluster " << clusterID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	const Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	//only admins and cluster owners may get the monitoring credential
	if(!user.admin && !store.userInGroup(user.id,cluster.owningGroup))
		return crow::response(403,generateError("Not authorized"));
	
	auto credRemoval=internal::removeClusterMonitoringCredential(store,cluster);
	if(!credRemoval.empty()){
		log_error(credRemoval);
		return crow::response(500,generateError("Removing monitoring credential failed"));
	}
	
	return crow::response(200);
}

enum class ClusterConsistencyState{
	Unreachable, HelmFailure, Inconsistent, Consistent
};

struct ClusterConsistencyResult{
	ClusterConsistencyState status;
	
	std::vector<ApplicationInstance> expectedInstances;
	std::set<std::string> existingInstanceNames;
	
	std::map<std::string,const ApplicationInstance&> expectedInstancesByName;
	std::set<std::string> missingInstances;
	std::set<std::string> unexpectedInstances;
	
	std::vector<Secret> expectedSecrets;
	std::set<std::string> existingSecretNames;
	
	std::map<std::string,const Secret&> expectedSecretsByName;
	std::set<std::string> missingSecrets;
	std::set<std::string> unexpectedSecrets;
	
	ClusterConsistencyResult(PersistentStore& store, const Cluster& cluster);
	
	rapidjson::Document toJSON() const;
};

namespace internal{

bool pingCluster(PersistentStore& store, const Cluster& cluster){
	auto configPath=store.configPathForCluster(cluster.id);

	bool contactable=false;
	//check that the cluster can be reached
	auto clusterInfo=kubernetes::kubectl(*configPath,{"get","serviceaccounts","-o=jsonpath={.items[*].metadata.name}"});
	if(clusterInfo.status || 
	   clusterInfo.output.find("default")==std::string::npos){
		log_info("Unable to contact " << cluster << ": " << clusterInfo.error);
		return false;
	}
	else{
		log_info("Success contacting " << cluster);
		return true;
	}
}

}

ClusterConsistencyResult::ClusterConsistencyResult(PersistentStore& store, const Cluster& cluster){
	auto configPath=store.configPathForCluster(cluster.id);
	
	status=ClusterConsistencyState::Consistent;
	
	//check that the cluster can be reached
	if(!internal::pingCluster(store, cluster)){
		status=ClusterConsistencyState::Unreachable;
		return;
	}
	
	//figure out what instances helm thinks exist
	auto instanceInfo=kubernetes::helm(*configPath,cluster.systemNamespace,{"list"});
	if(instanceInfo.status){
		log_info("Unable to list helm releases on " << cluster);
		status=ClusterConsistencyState::HelmFailure;
		return;
	}
	{
		bool first=true;
		for(const auto& line : string_split_lines(instanceInfo.output)){
			if(first){ //skip helm's header line
				first=false;
				continue;
			}
			auto items=string_split_columns(line,'\t',false);
			if(items.empty())
				continue;
			existingInstanceNames.insert(items.front());
		}
	}
	
	//figure out what instances are supposed to exist
	expectedInstances=store.listApplicationInstancesByClusterOrGroup("", cluster.id);
	std::set<std::string> expectedInstanceNames;
	for(const auto& instance : expectedInstances){
		expectedInstanceNames.insert(instance.name);
		expectedInstancesByName.emplace(instance.name,instance);
	}
	
	std::set_difference(expectedInstanceNames.begin(),expectedInstanceNames.end(),
						existingInstanceNames.begin(),existingInstanceNames.end(),
						std::inserter(missingInstances,missingInstances.begin()));
	
	std::set_difference(existingInstanceNames.begin(),existingInstanceNames.end(),
						expectedInstanceNames.begin(),expectedInstanceNames.end(),
						std::inserter(unexpectedInstances,unexpectedInstances.begin()));
	
	log_info(cluster << " is missing " << missingInstances.size() << " instance"
			 << (missingInstances.size()!=1 ? "s" : "") << " and has " <<
			 unexpectedInstances.size() << " unexpected instance" << 
			 (unexpectedInstances.size()!=1 ? "s" : ""));
	
	if(!missingInstances.empty() || !unexpectedInstances.empty())
		status=ClusterConsistencyState::Inconsistent;
	
	//figure out what secrets currently exist
	//start by learning which namespaces we can see, in which we should search for secrets
	auto namespaceInfo=kubernetes::kubectl(*configPath,{"get","clusternamespaces","-o=jsonpath={.items[*].metadata.name}"});
	std::vector<std::string> namespaceNames=string_split_columns(namespaceInfo.output,' ',false);
	//iterate over namespaces, listing secrets
	for(const auto& namespaceName : namespaceNames){
		if(namespaceName.find(Group::namespacePrefix())!=0){
			log_error("Found peculiar namespace: " << namespaceName);
			continue;
		}
		std::string groupName=namespaceName.substr(Group::namespacePrefix().size());
		auto secretsInfo=kubernetes::kubectl(*configPath,{"get","secrets","-n",namespaceName,"-o=jsonpath={.items[*].metadata.name}"});
		for(const auto& secretName : string_split_columns(secretsInfo.output,' ',false)){
			if(secretName.find("default-token-")==0)
				continue; //ignore kubernetes infrastructure
			existingSecretNames.insert(groupName+":"+secretName);
		}
	}
	
	//figure out what secrets are supposed to exist
	expectedSecrets=store.listSecrets("", cluster.id);
	std::set<std::string> expectedSecretNames;
	for(const auto& secret : expectedSecrets){
		std::string groupName=store.findGroupByID(secret.group).name;
		std::string secretName=groupName+":"+secret.name;
		expectedSecretNames.insert(secretName);
		expectedSecretsByName.emplace(secretName,secret);
	}
	
	std::set_difference(expectedSecretNames.begin(),expectedSecretNames.end(),
						existingSecretNames.begin(),existingSecretNames.end(),
						std::inserter(missingSecrets,missingSecrets.begin()));
	
	std::set_difference(existingSecretNames.begin(),existingSecretNames.end(),
						expectedSecretNames.begin(),expectedSecretNames.end(),
						std::inserter(unexpectedSecrets,unexpectedSecrets.begin()));
	
	log_info(cluster << " is missing " << missingSecrets.size() << " secret"
			 << (missingSecrets.size()!=1 ? "s" : "") << " and has " <<
			 unexpectedSecrets.size() << " unexpected secret" << 
			 (unexpectedSecrets.size()!=1 ? "s" : ""));
	
	if(!missingSecrets.empty() || !unexpectedSecrets.empty())
		status=ClusterConsistencyState::Inconsistent;
	
}

rapidjson::Document ClusterConsistencyResult::toJSON() const{
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	
	switch(status){
		case ClusterConsistencyState::Unreachable:
			result.AddMember("status", "Unreachable", alloc); break;
		case ClusterConsistencyState::HelmFailure:
			result.AddMember("status", "HelmFailure", alloc); break;
		case ClusterConsistencyState::Inconsistent:
			result.AddMember("status", "Inconsistent", alloc); break;
		case ClusterConsistencyState::Consistent:
			result.AddMember("status", "Consistent", alloc); break;
	}
	
	rapidjson::Value missingResults(rapidjson::kArrayType);
	missingResults.Reserve(missingInstances.size(), alloc);
	for(const auto& missing : missingInstances){
		const ApplicationInstance& instance=expectedInstancesByName.find(missing)->second;
		rapidjson::Value missingResult(rapidjson::kObjectType);
		missingResult.AddMember("apiVersion", "v1alpha3", alloc);
		missingResult.AddMember("kind", "ApplicationInstance", alloc);
		rapidjson::Value instanceData(rapidjson::kObjectType);
		instanceData.AddMember("id", instance.id, alloc);
		instanceData.AddMember("name", instance.name, alloc);
		instanceData.AddMember("application", instance.application, alloc);
		instanceData.AddMember("group", instance.owningGroup, alloc);
		instanceData.AddMember("cluster", instance.cluster, alloc);
		instanceData.AddMember("created", instance.ctime, alloc);
		missingResult.AddMember("metadata", instanceData, alloc);
		missingResults.PushBack(missingResult, alloc);
	}
	result.AddMember("missingInstances", missingResults, alloc);
	
	rapidjson::Value unexpectedResults(rapidjson::kArrayType);
	unexpectedResults.Reserve(unexpectedInstances.size(), alloc);
	for(const auto& extra : unexpectedInstances){
		rapidjson::Value unexpectedResult(rapidjson::kStringType);
		unexpectedResult.SetString(extra,alloc);
		unexpectedResults.PushBack(unexpectedResult,alloc);
	}
	result.AddMember("unexpectedInstances", unexpectedResults, alloc);
	
	result.AddMember("missingSecrets", rapidjson::Value((uint64_t)missingSecrets.size()), alloc);
	result.AddMember("unexpectedSecrets", rapidjson::Value((uint64_t)unexpectedSecrets.size()), alloc);
	
	return result;
}

crow::response pingCluster(PersistentStore& store, const crow::request& req,
                           const std::string& clusterID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to ping cluster " << clusterID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//validate input
	const Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
		
	bool useCache=req.url_params.get("cache");
	
	CacheRecord<bool> cacheResult;
	if(useCache)
		cacheResult=store.getCachedClusterReachability(cluster.id);
	
	bool reachable;
	if(cacheResult) //if we got a valid result it can only be because we asked for it
		reachable=cacheResult.record;
	else{ //if we either didn't use the cache, it was empty, or expired, get a fresh result
		reachable=internal::pingCluster(store, cluster);
		//update the cache
		store.cacheClusterReachability(cluster.id, reachable);
	}
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("reachable", reachable, alloc);
	
	return crow::response(to_string(result));
}

crow::response verifyCluster(PersistentStore& store, const crow::request& req,
                             const std::string& clusterID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to verify the state of cluster " << clusterID << " from " << req.remote_endpoint);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//validate input
	const Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	return crow::response(to_string(ClusterConsistencyResult(store, cluster).toJSON()));
}

crow::response repairCluster(PersistentStore& store, const crow::request& req,
                             const std::string& clusterID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to repair cluster " << clusterID << " from " << req.remote_endpoint);
	if(!user || !user.admin) //only admins can perform this action
		return crow::response(403,generateError("Not authorized"));
	
	//validate input
	const Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(404,generateError("Cluster not found"));
	
	enum class Strategy{
		Reinstall, Wipe
	};
	
	//TODO: determine this from a query parameter
	Strategy strategy=Strategy::Reinstall;
	
	//figure out what's wrong
	ClusterConsistencyResult state(store, cluster);
	
	if(strategy==Strategy::Reinstall){
		//Try to put back each thing which isn't where it should be
		//TODO: implement this
	}
	else if(strategy==Strategy::Wipe){
		//Delete records of things which no longer exist
		//TODO: implement this
	}
	return crow::response(200);
}
