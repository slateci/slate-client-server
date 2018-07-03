#include <iostream>

#include <crow.h>

#include "Entities.h"
#include "PersistentStore.h"

#include "ApplicationCommands.h"
#include "ApplicationInstanceCommands.h"
#include "ClusterCommands.h"
#include "UserCommands.h"
#include "VOCommands.h"

int main(int argc, char* argv[]){
	// DB client initialization
	Aws::SDKOptions options;
	Aws::InitAPI(options);
	using AWSOptionsHandle=std::unique_ptr<Aws::SDKOptions,void(*)(Aws::SDKOptions*)>;
	AWSOptionsHandle opt_holder(&options,
								[](Aws::SDKOptions* options){
									Aws::ShutdownAPI(*options); 
								});
	//TODO: credentials should be read from a configuration file or environment variable
	Aws::Auth::AWSCredentials credentials("foo","bar");
	Aws::Client::ClientConfiguration clientConfig;
	clientConfig.region="us-east-1";
	clientConfig.scheme=Aws::Http::Scheme::HTTP;
	//TODO: should provide some convenient way to set this
	clientConfig.endpointOverride="localhost:8000";
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
	
	server.loglevel(crow::LogLevel::Warning);
    server.port(18080).run();
}
