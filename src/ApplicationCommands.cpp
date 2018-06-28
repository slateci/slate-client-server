#include "ApplicationCommands.h"

#include "Logging.h"
#include "Utilities.h"

crow::response listApplications(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list applications");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list applications
	
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	//TODO: compose actual result list
	result["items"]=std::vector<std::string>{};
	return crow::response(result);
}

crow::response fetchApplicationConfig(PersistentStore& store, const crow::request& req, const std::string& appName){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to fetch configuration for application " << appName);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//TODO: Can all users obtain configurations for all applications?
	
	//TODO: implement this
	
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	result["kind"]="Configuration";
	crow::json::wvalue metadata;
	//metadata["name"]=appName;
	result["metadata"]=std::move(metadata);
	crow::json::wvalue spec;
	//spec["body"]=appName;
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
	
	//TODO: instantiate the application using helm
	//if application instantiation fails, remove record from DB again
	if(!success){
		store.removeApplicationInstance(instance.id);
		//TODO: include any other error information?
		return crow::response(500,generateError("Failed to start application instance"
												" with helm"));
	}
	
	return crow::response(crow::json::wvalue());
}
