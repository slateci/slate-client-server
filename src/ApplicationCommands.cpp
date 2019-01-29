#include "ApplicationCommands.h"

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
#include "Archive.h"
#include "FileSystem.h"
#include "ServerUtilities.h"

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
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list applications");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list applications

	std::string repoName=getRepoName(selectRepo(req));
	
	auto commandResult=runCommand("helm", {"search",repoName+"/"});
	if(commandResult.status){
		log_error("helm search failed: " << commandResult.error);
		return crow::response(500,generateError("helm search failed"));
	}
	std::vector<std::string> lines = string_split_lines(commandResult.output);

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);

	rapidjson::Value resultItems(rapidjson::kArrayType);
        int n = 0;
	while (n < lines.size()) {
		if (n > 0) {
			auto tokens = string_split_columns(lines[n], '\t');
	  	    
			rapidjson::Value applicationResult(rapidjson::kObjectType);
			applicationResult.AddMember("apiVersion", "v1alpha1", alloc);
			applicationResult.AddMember("kind", "Application", alloc);
			rapidjson::Value applicationData(rapidjson::kObjectType);

			rapidjson::Value name;
			//strip the leading repository name and slash from the chart name
			name.SetString(tokens[0].substr(repoName.size()+1), alloc);
			applicationData.AddMember("name", name, alloc);
			rapidjson::Value app_version;
			app_version.SetString(tokens[2], alloc);
			applicationData.AddMember("app_version", app_version, alloc);
			rapidjson::Value chart_version;
			chart_version.SetString(tokens[1], alloc);
			applicationData.AddMember("chart_version", chart_version, alloc);
			rapidjson::Value description;
			description.SetString(tokens[3], alloc);
			applicationData.AddMember("description", description, alloc);
	    
			applicationResult.AddMember("metadata", applicationData, alloc);
			resultItems.PushBack(applicationResult, alloc);
		}
		n++;
	}

	result.AddMember("items", resultItems, alloc);

	return crow::response(to_string(result));
}

Application findApplication(std::string appName, Application::Repository repo){
	std::string repoName=getRepoName(repo);
	std::string target=repoName+"/"+appName;
	auto result=runCommand("helm", {"search",target});
	if(result.status){
		log_error("Command failed: helm search " << target << ": " << result.error);
		return Application();
	}
	
	if(result.output.find("No results found")!=std::string::npos)
		return Application();
	
	//Deal with the possibility of multiple results, which could happen if
	//both "slate/stuff" and "slate/superduper" existed and the user requested
	//the application "s". Multiple results might also not indicate ambiguity, 
	//if the user searches for the full name of an application, which is also a
	//prefix of the name another application which exists
	std::vector<std::string> lines = string_split_lines(result.output);
	//ignore initial header line printed by helm
	for(size_t i=1; i<lines.size(); i++){
		auto tokens=string_split_columns(lines[i], '\t');
		if(trim(tokens.front())==target)
			return Application(appName);
	}
	
	return Application();
}

crow::response fetchApplicationConfig(PersistentStore& store, const crow::request& req, const std::string& appName){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to fetch configuration for application " << appName);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//TODO: Can all users obtain configurations for all applications?

	auto repo=selectRepo(req);
	std::string repoName=getRepoName(repo);
		
	const Application application=findApplication(appName,repo);
	if(!application)
		return crow::response(404,generateError("Application not found"));
	
	auto commandResult = runCommand("helm",{"inspect","values",repoName + "/" + appName});
	if(commandResult.status){
		log_error("Command failed: helm inspect " << (repoName + "/" + appName) << ": " << commandResult.error);
		return crow::response(500, generateError("Unable to fetch application config"));
	}

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	result.AddMember("kind", "Configuration", alloc);

	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", appName, alloc);
	result.AddMember("metadata", metadata, alloc);

	rapidjson::Value spec(rapidjson::kObjectType);
	spec.AddMember("body", filterValuesFile(commandResult.output), alloc);
	result.AddMember("spec", spec, alloc);

	return crow::response(to_string(result));
}

///Internal function which requires that initial authorization checks have already been performed
crow::response installApplicationImpl(PersistentStore& store, const User& user, const std::string& appName, const std::string& installSrc, const rapidjson::Document& body){
	if(!body.HasMember("vo"))
		return crow::response(400,generateError("Missing VO"));
	if(!body["vo"].IsString())
		return crow::response(400,generateError("Incorrect type for VO"));
	const std::string voID=body["vo"].GetString();
	
	if(!body.HasMember("cluster"))
		return crow::response(400,generateError("Missing cluster"));
	if(!body["cluster"].IsString())
		return crow::response(400,generateError("Incorrect type for cluster"));
	const std::string clusterID=body["cluster"].GetString();
	
	if(!body.HasMember("configuration"))
		return crow::response(400,generateError("Missing configuration"));
	if(!body["configuration"].IsString())
		return crow::response(400,generateError("Incorrect type for configuration"));
	const std::string config=body["configuration"].GetString();

	std::string tag; //start by assuming this is empty
	bool gotTag=false;
	
	//returns true if YAML parsing was successful
	auto extractInstanceTag=[&tag,&gotTag](const std::string& config)->bool{
		std::vector<YAML::Node> parsedConfig;
		try{
			parsedConfig=YAML::LoadAll(config);
		}catch(const YAML::ParserException& ex){
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
		if(!extractInstanceTag(config))
			return crow::response(400,generateError("Configuration could not be parsed as YAML"));
	}
	//if the user did not specify a tag we must parse the base helm chart to 
	//find out what the default value is
	if(!gotTag){
		auto commandResult = runCommand("helm",{"inspect","values",installSrc});
		if(commandResult.status){
			log_error("Command failed: helm inspect values " << installSrc << ": " << commandResult.error);
			return crow::response(500, generateError("Unable to fetch default application config"));
		}
		if(!extractInstanceTag(commandResult.output))
			return crow::response(500,generateError("Default configuration could not be parsed as YAML"));
	}
	if(!gotTag){
		log_error("Failed to determine instance tag for " << appName);
		return crow::response(500, generateError("Failed to determine instance tag for "+appName));
	}
	
	//Direct specification of instance tags is forbidden
	/*//if a tag was manually specified, let it override
	if(body.HasMember("tag")){
		if(!body["tag"].IsString())
			return crow::response(400,generateError("Incorrect type for tag"));
		tag=body["tag"].GetString();
	}*/
	
	if(tag.find_first_not_of("abcdefghijklmnopqrstuvwxzy0123456789-")!=std::string::npos)
		return crow::response(400,generateError("Instance tags names may only contain [a-z], [0-9] and -"));
	if(!tag.empty() && tag.back()=='-')
		return crow::response(400,generateError("Instance tags names may not end with a dash"));
	
	//validate input
	const VO vo=store.getVO(voID);
	if(!vo)
		return crow::response(400,generateError("Invalid VO"));
	const Cluster cluster=store.getCluster(clusterID);
	if(!cluster)
		return crow::response(400,generateError("Invalid Cluster"));
	//A user must belong to a VO to install applications on its behalf
	if(!store.userInVO(user.id,vo.id))
		return crow::response(403,generateError("Not authorized"));
	//The VO must own or be allowed to access to the cluster to install
	//applications to it. If the VO is not the cluster owner it must also have 
	//permission to install the specific application. 
	log_info(cluster << " is owned by " << cluster.owningVO << ", install request is from " << vo);
	if(vo.id!=cluster.owningVO){
		if(!store.voAllowedOnCluster(vo.id,cluster.id))
			return crow::response(403,generateError("Not authorized"));
		if(!store.voMayUseApplication(vo.id, cluster.id, appName))
			return crow::response(403,generateError("Not authorized"));
	}

	ApplicationInstance instance;
	instance.valid=true;
	instance.id=idGenerator.generateInstanceID();
	instance.application=appName;
	instance.owningVO=vo.id;
	instance.cluster=cluster.id;
	//TODO: strip comments and whitespace from config
	instance.config=reduceYAML(config);
	if(instance.config.empty())
		instance.config="\n"; //empty strings upset Dynamo
	instance.ctime=timestamp();
	instance.name=vo.name+"-"+appName;
	if(!tag.empty())
		instance.name+="-"+tag;
	if(instance.name.size()>63)
		return crow::response(400,generateError("Instance tag too long"));
	
	//find all instances with the same name
	auto nameMatches=store.findInstancesByName(instance.name);
	//Names include VO and application, but do not include cluster. Check 
	//whether any instance with a matching name is already on the target cluster
	for(const auto& otherInst : nameMatches){
		if(otherInst.cluster==cluster.id)
			return crow::response(400,generateError("Instance name is already in use,"
			                                        " consider using a different tag"));
	}
	
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
	
	log_info("Instantiating " << appName << " on " << cluster);
	//first record the instance in the peristent store
	bool success=store.addApplicationInstance(instance);
	
	if(!success){
		return crow::response(500,generateError("Failed to add application instance"
		                                        " record to the persistent store"));
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
		kubernetes::kubectl_create_namespace(*clusterConfig, vo);
	}
	catch(std::runtime_error& err){
		store.removeApplicationInstance(instance.id);
		return crow::response(500,generateError(err.what()));
	}
	
	auto commandResult=runCommand("helm",
	  {"install",installSrc,"--name",instance.name,
	   "--namespace",vo.namespaceName(),"--values",instanceConfig.path(),
	   "--set",additionalValues,
	   "--tiller-namespace",cluster.systemNamespace},
	  {{"KUBECONFIG",*clusterConfig}});
	
	//if application instantiation fails, remove record from DB again
	if(commandResult.status || 
	   commandResult.output.find("STATUS: DEPLOYED")==std::string::npos){
		std::string errMsg="Failed to start application instance with helm:\n"+commandResult.error+"\n system namespace: "+cluster.systemNamespace;
		log_error(errMsg);
		store.removeApplicationInstance(instance.id);
		//helm will (unhelpfully) keep broken 'releases' around, so clean up here
		runCommand("helm",
		  {"delete","--purge",instance.name,"--tiller-namespace",cluster.systemNamespace},
		  {{"KUBECONFIG",*clusterConfig}});
		//TODO: include any other error information?
		return crow::response(500,generateError(errMsg));
	}
	
	log_info("Installed " << instance << " of " << appName
	         << " to " << cluster << " on behalf of " << user);

	auto listResult = runCommand("helm",
	  {"list",instance.name,"--tiller-namespace",cluster.systemNamespace},
	  {{"KUBECONFIG",*clusterConfig}});
	if(listResult.status){
		log_error("helm list " << instance.name << " failed: " << listResult.error);
		return crow::response(500,generateError("Failed to query helm for instance information"));
	}
	auto lines = string_split_lines(listResult.output);

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	result.AddMember("kind", "Configuration", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", instance.id, alloc);
	metadata.AddMember("name", instance.name, alloc);
	if(lines.size()>1){
		auto cols = string_split_columns(lines[1], '\t');
		if(cols.size()>3){
			metadata.AddMember("revision", cols[1], alloc);
			metadata.AddMember("updated", cols[2], alloc);
		}
	}
	if(!metadata.HasMember("revision")){
		metadata.AddMember("revision", "?", alloc);
		metadata.AddMember("updated", "?", alloc);
	}
	metadata.AddMember("application", appName, alloc);
	metadata.AddMember("vo", vo.id, alloc);
	result.AddMember("metadata", metadata, alloc);
	result.AddMember("status", "DEPLOYED", alloc);

	return crow::response(to_string(result));
}

crow::response installApplication(PersistentStore& store, const crow::request& req, const std::string& appName){
	if(appName.find('\'')!=std::string::npos)
		return crow::response(400,generateError("Application names cannot contain single quote characters"));
	
	auto repo=selectRepo(req);
	const Application application=findApplication(appName,repo);
	if(!application)
		return crow::response(404,generateError("Application not found"));
	
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to install an instance of " << application);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//collect data out of JSON body
	rapidjson::Document body;
	try{
		body.Parse(req.body.c_str());
	}catch(std::runtime_error& err){
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	if(body.IsNull())
		return crow::response(400,generateError("Invalid JSON in request body"));
		
	std::string repoName = getRepoName(repo);
	
	return installApplicationImpl(store, user, appName, repoName + "/" + appName, body);
}

//return a pair consisting of either true and the chart's/application's name
//or false and the error message from helm
std::pair<bool,std::string> extractChartName(const std::string& path){
	auto result=kubernetes::helm("","",{"inspect","chart",path});
	if(result.status==0){
		//try to parse the output as YAML
		try{
			YAML::Node node = YAML::Load(result.output);
			if(!node.IsMap() || !node["name"] || !node["name"].IsScalar())
				throw YAML::ParserException(YAML::Mark(),"Unexpected document structure");
			return std::make_pair(true,node["name"].as<std::string>());
		}catch(const YAML::ParserException& ex){
			return std::make_pair(false,"Did not get valid YAML chart description");
		}
	}
	else
		return std::make_pair(false,result.error);
}

crow::response installAdHocApplication(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to install an instance of an ad-hoc application");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//collect data out of JSON body
	rapidjson::Document body;
	try{
		body.Parse(req.body.c_str());
	}catch(std::runtime_error& err){
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	if(body.IsNull())
		return crow::response(400,generateError("Invalid JSON in request body"));
		
	if(!body.HasMember("chart"))
		return crow::response(400,generateError("Missing chart"));
	if(!body["chart"].IsString())
		return crow::response(400,generateError("Incorrect type for chart"));
	
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
		log_error("Unable to extract application chart: " << ex.what());
		return crow::response(500,generateError("Failed to extract application chart"));
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
		else
			return crow::response(400,generateError("Too many directories in chart tarball"));
	}
	if(!foundSubDir)
		return crow::response(400,generateError("No directory in chart tarball"));
	
	auto nameInfo=extractChartName(chartSubDir);
	if(!nameInfo.first)
		return crow::response(400,generateError(nameInfo.second));
	appName=nameInfo.second;
	
	return installApplicationImpl(store, user, appName, chartSubDir, body);

	//return crow::response(500,generateError("Ad-hoc application installation is not implemented"));
}

crow::response updateCatalog(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to update the application catalog");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	auto result = runCommand("helm",{"repo","update"});
	if(result.status){
		log_error("helm repo update failed: " << result.error);
		return crow::response(500,generateError("helm repo update failed"));
	}
	return crow::response(200);
}
