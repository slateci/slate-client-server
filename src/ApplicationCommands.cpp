#include "ApplicationCommands.h"

#include "Logging.h"
#include "Utilities.h"

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

crow::response listApplications(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list applications");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list applications

	std::string repoName=getRepoName(selectRepo(req));
	
	auto commandResult=runCommand("helm search "+repoName+"/");
	if(commandResult.status){
		log_error("helm search failed with status " << commandResult.status);
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
	auto command="helm search "+target;
	auto result=runCommand(command);
	if(result.status){
		log_error("Command failed: helm search " << target << ": " << result.status);
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
	
	auto commandResult = runCommand("helm inspect " + repoName + "/" + appName);
	if(commandResult.status){
		log_error("Command failed: helm inspect " << (repoName + "/" + appName) << ": " << commandResult.status);
		return crow::response(500, generateError("Unable to fetch application config"));
	}

	if (commandResult.output.find("Error") != std::string::npos)
		return crow::response(404, generateError("Application not found"));

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	result.AddMember("kind", "Configuration", alloc);

	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", appName, alloc);
	result.AddMember("metadata", metadata, alloc);

	rapidjson::Value spec(rapidjson::kObjectType);
	spec.AddMember("body", commandResult.output, alloc);
	result.AddMember("spec", spec, alloc);

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
	
	if(!body.HasMember("tag"))
		return crow::response(400,generateError("Missing tag"));
	if(!body["tag"].IsString())
		return crow::response(400,generateError("Incorrect type for tag"));
	const std::string tag=body["tag"].GetString();
	if(tag.find_first_not_of("abcdefghijklmnopqrstuvwxzy0123456789-")!=std::string::npos)
		return crow::response(400,generateError("Instance tags names may only contain [a-z], [0-9] and -"));
	if(!tag.empty() && tag.back()=='-')
		return crow::response(400,generateError("Instance tags names may not end with a dash"));
	
	if(!body.HasMember("configuration"))
		return crow::response(400,generateError("Missing configuration"));
	if(!body["configuration"].IsString())
		return crow::response(400,generateError("Incorrect type for configuration"));
	const std::string config=body["configuration"].GetString();
	
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
	
	if(!store.findInstancesByName(instance.name).empty()){
		return crow::response(400,generateError("Instance name is already in use,"
		                                        " consider using a different tag"));
	}
	
	log_info("Instantiating " << application  << " on " << cluster);
	//first record the instance in the peristent store
	bool success=store.addApplicationInstance(instance);
	
	if(!success){
		return crow::response(500,generateError("Failed to add application instance"
		                                        " record the persistent store"));
	}

	std::string repoName = getRepoName(repo);

	auto configPath=store.configPathForCluster(cluster.id);
	auto commandResult = runCommand("export KUBECONFIG='"+*configPath+
	                           "'; helm install " + repoName + "/" + 
	                           application.name + " --name " + instance.name + 
							   " --namespace " + vo.namespaceName());
	
	//if application instantiation fails, remove record from DB again
	if(commandResult.status || 
	   commandResult.output.find("STATUS: DEPLOYED")==std::string::npos){
		std::string errMsg="Failed to start application instance with helm";
		log_error(errMsg << ":\n" << commandResult.output);
		//try to figure out what helm is unhappy about to tell the user
		for(auto line : string_split_lines(commandResult.output)){
			if(line.find("Error")){
				errMsg+=": "+line;
				break;
			}
		}
		store.removeApplicationInstance(instance.id);
		//TODO: include any other error information?
		return crow::response(500,generateError(errMsg));
	}

	auto listResult = runCommand("export KUBECONFIG='"+*configPath+"'; helm list " + instance.name);
	if(listResult.status){
		log_error("helm list " << instance.name << " failed: " << listResult.status);
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
