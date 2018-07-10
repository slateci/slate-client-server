#include <iostream>

#include <crow.h>

#include "Entities.h"
#include "Logging.h"
#include "PersistentStore.h"
#include "Utilities.h"

#include "ApplicationCommands.h"
#include "ApplicationInstanceCommands.h"
#include "ClusterCommands.h"
#include "UserCommands.h"
#include "VOCommands.h"

int main(int argc, char* argv[]){
	std::string awsAccessKey="foo";
	std::string awsSecretKey="bar";
	std::string awsRegion="us-east-1";
	std::string awsURLScheme="http";
	std::string awsEndpoint="localhost:8000";
	std::string portString="18080";
	
	//check for environment variables
	fetchFromEnvironment("SLATE_awsAccessKey",awsAccessKey);
	fetchFromEnvironment("SLATE_awsSecretKey",awsSecretKey);
	fetchFromEnvironment("SLATE_awsRegion",awsRegion);
	fetchFromEnvironment("SLATE_awsURLScheme",awsURLScheme);
	fetchFromEnvironment("SLATE_awsEndpoint",awsEndpoint);
	fetchFromEnvironment("SLATE_PORT",portString);
	
	//interpret command line arguments
	for(int i=1; i<argc; i++){
		std::string arg(argv[i]);
		if(arg=="--awsAccessKey"){
			if(i==argc-1)
				log_fatal("Missing value after --awsAccessKey");
			i++;
			awsAccessKey=argv[i];
		}
		else if(arg=="--awsSecretKey"){
			if(i==argc-1)
				log_fatal("Missing value after --awsSecretKey");
			i++;
			awsSecretKey=argv[i];
		}
		else if(arg=="--awsRegion"){
			if(i==argc-1)
				log_fatal("Missing value after --awsRegion");
			i++;
			awsRegion=argv[i];
		}
		else if(arg=="--awsURLScheme"){
			if(i==argc-1)
				log_fatal("Missing value after --awsURLScheme");
			i++;
			awsURLScheme=argv[i];
		}
		else if(arg=="--awsEndpoint"){
			if(i==argc-1)
				log_fatal("Missing value after --awsEndpoint");
			i++;
			awsEndpoint=argv[i];
		}
		else if(arg=="--port"){
			if(i==argc-1)
				log_fatal("Missing value after --port");
			i++;
			portString=argv[i];
		}
		else{
			log_error("Unknown argument ignored: '" << arg << '\'');
		}
	}
	
	log_info("Database URL is " << awsURLScheme << "://" << awsEndpoint);
	unsigned int port=0;
	{
		std::istringstream is(portString);
		is >> port;
		if(!port || is.fail())
			log_fatal("Unable to parse \"" << portString << "\" as a valid port number");
	}
	log_info("Service port is " << port);
	
	// DB client initialization
	Aws::SDKOptions options;
	Aws::InitAPI(options);
	using AWSOptionsHandle=std::unique_ptr<Aws::SDKOptions,void(*)(Aws::SDKOptions*)>;
	AWSOptionsHandle opt_holder(&options,
								[](Aws::SDKOptions* options){
									Aws::ShutdownAPI(*options); 
								});
	Aws::Auth::AWSCredentials credentials(awsAccessKey,awsSecretKey);
	Aws::Client::ClientConfiguration clientConfig;
	clientConfig.region=awsRegion;
	if(awsURLScheme=="http")
		clientConfig.scheme=Aws::Http::Scheme::HTTP;
	else if(awsURLScheme=="https")
		clientConfig.scheme=Aws::Http::Scheme::HTTPS;
	else
		log_fatal("Unrecognized URL scheme for AWS: '" << awsURLScheme << '\'');
	clientConfig.endpointOverride=awsEndpoint;
	PersistentStore store(credentials,clientConfig);
	
	// REST server initialization
	crow::SimpleApp server;
	
	// == User commands ==
	CROW_ROUTE(server, "/v1alpha1/users").methods("GET"_method)(
	  [&](const crow::request& req){ return listUsers(store,req); }); //√
	CROW_ROUTE(server, "/v1alpha1/users").methods("POST"_method)(
	  [&](const crow::request& req){ return createUser(store,req); }); //√
	CROW_ROUTE(server, "/v1alpha1/users/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& uID){ return getUserInfo(store,req,uID); }); //-
	CROW_ROUTE(server, "/v1alpha1/users/<string>").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& uID){ return updateUser(store,req,uID); }); //√
	CROW_ROUTE(server, "/v1alpha1/users/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& uID){ return deleteUser(store,req,uID); }); //√
	CROW_ROUTE(server, "/v1alpha1/users/<string>/vos").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& uID){ return listUserVOs(store,req,uID); }); //-
	CROW_ROUTE(server, "/v1alpha1/users/<string>/vos/<string>").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& uID, const std::string voID){ return addUserToVO(store,req,uID,voID); }); //-
	CROW_ROUTE(server, "/v1alpha1/users/<string>/vos/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& uID, const std::string voID){ return removeUserFromVO(store,req,uID,voID); }); //-
	CROW_ROUTE(server, "/v1alpha1/find_user").methods("GET"_method)(
	  [&](const crow::request& req){ return findUser(store,req); }); //√
	
	// == Cluster commands ==
	CROW_ROUTE(server, "/v1alpha1/clusters").methods("GET"_method)(
	  [&](const crow::request& req){ return listClusters(store,req); });
	CROW_ROUTE(server, "/v1alpha1/clusters").methods("POST"_method)(
	  [&](const crow::request& req){ return createCluster(store,req); });
	CROW_ROUTE(server, "/v1alpha1/clusters/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& clID){ return deleteCluster(store,req,clID); });
	CROW_ROUTE(server, "/v1alpha1/clusters/<string>").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& clID){ return updateCluster(store,req,clID); });
	
	// == VO commands ==
	CROW_ROUTE(server, "/v1alpha1/vos").methods("GET"_method)(
	  [&](const crow::request& req){ return listVOs(store,req); }); //√
	CROW_ROUTE(server, "/v1alpha1/vos").methods("POST"_method)(
	  [&](const crow::request& req){ return createVO(store,req); }); //-
	CROW_ROUTE(server, "/v1alpha1/vos/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& voID){ return deleteVO(store,req,voID); }); //-
	
	// == Application commands ==
	CROW_ROUTE(server, "/v1alpha1/apps").methods("GET"_method)(
	  [&](const crow::request& req){ return listApplications(store,req); });
	CROW_ROUTE(server, "/v1alpha1/apps/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& aID){ return fetchApplicationConfig(store,req,aID); });
	CROW_ROUTE(server, "/v1alpha1/apps/<string>").methods("POST"_method)(
	  [&](const crow::request& req, const std::string& aID){ return installApplication(store,req,aID); });
	
	// == Application Instance commands ==
	CROW_ROUTE(server, "/v1alpha1/instances").methods("GET"_method)(
	  [&](const crow::request& req){ return listApplicationInstances(store,req); });
	CROW_ROUTE(server, "/v1alpha1/instances/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& iID){ return fetchApplicationInstanceInfo(store,req,iID); });
	CROW_ROUTE(server, "/v1alpha1/instances/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& iID){ return deleteApplicationInstance(store,req,iID); });
	
	CROW_ROUTE(server, "/v1alpha1/stats").methods("GET"_method)(
	  [&](){ return(store.getStatistics()); });
	
	server.loglevel(crow::LogLevel::Warning);
	server.port(port).multithreaded().run();
}
