#include "ApplicationCommands.h"

#include "Logging.h"
#include "Utilities.h"

crow::response listApplications(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list applications");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list applications

	std::string repoName="slate";
	if(req.url_params.get("dev"))
		repoName="slate-dev";
	
	auto commandResult=runCommand("helm search "+repoName+"/");
	std::vector<std::string> lines = string_split_lines(commandResult);

	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	std::vector<crow::json::wvalue> resultItems;
	resultItems.reserve(lines.size() - 1);
	
	int n = 0;
	while (n < lines.size()) {
	  if (n > 0) {
	    auto tokens = string_split_columns(lines[n], '\t');
	    
	    crow::json::wvalue applicationResult;
	    applicationResult["apiVersion"] = "v1alpha1";
	    applicationResult["kind"] = "Application";
	    crow::json::wvalue applicationData;
	    applicationData["name"] = tokens[0];
	    applicationData["app_version"] = tokens[2];
	    applicationData["chart_version"] = tokens[1];
	    applicationData["description"] = tokens[3];
	    applicationResult["metadata"] = std::move(applicationData);
	    resultItems.emplace_back(std::move(applicationResult));
	  }
	  n++;
	}

	result["items"] = std::move(resultItems);
	return crow::response(result);
}

crow::response fetchApplicationConfig(PersistentStore& store, const crow::request& req, const std::string& appName){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to fetch configuration for application " << appName);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//TODO: Can all users obtain configurations for all applications?

	std::string repoName = "slate";
	if(req.url_params.get("dev"))
	  repoName = "slate-dev";
		
	auto commandResult = runCommand("helm inspect " + repoName + "/" + appName);

	if (commandResult.find("Error") != std::string::npos)
	  return crow::response(404, generateError("Application not found"));
	
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	result["kind"]="Configuration";
	crow::json::wvalue metadata;
	metadata["name"]=appName;
	result["metadata"]=std::move(metadata);
	crow::json::wvalue spec;
	spec["body"]=commandResult;
	result["spec"]=std::move(spec);
	return crow::response(result);
}

Application findApplication(std::string appName, Application::Repository repo){
	std::string repoName="slate";
	if(repo==Application::DevelopmentRepository)
		repoName="slate-dev";
	auto command="helm search "+repoName+"/"+appName;
	auto result=runCommand(command);
	
	if(result.find("No results found")!=std::string::npos)
		return Application();
	
	//TODO: deal with the possibility of multiple results, which could happen if
	//both "slate/stuff" and "slate/superduper" existed and the user requested
	//the application "s". Multiple results might also not indicate ambiguity, 
	//if the user searches for the full name of an application, which is also a
	//prefix of the name another application which exists
	
	return Application(appName);
}

crow::response installApplication(PersistentStore& store, const crow::request& req, const std::string& appName){
	if(appName.find('\'')!=std::string::npos)
		return crow::response(400,generateError("Application names cannot contain single quote characters"));
	Application::Repository repo=Application::MainRepository;
	if(req.url_params.get("dev"))
		repo=Application::DevelopmentRepository;
	const Application application=findApplication(appName,repo);
	if(!application)
		return crow::response(404,generateError("Application not found"));
	
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to install an instance of " << application);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	//collect data out of JSON body
	crow::json::rvalue body;
	try{
		body = crow::json::load(req.body);
	}catch(std::runtime_error& err){
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	if(!body)
		return crow::response(400,generateError("Invalid JSON in request body"));
	
	if(!body.has("vo"))
		return crow::response(400,generateError("Missing VO"));
	if(body["vo"].t()!=crow::json::type::String)
		return crow::response(400,generateError("Incorrect type for VO"));
	const std::string voID=body["vo"].s();
	
	if(!body.has("cluster"))
		return crow::response(400,generateError("Missing cluster"));
	if(body["cluster"].t()!=crow::json::type::String)
		return crow::response(400,generateError("Incorrect type for cluster"));
	const std::string clusterID=body["cluster"].s();
	
	if(!body.has("tag"))
		return crow::response(400,generateError("Missing tag"));
	if(body["tag"].t()!=crow::json::type::String)
		return crow::response(400,generateError("Incorrect type for tag"));
	const std::string tag=body["tag"].s();
	
	if(!body.has("configuration"))
		return crow::response(400,generateError("Missing configuration"));
	if(body["configuration"].t()!=crow::json::type::String)
		return crow::response(400,generateError("Incorrect type for configuration"));
	const std::string config=body["configuration"].s();
	
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
	instance.name=appName+"-"+tag;
	if(instance.name.size()>63)
		return crow::response(400,generateError("Instance tag too long"));
	
	log_info("Instantiating " << application  << " on " << cluster);
	//first record the instance in the peristent store
	bool success=store.addApplicationInstance(instance);
	
	if(!success){
		return crow::response(500,generateError("Failed to add application instance"
												" record the persistent store"));
	}

	std::string repoName = "slate";
	if (repo == Application::DevelopmentRepository)
	  repoName = "slate-dev";

	auto configInfo=store.configPathForCluster(cluster.id);
	std::string commandResult;
	{
		log_info("Locking " << &configInfo.second << " to read " << configInfo.first);
		std::lock_guard<std::mutex> lock(configInfo.second);
		std::cout << "Command: " << (
		                           "export KUBECONFIG='"+configInfo.first+
		                           "'; helm install " + repoName + "/" + 
		                           application.name + " --name " + instance.name
		) << std::endl;
		commandResult = runCommand("export KUBECONFIG='"+configInfo.first+
		                           "'; helm install " + repoName + "/" + 
		                           application.name + " --name " + instance.name + 
								   " --namespace " + vo.namespaceName());
	}
	
	//if application instantiation fails, remove record from DB again
	if(commandResult.find("STATUS: DEPLOYED")==std::string::npos){
		store.removeApplicationInstance(instance.id);
		//TODO: include any other error information?
		return crow::response(500,generateError("Failed to start application instance"
												" with helm"));
	}

	auto listResult = runCommand("helm list " + instance.name);
	auto lines = string_split_lines(listResult);
	auto cols = string_split_columns(lines[1], '\t');
	
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	result["kind"]="Configuration";
	crow::json::wvalue metadata;
	metadata["id"]=instance.id;
	metadata["name"]=instance.name;
	metadata["revision"]=cols[1];
	metadata["updated"]=cols[2];
	metadata["application"]=appName;
	metadata["vo"]=vo.id;
	result["metadata"]=std::move(metadata);
	result["status"]="DEPLOYED";
	return crow::response(result);
}
