#include <cerrno>
#include <iostream>

#include <sys/stat.h>

#define CROW_ENABLE_SSL
#include <crow.h>

#include "Entities.h"
#include "Logging.h"
#include "PersistentStore.h"
#include "Process.h"
#include "Utilities.h"

#include "ApplicationCommands.h"
#include "ApplicationInstanceCommands.h"
#include "ClusterCommands.h"
#include "SecretCommands.h"
#include "UserCommands.h"
#include "VOCommands.h"

void initializeHelm(){
	const static std::string helmRepoBase="https://raw.githubusercontent.com/slateci/slate-catalog/master";
	
	auto helmCheck=runCommand("which",{"helm"});
	if(helmCheck.status!=0)
		log_fatal("`helm` is not available");
	
	std::string helmHome;
	fetchFromEnvironment("HELM_HOME",helmHome);
	if(helmHome.empty()){
		std::string home;
		fetchFromEnvironment("HOME",home);
		if(home.empty())
			log_fatal("Neither $HOME nor $HELM_HOME is not set, unable to find helm data directory");
		else
			helmHome=home+"/.helm";
	}
	
	struct stat info;
	int err=stat((helmHome+"/repository").c_str(),&info);
	if(err){
		err=errno;
		if(err!=ENOENT)
			log_fatal("Unable to stat "+helmHome+"/repository; error "+std::to_string(err));
		else{ //try to initialize helm
			log_info("Helm appears not to be initialized; initializing");
			auto helmResult=runCommand("helm",{"init","-c"});
			if(helmResult.status)
				log_fatal("Helm initialization failed: \n"+helmResult.output);
			if(helmResult.output.find("Happy Helming")==std::string::npos)
				//TODO: this only reports what was sent to stdout. . . 
				//which tends not to contain the error message.
				log_fatal("Helm initialization failed: \n"+helmResult.output);
			log_info("Helm successfully initialized");
		}
	}
	{ //Ensure that necessary repositories are installed
		auto helmResult=runCommand("helm",{"repo","list"});
		if(helmResult.status)
			log_fatal("helm repo list failed");
		auto lines=string_split_lines(helmResult.output);
		bool hasMain=false, hasDev=false;
		for(const auto& line  : lines){
			auto tokens=string_split_columns(line,'\t');
			if(!tokens.empty()){
				if(trim(tokens[0])=="slate")
					hasMain=true;
				else if(trim(tokens[0])=="slate-dev")
					hasDev=true;
			}
		}
		if(!hasMain){
			log_info("Main slate repository not installed; installing");
			err=runCommand("helm",{"repo","add","slate",helmRepoBase+"/stable-repo/"}).status;
			if(err)
				log_fatal("Unable to install main slate repository");
		}
		if(!hasDev){
			log_info("Slate development repository not installed; installing");
			err=runCommand("helm",{"repo","add","slate-dev",helmRepoBase+"/incubator-repo/"}).status;
			if(err)
				log_fatal("Unable to install slate development repository");
		}
	}
	{ //Ensure that repositories are up-to-date
		err=runCommand("helm",{"repo","update"}).status;
		if(err)
			log_fatal("helm repo update failed");
	}
}

int main(int argc, char* argv[]){
	std::string awsAccessKey="foo";
	std::string awsSecretKey="bar";
	std::string awsRegion="us-east-1";
	std::string awsURLScheme="http";
	std::string awsEndpoint="localhost:8000";
	std::string portString="18080";
	std::string sslCertificate;
	std::string sslKey;
	
	//check for environment variables
	fetchFromEnvironment("SLATE_awsAccessKey",awsAccessKey);
	fetchFromEnvironment("SLATE_awsSecretKey",awsSecretKey);
	fetchFromEnvironment("SLATE_awsRegion",awsRegion);
	fetchFromEnvironment("SLATE_awsURLScheme",awsURLScheme);
	fetchFromEnvironment("SLATE_awsEndpoint",awsEndpoint);
	fetchFromEnvironment("SLATE_PORT",portString);
	fetchFromEnvironment("SLATE_SSL_CERTIFICATE",sslCertificate);
	fetchFromEnvironment("SLATE_SSL_KEY",sslKey);
	
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
		else if(arg=="--ssl-certificate"){
			if(i==argc-1)
				log_fatal("Missing value after --ssl-certificate");
			i++;
			sslCertificate=argv[i];
		}
		else if(arg=="--ssl-key"){
			if(i==argc-1)
				log_fatal("Missing value after --ssl-key");
			i++;
			sslKey=argv[i];
		}
		else{
			log_error("Unknown argument ignored: '" << arg << '\'');
		}
	}
	if(sslCertificate.empty()!=sslKey.empty()){
		log_fatal("--ssl-certificate ($SLATE_SSL_CERTIFICATE) and --ssl-key ($SLATE_SSL_KEY)"
		          " must be specified together");
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
	
	startReaper();
	initializeHelm();
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
	  [&](const crow::request& req){ return listUsers(store,req); });
	CROW_ROUTE(server, "/v1alpha1/users").methods("POST"_method)(
	  [&](const crow::request& req){ return createUser(store,req); });
	CROW_ROUTE(server, "/v1alpha1/users/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& uID){ return getUserInfo(store,req,uID); });
	CROW_ROUTE(server, "/v1alpha1/users/<string>").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& uID){ return updateUser(store,req,uID); });
	CROW_ROUTE(server, "/v1alpha1/users/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& uID){ return deleteUser(store,req,uID); });
	CROW_ROUTE(server, "/v1alpha1/users/<string>/vos").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& uID){ return listUserVOs(store,req,uID); });
	CROW_ROUTE(server, "/v1alpha1/users/<string>/vos/<string>").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& uID, const std::string voID){ return addUserToVO(store,req,uID,voID); });
	CROW_ROUTE(server, "/v1alpha1/users/<string>/vos/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& uID, const std::string voID){ return removeUserFromVO(store,req,uID,voID); });
	CROW_ROUTE(server, "/v1alpha1/users/<string>/replace_token").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& uID){ return replaceUserToken(store,req,uID); });
	CROW_ROUTE(server, "/v1alpha1/find_user").methods("GET"_method)(
	  [&](const crow::request& req){ return findUser(store,req); });
	
	// == Cluster commands ==
	CROW_ROUTE(server, "/v1alpha1/clusters").methods("GET"_method)(
	  [&](const crow::request& req){ return listClusters(store,req); });
	CROW_ROUTE(server, "/v1alpha1/clusters").methods("POST"_method)(
	  [&](const crow::request& req){ return createCluster(store,req); });
	CROW_ROUTE(server, "/v1alpha1/clusters/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& cID){ return deleteCluster(store,req,cID); });
	CROW_ROUTE(server, "/v1alpha1/clusters/<string>").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& cID){ return updateCluster(store,req,cID); });
	CROW_ROUTE(server, "/v1alpha1/clusters/<string>/allowed_vos").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& cID){ return listClusterAllowedVOs(store,req,cID); });
	CROW_ROUTE(server, "/v1alpha1/clusters/<string>/allowed_vos/<string>").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& cID, const std::string& voID){ 
		  return grantVOClusterAccess(store,req,cID,voID); });
	CROW_ROUTE(server, "/v1alpha1/clusters/<string>/allowed_vos/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& cID, const std::string& voID){ 
		  return revokeVOClusterAccess(store,req,cID,voID); });
	
	// == VO commands ==
	CROW_ROUTE(server, "/v1alpha1/vos").methods("GET"_method)(
	  [&](const crow::request& req){ return listVOs(store,req); });
	CROW_ROUTE(server, "/v1alpha1/vos").methods("POST"_method)(
	  [&](const crow::request& req){ return createVO(store,req); });
	CROW_ROUTE(server, "/v1alpha1/vos/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& voID){ return deleteVO(store,req,voID); });
	CROW_ROUTE(server, "/v1alpha1/vos/<string>/members").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& voID){ return listVOMembers(store,req,voID); });
	CROW_ROUTE(server, "/v1alpha1/vos/<string>/clusters").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& voID){ return listVOClusters(store,req,voID); });
	
	// == Application commands ==
	CROW_ROUTE(server, "/v1alpha1/apps").methods("GET"_method)(
	  [&](const crow::request& req){ return listApplications(store,req); });
	CROW_ROUTE(server, "/v1alpha1/apps/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& aID){ return fetchApplicationConfig(store,req,aID); });
	CROW_ROUTE(server, "/v1alpha1/apps/<string>").methods("POST"_method)(
	  [&](const crow::request& req, const std::string& aID){ return installApplication(store,req,aID); });
	CROW_ROUTE(server, "/v1alpha1/update_apps").methods("POST"_method)(
	  [&](const crow::request& req){ return updateCatalog(store,req); });
	
	// == Application Instance commands ==
	CROW_ROUTE(server, "/v1alpha1/instances").methods("GET"_method)(
	  [&](const crow::request& req){ return listApplicationInstances(store,req); });
	CROW_ROUTE(server, "/v1alpha1/instances/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& iID){ return fetchApplicationInstanceInfo(store,req,iID); });
	CROW_ROUTE(server, "/v1alpha1/instances/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& iID){ return deleteApplicationInstance(store,req,iID); });
	
	// == Secret commands ==
	CROW_ROUTE(server, "/v1alpha1/secrets").methods("GET"_method)(
	  [&](const crow::request& req){ return listSecrets(store,req); });
	CROW_ROUTE(server, "/v1alpha1/secrets").methods("POST"_method)(
	  [&](const crow::request& req){ return createSecret(store,req); });
	CROW_ROUTE(server, "/v1alpha1/secrets/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& id){ return getSecret(store,req,id); });
	CROW_ROUTE(server, "/v1alpha1/secrets/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& id){ return deleteSecret(store,req,id); });
	
	CROW_ROUTE(server, "/v1alpha1/stats").methods("GET"_method)(
	  [&](){ return(store.getStatistics()); });
	
	server.loglevel(crow::LogLevel::Warning);
	if(!sslCertificate.empty())
		server.port(port).ssl_file(sslCertificate,sslKey).multithreaded().run();
	else
		server.port(port).multithreaded().run();
}
