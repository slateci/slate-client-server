#include "ApplicationCommands.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"

#include <yaml-cpp/exceptions.h>
#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/impl.h>
#include "yaml-cpp/node/convert.h"
#include "yaml-cpp/node/detail/impl.h"
#include <yaml-cpp/node/parse.h>

#include "KubeInterface.h"
#include "Logging.h"
#include "Archive.h"
#include "FileSystem.h"
#include "ServerUtilities.h"

//#include <regex>

Application::Repository selectRepo(const crow::request& req){
	Application::Repository repo=Application::MainRepository;
	if(req.url_params.get("dev"))
		repo=Application::DevelopmentRepository;
	if(req.url_params.get("test"))
		repo=Application::TestRepository;
	return repo;
}

std::string getRepoName(Application::Repository repo){
	switch(repo){
		case Application::MainRepository: return "slate";
		case Application::DevelopmentRepository: return "slate-dev";
		case Application::TestRepository: return "local";
	}
	// Should never get here but return something just in case
	return "impossible-repo";
}

///Remove all text contained between the strings
///"### SLATE-START ###" and "### SLATE-END ###"
std::string filterValuesFile(std::string data){
	const static std::string startMarker="### SLATE-START ###";
	const static std::string endMarker="### SLATE-END ###";
	std::size_t pos=0;
	while(true){
		std::size_t startPos=data.find(startMarker,pos);
		if(startPos==std::string::npos)
			break;
		std::size_t endPos=data.find(endMarker,startPos);
		if(endPos==std::string::npos){
			log_error("Unbalanced SLATE-internal markers in values data");
			break;
		}
		data.erase(startPos,endPos-startPos + endMarker.size());
		pos=startPos;
	}
	return data;
}

crow::response listApplications(PersistentStore& store, const crow::request& req){
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
	if(!user) //non-users _are_ allowed to list applications
		log_info("Anonymous user requested to list applications from " << req.remote_endpoint);
	else
		log_info(user << " requested to list applications from " << req.remote_endpoint);
	//All users are allowed to list applications

	std::string repoName=getRepoName(selectRepo(req));
	std::vector<Application> applications;
	try{
		applications=store.listApplications(repoName);
	}
	catch(std::runtime_error& err){
		setWebSpanError(span, std::string("helm search failed: ").append(err.what()), 500);
		span->End();
		return crow::response(500,generateError("helm search failed"));
	}

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();

	result.AddMember("apiVersion", "v1alpha3", alloc);

	rapidjson::Value resultItems(rapidjson::kArrayType);
	for(const Application& application : applications){
		rapidjson::Value applicationResult(rapidjson::kObjectType);
		applicationResult.AddMember("apiVersion", "v1alpha3", alloc);
		applicationResult.AddMember("kind", "Application", alloc);
		rapidjson::Value applicationData(rapidjson::kObjectType);

		applicationData.AddMember("name", application.name, alloc);
		applicationData.AddMember("app_version", application.version, alloc);
		applicationData.AddMember("chart_version", application.chartVersion, alloc);
		applicationData.AddMember("description", application.description, alloc);

		applicationResult.AddMember("metadata", applicationData, alloc);
		resultItems.PushBack(applicationResult, alloc);
	}

	result.AddMember("items", resultItems, alloc);

	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	log_info("application listing completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
	span->End();
	return crow::response(to_string(result));
}

crow::response fetchApplicationConfig(PersistentStore& store, const crow::request& req, const std::string& appName){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);

	const User user=authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	if(!user) //non-users _are_ allowed to obtain configurations for all applications
		log_info("Anonymous user requested to fetch configuration for application " << appName << " from " << req.remote_endpoint);
	else
		log_info(user << " requested to fetch configuration for application " << appName << " from " << req.remote_endpoint);
	//All users may obtain configurations for all applications

	auto repo=selectRepo(req);
	std::string repoName=getRepoName(repo);

	std::string chartVersion = "";
	if (req.url_params.get("chartVersion"))
		chartVersion = req.url_params.get("chartVersion");
		
	Application application;
	try{
		application=store.findApplication(repoName, appName, chartVersion);
	}
	catch(std::runtime_error& err){
		setWebSpanError(span, std::string("Runtime Error: ").append(err.what()), 500);
		span->End();
		return crow::response(500);
	}
	if(!application) {
		const std::string& err = "Application not found";
		setWebSpanError(span, err, 404);
		span->End();
		return crow::response(404, generateError(err));
	}
	auto commandResult = runCommand("helm",{"inspect","values",repoName + "/" + application.name, "--version", application.chartVersion});
	if(commandResult.status){
		const std::string& err = "Unable to fetch application config";
		setWebSpanError(span, err, 500);
		span->End();
		log_error("Command failed: helm inspect " << (repoName + "/" + appName) << ": [exit] " << commandResult.status << " [err] " << commandResult.error << " [out] " << commandResult.output);
		return crow::response(500, generateError(err));
	}

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "Configuration", alloc);

	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", appName, alloc);
	metadata.AddMember("version", application.version, alloc);
	metadata.AddMember("chartVersion", application.chartVersion, alloc);
	result.AddMember("metadata", metadata, alloc);

	rapidjson::Value spec(rapidjson::kObjectType);
	spec.AddMember("body", filterValuesFile(commandResult.output), alloc);
	result.AddMember("spec", spec, alloc);

	span->End();
	return crow::response(to_string(result));
}

crow::response fetchApplicationVersions(PersistentStore& store, const crow::request& req, const std::string& appName){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
;
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);

	const User user=authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	if(!user) //non-users _are_ allowed to get documentation
		log_info("Anonymous user requested to fetch versions for application " << appName << " from " << req.remote_endpoint);
	else
		log_info(user << " requested to fetch versions for application " << appName << " from " << req.remote_endpoint);
	//All users may get documentation

	auto repo=selectRepo(req);
	std::string repoName=getRepoName(repo);
		
	Application application;//=findApplication(appName,repo);
	try{
		application=store.findApplication(repoName, appName, "");
	}
	catch(std::runtime_error& err){
		setWebSpanError(span, std::string("Runtime error: ").append(err.what()), 500);
		span->End();
		return crow::response(500);
	}
	if(!application) {
		const std::string& err = "Application not found";
		setWebSpanError(span, err, 404);
		span->End();
		return crow::response(404, generateError(err));
	}
	
	auto commandResult = runCommand("helm",{"search","repo",repoName + "/" + appName, "--versions", "-o", "json"});
	if(commandResult.status){
		const std::string& err = "Unable to fetch application versions";
		setWebSpanError(span, err, 500);
		span->End();
		log_error("Command failed: helm search " << (repoName + "/" + appName) << ": [exit] " << commandResult.status << " [err] " << commandResult.error << " [out] " << commandResult.output);
		return crow::response(500, generateError(err));
	}

/*
	std::regex match_version_strings("[:d:]+\.[:d:]+\.[:d:]+");
	auto versions_begin = std::sregex_iterator(commandResult.output.begin(), commandResult.output.end(), match_version_strings);
	auto versions_end = std::sregex_iterator();
	std::string versions = "";
	for (std::sregex_iterator version = versions_begin; version != versions_end; version++){
		versions.append((*version).str());
		versions.append("\n");
	} */

	rapidjson::Document chartSearchDetails;
	chartSearchDetails.Parse(commandResult.output.c_str());
	std::string versions = "";

	for(const auto& chartEntry : chartSearchDetails.GetArray()){
		versions.append(chartEntry["version"].GetString());
		versions.append("\n");
	}

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "Configuration", alloc);

	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", appName, alloc);
	result.AddMember("metadata", metadata, alloc);

	rapidjson::Value spec(rapidjson::kObjectType);
	spec.AddMember("body", versions, alloc);
	result.AddMember("spec", spec, alloc);

	span->End();
	return crow::response(to_string(result));
}

crow::response fetchApplicationDocumentation(PersistentStore& store, const crow::request& req, const std::string& appName){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
;
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	const User user=authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	if(!user) //non-users _are_ allowed to get documentation
		log_info("Anonymous user requested to fetch documentation for application " << appName << " from " << req.remote_endpoint);
	else
		log_info(user << " requested to fetch configuration for application " << appName << " from " << req.remote_endpoint);
	//All users may get documentation

	auto repo=selectRepo(req);
	std::string repoName=getRepoName(repo);

	std::string chartVersion = "";
	if (req.url_params.get("chartVersion"))
		chartVersion = req.url_params.get("chartVersion");
		
	Application application;//=findApplication(appName,repo);
	try{
		application=store.findApplication(repoName, appName, chartVersion);
	}
	catch(std::runtime_error& err){
		setWebSpanError(span, std::string("Runtime error: ").append(err.what()), 500);
		span->End();
		return crow::response(500);
	}
	if(!application) {
		const std::string& err = "Application not found";
		setWebSpanError(span, err, 404);
		span->End();
		return crow::response(404, generateError(err));
	}
	
	auto commandResult = runCommand("helm",{"inspect","readme",repoName + "/" + application.name, "--version", application.chartVersion});
	if(commandResult.status){
		const std::string& err = "Unable to fetch application readme";
		setWebSpanError(span, err, 500);
		span->End();
		log_error("Command failed: helm inspect " << (repoName + "/" + appName) << ": [exit] " << commandResult.status << " [err] " << commandResult.error << " [out] " << commandResult.output);
		return crow::response(500, generateError(err));
	}

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "Configuration", alloc);

	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", appName, alloc);
	metadata.AddMember("version", application.version, alloc);
	metadata.AddMember("chartVersion", application.chartVersion, alloc);
	result.AddMember("metadata", metadata, alloc);

	rapidjson::Value spec(rapidjson::kObjectType);
	spec.AddMember("body", commandResult.output, alloc);
	result.AddMember("spec", spec, alloc);

	span->End();
	return crow::response(to_string(result));
}

namespace internal{

std::string assembleExtraHelmValues(const PersistentStore& store, const Cluster& cluster, const ApplicationInstance& instance, const Group& group){
	std::string additionalValues;
	if(!store.getAppLoggingServerName().empty()){
		additionalValues+="SLATE.Logging.Enabled=true";
		additionalValues+=",SLATE.Logging.Server.Name="+store.getAppLoggingServerName();
		additionalValues+=",SLATE.Logging.Server.Port="+std::to_string(store.getAppLoggingServerPort());
	}
	else
		additionalValues+="SLATE.Logging.Enabled=false";
	// Add these values to every application
	additionalValues+=",SLATE.Cluster.Name="+cluster.name;
	additionalValues+=",SLATE.Cluster.DNSName="+store.dnsNameForCluster(cluster);
	additionalValues+=",SLATE.Cluster.ClusterID="+instance.cluster;
	additionalValues+=",SLATE.Instance.ID="+instance.id;
	additionalValues+=",SLATE.Metadata.Group="+group.name;
	additionalValues+=",SLATE.Metadata.GroupEmail="+group.email;
	
	return additionalValues;
}
}

///Internal function which requires that initial authorization checks have already been performed
crow::response installApplicationImpl(PersistentStore& store, const User& user, const std::string& appName, const std::string& installSrc, const rapidjson::Document& body){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setInternalSpanAttributes(attributes);
	auto options = getInternalSpanOptions();
	auto span = tracer->StartSpan("installApplicationImpl", attributes, options);
	auto scope = tracer->WithActiveSpan(span);
	span->SetAttribute("user", user.name);

	if(!body.HasMember("group")) {
		const std::string& err = "Missing Group";
		setWebSpanError(span, err, 400);
		span->End();
		return crow::response(400, generateError(err));
	}
	if(!body["group"].IsString()) {
		const std::string& err = "Incorrect type for Group";
		setWebSpanError(span, err, 400);
		span->End();
		return crow::response(400, generateError(err));
	}
	const std::string groupID=body["group"].GetString();
	if(!body.HasMember("cluster")) {
		const std::string& err = "Missing cluster";
		setWebSpanError(span, err, 400);
		span->End();
		return crow::response(400, generateError(err));
	}
	if(!body["cluster"].IsString()) {
		const std::string& err = "Incorrect type for cluster";
		setWebSpanError(span, err, 400);
		span->End();
		return crow::response(400, generateError(err));
	}
	const std::string clusterID=body["cluster"].GetString();
	if(!body.HasMember("configuration")) {
		const std::string& err = "Missing configuration";
		setWebSpanError(span, err, 400);
		span->End();
		return crow::response(400, generateError(err));
	}
	if(!body["configuration"].IsString()) {
		const std::string& err = "Incorrect type for configuration";
		setWebSpanError(span, err, 400);
		span->End();
		return crow::response(400, generateError(err));
	}
	const std::string config=body["configuration"].GetString();

	std::string chartVersion = "";
	if(body.HasMember("chartVersion") && body["chartVersion"].IsString())
		chartVersion = body["chartVersion"].GetString();

	std::string tag; //start by assuming this is empty
	bool gotTag=false;
	
	std::string yamlError;
	//returns true if YAML parsing was successful
	auto extractInstanceTag=[&tag,&gotTag](const std::string& config, std::string& error)->bool{
		std::vector<YAML::Node> parsedConfig;
		try{
			parsedConfig=YAML::LoadAll(config);
		}catch(const YAML::ParserException& ex){
			error = ex.what();
			return false;
		}
		for(const auto& document : parsedConfig){
			if(document.IsMap() && document["Instance"] && document["Instance"].IsScalar()){
				tag=document["Instance"].as<std::string>();
				gotTag=true;
			}
		}
		return true;
	};
	if(!config.empty()){ //see if an instance tag is specified in the configuration
		if(!extractInstanceTag(config, yamlError)) {
			const std::string& err = "Configuration could not be parsed as YAML.\n" + yamlError;
			setWebSpanError(span, err, 400);
			span->End();
			return crow::response(400, generateError(err));
		}
	}
	//if the user did not specify a tag we must parse the base helm chart to 
	//find out what the default value is
	if(!gotTag){
		auto commandResult = runCommand("helm",{"inspect","values",installSrc, "--version", chartVersion});
		if(commandResult.status){
			log_error("Command failed: helm inspect values " << installSrc << ": [exit] " << commandResult.status << " [err] " << commandResult.error << " [out] " << commandResult.output);
			const std::string& err = "Unable to fetch default application config";
			setWebSpanError(span, err, 500);
			span->End();
			return crow::response(500, generateError(err));
		}
		if(!extractInstanceTag(commandResult.output, yamlError)) {
			const std::string& err = "Default configuration could not be parsed as YAML.\n" + yamlError;
			setWebSpanError(span, err, 500);
			span->End();
			return crow::response(500, generateError(err));
		}
	}
	if(!gotTag){
		const std::string& err = "Failed to determine instance tag for "+appName;
		setWebSpanError(span, err, 500);
		span->End();
		log_error(err);
		return crow::response(500, generateError(err));
	}
	
	//Direct specification of instance tags is forbidden
	/*//if a tag was manually specified, let it override
	if(body.HasMember("tag")){
		if(!body["tag"].IsString())
			return crow::response(400,generateError("Incorrect type for tag"));
		tag=body["tag"].GetString();
	}*/
	// TODO: replace with regex
	if(tag.find_first_not_of("abcdefghijklmnopqrstuvwxzy0123456789-")!=std::string::npos) {
		const std::string& err = "Instance tags names may only contain [a-z], [0-9] and -";
		setWebSpanError(span, err, 400);
		span->End();
		return crow::response(400, generateError(err));
	}
	if(!tag.empty() && tag.back()=='-') {
		const std::string& err = "Instance tags names may not end with a dash";
		setWebSpanError(span, err, 400);
		span->End();
		return crow::response(400, generateError(err));
	}
	
	//validate input
	const Group group=store.getGroup(groupID);
	if(!group) {
		const std::string& err = "Invalid Group";
		setWebSpanError(span, err, 400);
		span->End();
		return crow::response(400, generateError(err));
	}
	const Cluster cluster=store.getCluster(clusterID);
	if(!cluster) {
		const std::string& err = "Invalid Cluster";
		setWebSpanError(span, err, 400);
		span->End();
		return crow::response(400, generateError(err));
	}
	span->SetAttribute("cluster", cluster.name);
	//A user must belong to a Group to install applications on its behalf
	if(!store.userInGroup(user.id,group.id)) {
		const std::string& err = "Not authorized";
		setWebSpanError(span, err, 403);
		span->End();
		return crow::response(403, generateError(err));
	}
	//The Group must own or be allowed to access to the cluster to install
	//applications to it. If the Group is not the cluster owner it must also have 
	//permission to install the specific application. 
	log_info(cluster << " is owned by " << cluster.owningGroup << ", install request is from " << group);
	if(group.id!=cluster.owningGroup){
		if(!store.groupAllowedOnCluster(group.id,cluster.id)) {
			const std::string& err = "Not authorized";
			setWebSpanError(span, err, 403);
			span->End();
			return crow::response(403, generateError(err));
		}
		if(!store.groupMayUseApplication(group.id, cluster.id, appName)) {
			const std::string& err = "Not authorized";
			setWebSpanError(span, err, 403);
			span->End();
			return crow::response(403, generateError(err));
		}
	}

	ApplicationInstance instance;
	instance.valid=true;
	instance.id=idGenerator.generateInstanceID();
	instance.application=installSrc;
	instance.owningGroup=group.id;
	instance.cluster=cluster.id;
	//TODO: strip comments and whitespace from config
	instance.config=reduceYAML(config);
	if(instance.config.empty())
		instance.config="\n"; //empty strings upset Dynamo
	instance.ctime=timestamp();
	instance.name=appName;
	if(!tag.empty())
		instance.name+="-"+tag;
	if(instance.name.size()>63) {
		const std::string& err = "Instance tag too long";
		setWebSpanError(span, err, 400);
		span->End();
		return crow::response(400, generateError(err));
	}
	
	//find all instances in the group on a specific cluster
	auto groupInsts=store.listApplicationInstancesByClusterOrGroup(group.id, cluster.id);
	//get if the name is already in use (no need to check across namespaces with Helm v3+)
	for(const auto& otherInst : groupInsts){
		if(otherInst.name == instance.name) {
			const std::string& err = "Instance name is already in use,"
			                         " consider using a different tag";
			setWebSpanError(span, err, 400);
			span->End();
			return crow::response(400, generateError(err));
		}
	}
	
	//write configuration to a file for helm's benefit
	FileHandle instanceConfig=makeTemporaryFile(instance.id);
	{
		std::ofstream outfile(instanceConfig.path());
		outfile << instance.config;
		if(!outfile){
			const std::string& err = "Failed to write instance configuration to " + instanceConfig.path();
			setWebSpanError(span, err, 500);
			span->End();
			log_error(err);
			return crow::response(500,generateError(err));
		}
	}
	
	log_info("Instantiating " << appName << " on " << cluster);
	//first record the instance in the persistent store
	bool success=store.addApplicationInstance(instance);
	
	if(!success){
		const std::string& err = "Failed to add application instance record to the persistent store";
		setWebSpanError(span, err, 500);
		span->End();
		log_error(err);
		return crow::response(500,generateError(err));
	}
	
	std::string additionalValues=internal::assembleExtraHelmValues(store,cluster,instance,group);
	log_info("Additional values: " << additionalValues);

	auto clusterConfig=store.configPathForCluster(cluster.id);

	span->AddEvent("kubectl create ns");
	setInternalSpanAttributes(attributes);
	options = getInternalSpanOptions();
	auto nsSpan = tracer->StartSpan("kubectl create ns", attributes, options);
	auto nsScope = tracer->WithActiveSpan(nsSpan);
	try{
		kubernetes::kubectl_create_namespace(*clusterConfig, group);
		nsSpan->End();
	}
	catch(std::runtime_error& err){
		std::ostringstream errMsg;
		errMsg << "Failure installing " << appName << " on " << cluster << ": " << err.what();
		log_error(errMsg.str());
		setSpanError(nsSpan, errMsg.str());
		store.removeApplicationInstance(instance.id);
		nsSpan->End();
		return crow::response(500,generateError(err.what()));
	}

	
	std::vector<std::string> installArgs={"install",
	  instance.name,
	  installSrc,
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

	span->AddEvent("helm install");
	setInternalSpanAttributes(attributes);
	options = getInternalSpanOptions();
	auto helmSpan = tracer->StartSpan("helm install", attributes, options);
	auto helmScope = tracer->WithActiveSpan(helmSpan);
	auto commandResult=runCommand("helm",installArgs,{{"KUBECONFIG",*clusterConfig}});
	//if application instantiation fails, remove record from DB again
	if(commandResult.status || 
	   (commandResult.output.find("STATUS: DEPLOYED")==std::string::npos
	    && commandResult.output.find("STATUS: deployed")==std::string::npos)){
		std::string errMsg="Failed to start application instance with helm:\n[exit] "+std::to_string(commandResult.status)+"\n[err]: "+commandResult.error+"\n[out]: "+commandResult.output+"\n system namespace: "+cluster.systemNamespace;
		log_error(errMsg);
		setSpanError(helmSpan, errMsg);
		helmSpan->End();

		store.removeApplicationInstance(instance.id);
		//helm will (unhelpfully) keep broken 'releases' around, so clean up here
		std::vector<std::string> deleteArgs={"delete",instance.name};
		if(helmMajorVersion==2){
			deleteArgs.insert(deleteArgs.begin()+1,"--purge");
			deleteArgs.push_back("--tiller-namespace");
			deleteArgs.push_back(cluster.systemNamespace);
		}
		else{
			deleteArgs.push_back("--namespace");
			deleteArgs.push_back(group.namespaceName());
		}

		span->AddEvent("helm cleanup delete");
		setInternalSpanAttributes(attributes);
		options = getInternalSpanOptions();
		auto helmDeleteSpan = tracer->StartSpan("helm cleanup delete", attributes, options);
		auto helmDeleteScope = tracer->WithActiveSpan(helmDeleteSpan);
		runCommand("helm",deleteArgs,{{"KUBECONFIG",*clusterConfig}});
		helmDeleteSpan->End();
		//TODO: include any other error information?
		return crow::response(500,generateError(errMsg));
	}
	helmSpan->End();
	
	log_info("Installed " << instance << " of " << appName
	         << " to " << cluster << " on behalf of " << user);

	//TODO: figure out what this was for and whether it can be salvaged
	/*std::vector<std::string> listArgs={"list",instance.name};
	if(helmMajorVersion==2)
		listArgs.push_back("--tiller-namespace");
	else if(helmMajorVersion==3)
		listArgs.push_back("--namespace");
	listArgs.push_back(cluster.systemNamespace);
	auto listResult = runCommand("helm",listArgs,{{"KUBECONFIG",*clusterConfig}});
	if(listResult.status){
		log_error("helm list " << instance.name << " failed: [exit] " << listResult.status << " [err] " << listResult.error << " [out] " << listResult.output);
		return crow::response(500,generateError("Failed to query helm for instance information"));
	}
	auto lines = string_split_lines(listResult.output);*/

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha3", alloc);
	result.AddMember("kind", "Configuration", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", instance.id, alloc);
	metadata.AddMember("name", instance.name, alloc);
	/*if(lines.size()>1){
		auto cols = string_split_columns(lines[1], '\t');
		if(cols.size()>3){
			metadata.AddMember("revision", cols[1], alloc);
			metadata.AddMember("updated", cols[2], alloc);
		}
	}*/
	if(!metadata.HasMember("revision")){
		metadata.AddMember("revision", "?", alloc);
		metadata.AddMember("updated", "?", alloc);
	}
	metadata.AddMember("application", appName, alloc);
	metadata.AddMember("group", group.id, alloc);
	result.AddMember("metadata", metadata, alloc);
	result.AddMember("status", "DEPLOYED", alloc);

	span->End();
	return crow::response(to_string(result));
}

crow::response installApplication(PersistentStore& store, const crow::request& req, const std::string& appName){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
;
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user=authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	if(!user) {
		const std::string& err = "An unauthorized user attempted to install an instance of " + appName;
		setWebSpanError(span, err, 403);
		span->End();
		log_info("An unauthorized user attempted to install an instance of " << appName << " from " << req.remote_endpoint);
		return crow::response(403, generateError(err));
	}
	
	auto repo=selectRepo(req);
	std::string repoName=getRepoName(repo);

	if(appName.find('\'')!=std::string::npos) {
		const std::string& err = "Application names cannot contain single quote characters";
		setWebSpanError(span, err, 400);
		span->End();
		return crow::response(400, generateError(err));
	}
	//collect data out of JSON body
	rapidjson::Document body;
	try{
		body.Parse(req.body.c_str());
	}catch(std::runtime_error& err){
		const std::string& errMsg = std::string("Invalid JSON in request body: ").append(err.what()) ;
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}
	if(body.IsNull()) {
		const std::string& errMsg = "Invalid JSON in request body";
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(400, generateError(errMsg));
	}

	std::string chartVersion= "";
	if(body.HasMember("chartVersion") && body["chartVersion"].IsString())
		chartVersion = body["chartVersion"].GetString();
	
	Application application;
	try{
		application=store.findApplication(repoName, appName, chartVersion);
	}
	catch(std::runtime_error& err){
		const std::string& errMsg = std::string("Runtime error: ").append(err.what());
		setWebSpanError(span, errMsg, 400);
		span->End();
		return crow::response(500);
	}
	if(!application)
		return crow::response(404,generateError("Application not found"));
	log_info(user << " requested to install an instance of " << application << " from " << req.remote_endpoint);
	log_info("Installsrc will be " << (repoName + "/" + appName));
	return installApplicationImpl(store, user, appName, repoName + "/" + appName, body);
}

//return a pair consisting of either true and the chart's/application's name
//or false and the error message from helm
std::pair<bool,std::string> extractChartName(const std::string& path){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setInternalSpanAttributes(attributes);
	auto options = getInternalSpanOptions();
	auto span = tracer->StartSpan("extractChartName", attributes, options);

	span->AddEvent("helm inspect chart");
	auto result=kubernetes::helm("","",{"inspect","chart",path});
	if(result.status==0){
		//try to parse the output as YAML
		try{
			YAML::Node node = YAML::Load(result.output);
			if(!node.IsMap() || !node["name"] || !node["name"].IsScalar())
				throw YAML::ParserException(YAML::Mark(),"Unexpected document structure");
			span->End();
			return std::make_pair(true,node["name"].as<std::string>());
		}catch(const YAML::ParserException& ex){
			setSpanError(span, std::string("Yaml parse error: ").append(ex.what()));
			span->End();
			return std::make_pair(false,"Did not get valid YAML chart description");
		}
	}
	else {
		span->End();
		return std::make_pair(false, result.error);
	}
}

crow::response installAdHocApplication(PersistentStore& store, const crow::request& req){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
;
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user=authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);

	log_info(user << " requested to install an instance of an ad-hoc application from " << req.remote_endpoint);
	if(!user) {
		const std::string& err = "An unauthorized user attempted to install an ad-hoc application";
		setWebSpanError(span, err, 403);
		span->End();
		log_info(err);
		return crow::response(403, generateError(err));
	}
	
	//collect data out of JSON body
	rapidjson::Document body;
	try{
		body.Parse(req.body.c_str());
	}catch(std::runtime_error& err){
		const std::string& errMsg = std::string("Invalid JSON in request body: ").append(err.what());
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400,generateError(errMsg));
	}
	if(body.IsNull()) {
		const std::string& errMsg = "Invalid JSON in request body";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400,generateError(errMsg));
	}
		
	if(!body.HasMember("chart")) {
		const std::string& errMsg = "Missing chart";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	if(!body["chart"].IsString()) {
		const std::string& errMsg = "Incorrect type for chart";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	
	FileHandle chartDir;
	std::string appName;
	struct DirCleaner{
		FileHandle& dir;
		~DirCleaner(){
			if(!dir.path().empty())
				recursivelyDestroyDirectory(dir);
		}
	} dirCleaner{chartDir};
	try{
		chartDir=makeTemporaryDir("/tmp/slate_chart_");
		std::stringstream gzipStream(decodeBase64(body["chart"].GetString())), tarStream;
		gzipDecompress(gzipStream,tarStream);
		TarReader tr(tarStream);
		tr.extractToFileSystem(chartDir+"/");
		log_info("Extracted chart to " << chartDir.path());
	}catch(std::exception& ex){
		const std::string& errMsg = std::string("Unable to extract application chart: ").append(ex.what());
		setWebSpanError(span, errMsg, 500);
		span->End();
		log_error(errMsg);
		return crow::response(500, generateError(errMsg));
	}
	
	directory_iterator dit(chartDir);
	bool foundSubDir=false;
	std::string chartSubDir;
	for(const directory_iterator end; dit!=end; dit++){
		if(dit->path().name()=="." || dit->path().name()=="..")
			continue;
		if(!is_directory(*dit))
			continue;
		if(!foundSubDir){
			chartSubDir=dit->path().str();
			foundSubDir=true;
		}
		else {
			const std::string& errMsg = "Too many directories in chart tarball";
			setWebSpanError(span, errMsg, 400);
			span->End();
			log_error(errMsg);
			return crow::response(400, generateError(errMsg));
		}
	}
	if(!foundSubDir) {
		const std::string& errMsg = "No directory in chart tarball";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	
	auto nameInfo=extractChartName(chartSubDir);
	const directory_iterator templates(chartSubDir + "/templates");
	const directory_iterator tempNotFound;
	if (templates == tempNotFound) {
		const std::string& errMsg = "Templates subdirectory not found in chart tarball";
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError("Templates subdirectory not found in chart tarball"));
	}
	if(!nameInfo.first) {
		const std::string& errMsg = nameInfo.second;
		setWebSpanError(span, errMsg, 400);
		span->End();
		log_error(errMsg);
		return crow::response(400, generateError(errMsg));
	}
	appName=nameInfo.second;
	span->End();
	return installApplicationImpl(store, user, appName, chartSubDir, body);

	//return crow::response(500,generateError("Ad-hoc application installation is not implemented"));
}

crow::response updateCatalog(PersistentStore& store, const crow::request& req){
	auto tracer = getTracer();
	std::map<std::string, std::string> attributes;
	setWebSpanAttributes(attributes, req);
	auto options = getWebSpanOptions(req);
	auto span = tracer->StartSpan(req.url, attributes, options);
;
	populateSpan(span, req);
	auto scope = tracer->WithActiveSpan(span);
	//authenticate
	const User user=authenticateUser(store, req.url_params.get("token"));
	span->SetAttribute("user", user.name);
	log_info(user << " requested to update the application catalog from " << req.remote_endpoint);
	if(!user) {
		const std::string& err = "An unauthorized user attempted to update the application catalog";
		setWebSpanError(span, err, 403);
		span->End();
		log_info(err);
		return crow::response(403, generateError(err));
	}
	
	auto result = runCommand("helm",{"repo","update"});
	if(result.status){
		std::ostringstream msg;
		msg << "helm repo update failed: [exit] " << result.status << " [err] " << result.error << " [out] " << result.output;
		setWebSpanError(span, msg.str(), 500);
		span->End();
		log_info(msg.str());
		return crow::response(500,generateError("helm repo update failed"));
	}
	
	store.fetchApplications("slate");
	store.fetchApplications("slate-dev");

	span->End();
	return crow::response(200);
}
