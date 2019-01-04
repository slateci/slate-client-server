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
#include "VersionCommands.h"

void initializeHelm(){
	const static std::string helmRepoBase="https://raw.githubusercontent.com/slateci/slate-catalog/master";
	
	try{
	auto helmCheck=runCommand("helm");
		if(helmCheck.status!=0)
			log_fatal("`helm` is not available");
	}catch(std::runtime_error& err){
		log_fatal("`helm` is not available: " << err.what());
	}
	
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

struct Configuration{
	struct ParamRef{
		enum Type{String,Bool} type;
		union{
			std::reference_wrapper<std::string> s;
			std::reference_wrapper<bool> b;
		};
		ParamRef(std::string& s):type(String),s(s){}
		ParamRef(bool& b):type(Bool),b(b){}
		
		ParamRef& operator=(const std::string& value){
			switch(type){
				case String:
					s.get()=value;
					break;
				case Bool:
				{
					 if(value=="true" || value=="True" || value=="1")
					 	b.get()=true;
					 else
					 	b.get()=false;
					 break;
				}
			}
			return *this;
		}
	};

	std::string awsAccessKey;
	std::string awsSecretKey;
	std::string awsRegion;
	std::string awsURLScheme;
	std::string awsEndpoint;
	std::string portString;
	std::string sslCertificate;
	std::string sslKey;
	std::string bootstrapUserFile;
	std::string encryptionKeyFile;
	std::string appLoggingServerName;
	std::string appLoggingServerPortString;
	bool allowAdHocApps;
	
	std::map<std::string,ParamRef> options;
	
	Configuration(int argc, char* argv[]):
	awsAccessKey("foo"),
	awsSecretKey("bar"),
	awsRegion("us-east-1"),
	awsURLScheme("http"),
	awsEndpoint("localhost:8000"),
	portString("18080"),
	bootstrapUserFile("slate_portal_user"),
	encryptionKeyFile("encryptionKey"),
	appLoggingServerPortString("9200"),
	allowAdHocApps(false),
	options{
		{"awsAccessKey",awsAccessKey},
		{"awsSecretKey",awsSecretKey},
		{"awsRegion",awsRegion},
		{"awsURLScheme",awsURLScheme},
		{"awsEndpoint",awsEndpoint},
		{"port",portString},
		{"sslCertificate",sslCertificate},
		{"sslKey",sslKey},
		{"bootstrapUserFile",bootstrapUserFile},
		{"encryptionKeyFile",encryptionKeyFile},
		{"appLoggingServerName",appLoggingServerName},
		{"appLoggingServerPort",appLoggingServerPortString},
		{"allowAdHocApps",allowAdHocApps},
	}
	{
		//check for environment variables
		for(auto& option : options)
			fetchFromEnvironment("SLATE_"+option.first,option.second);
		
		std::string configPath;
		fetchFromEnvironment("SLATE_config",configPath);
		if(!configPath.empty())
			parseFile({configPath});
		
		//interpret command line arguments
		for(int i=1; i<argc; i++){
			std::string arg(argv[i]);
			if(arg.size()<=2 || arg[0]!='-' || arg[1]!='-'){
				log_error("Unknown argument ignored: '" << arg << '\'');
				continue;
			}
			auto eqPos=arg.find('=');
			std::string optName=arg.substr(2,eqPos-2);
			if(options.count(optName)){
				if(eqPos!=std::string::npos)
					options.find(optName)->second=arg.substr(eqPos+1);
				else{
					if(i==argc-1)
						log_fatal("Missing value after "+arg);
					i++;
					options.find(arg.substr(2))->second=argv[i];
				}
			}
			else if(optName=="config"){
				if(eqPos!=std::string::npos)
					parseFile({arg.substr(eqPos+1)});
				else{
					if(i==argc-1)
						log_fatal("Missing value after "+arg);
					i++;
					parseFile({argv[i]});
				}
			}
			else
				log_error("Unknown argument ignored: '" << arg << '\'');
		}
	}
	
	//attempt to read the last file in files, checking that it does not appear
	//previously
	void parseFile(const std::vector<std::string>& files){
		assert(!files.empty());
		if(std::find(files.begin(),files.end(),files.back())<(files.end()-1)){
			log_error("Configuration file loop: ");
			for(const auto file : files)
				log_error("  " << file);
			log_fatal("Configuration parsing terminated");
		}
		std::ifstream infile(files.back());
		if(!infile)
			log_fatal("Unable to open " << files.back() << " for reading");
		std::string line;
		unsigned int lineNumber=1;
		while(std::getline(infile,line)){
			auto eqPos=line.find('=');
			std::string optName=line.substr(0,eqPos);
			std::string value=line.substr(eqPos+1);
			if(options.count(optName))
				options.find(optName)->second=value;
			else if(optName=="config"){
				auto newFiles=files;
				newFiles.push_back(value);
				parseFile(newFiles);
			}
			else
				log_error(files.back() << ':' << lineNumber 
				          << ": Unknown option ignored: '" << line << '\'');
			lineNumber++;
		}
	}
	
};

int main(int argc, char* argv[]){
	Configuration config(argc, argv);
	
	if(config.sslCertificate.empty()!=config.sslKey.empty()){
		log_fatal("--sslCertificate ($SLATE_sslCertificate) and --sslKey ($SLATE_sslKey)"
		          " must be specified together");
	}
	
	log_info("Database URL is " << config.awsURLScheme << "://" << config.awsEndpoint);
	unsigned int port=0;
	{
		std::istringstream is(config.portString);
		is >> port;
		if(!port || is.fail())
			log_fatal("Unable to parse \"" << config.portString << "\" as a valid port number");
	}
	log_info("Service port is " << port);
	
	unsigned int appLoggingServerPort=0;
	{
		std::istringstream is(config.appLoggingServerPortString);
		is >> appLoggingServerPort;
		if(!appLoggingServerPort || is.fail())
			log_fatal("Unable to parse \"" << config.appLoggingServerPortString << "\" as a valid port number");
	}
	
	startReaper();
	initializeHelm();
	// DB client initialization
	Aws::SDKOptions awsOptions;
	Aws::InitAPI(awsOptions);
	using AWSOptionsHandle=std::unique_ptr<Aws::SDKOptions,void(*)(Aws::SDKOptions*)>;
	AWSOptionsHandle opt_holder(&awsOptions,
								[](Aws::SDKOptions* awsOptions){
									Aws::ShutdownAPI(*awsOptions); 
								});
	Aws::Auth::AWSCredentials credentials(config.awsAccessKey,config.awsSecretKey);
	Aws::Client::ClientConfiguration clientConfig;
	clientConfig.region=config.awsRegion;
	if(config.awsURLScheme=="http")
		clientConfig.scheme=Aws::Http::Scheme::HTTP;
	else if(config.awsURLScheme=="https")
		clientConfig.scheme=Aws::Http::Scheme::HTTPS;
	else
		log_fatal("Unrecognized URL scheme for AWS: '" << config.awsURLScheme << '\'');
	clientConfig.endpointOverride=config.awsEndpoint;
	PersistentStore store(credentials,clientConfig,
	                      config.bootstrapUserFile,config.encryptionKeyFile,
	                      config.appLoggingServerName,appLoggingServerPort);
	
	// REST server initialization
	crow::SimpleApp server;
	
	// == User commands ==
	CROW_ROUTE(server, "/v1alpha2/users").methods("GET"_method)(
	  [&](const crow::request& req){ return listUsers(store,req); });
	CROW_ROUTE(server, "/v1alpha2/users").methods("POST"_method)(
	  [&](const crow::request& req){ return createUser(store,req); });
	CROW_ROUTE(server, "/v1alpha2/users/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& uID){ return getUserInfo(store,req,uID); });
	CROW_ROUTE(server, "/v1alpha2/users/<string>").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& uID){ return updateUser(store,req,uID); });
	CROW_ROUTE(server, "/v1alpha2/users/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& uID){ return deleteUser(store,req,uID); });
	CROW_ROUTE(server, "/v1alpha2/users/<string>/vos").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& uID){ return listUserVOs(store,req,uID); });
	CROW_ROUTE(server, "/v1alpha2/users/<string>/vos/<string>").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& uID, const std::string voID){ return addUserToVO(store,req,uID,voID); });
	CROW_ROUTE(server, "/v1alpha2/users/<string>/vos/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& uID, const std::string voID){ return removeUserFromVO(store,req,uID,voID); });
	CROW_ROUTE(server, "/v1alpha2/users/<string>/replace_token").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& uID){ return replaceUserToken(store,req,uID); });
	CROW_ROUTE(server, "/v1alpha2/find_user").methods("GET"_method)(
	  [&](const crow::request& req){ return findUser(store,req); });
	
	// == Cluster commands ==
	CROW_ROUTE(server, "/v1alpha2/clusters").methods("GET"_method)(
	  [&](const crow::request& req){ return listClusters(store,req); });
	CROW_ROUTE(server, "/v1alpha2/clusters").methods("POST"_method)(
	  [&](const crow::request& req){ return createCluster(store,req); });
	CROW_ROUTE(server, "/v1alpha2/clusters/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& cID){ return deleteCluster(store,req,cID); });
	CROW_ROUTE(server, "/v1alpha2/clusters/<string>").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& cID){ return updateCluster(store,req,cID); });
	CROW_ROUTE(server, "/v1alpha2/clusters/<string>/verify").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& cID){ return verifyCluster(store,req,cID); });
	CROW_ROUTE(server, "/v1alpha2/clusters/<string>/allowed_vos").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& cID){ return listClusterAllowedVOs(store,req,cID); });
	CROW_ROUTE(server, "/v1alpha2/clusters/<string>/allowed_vos/<string>").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& cID, const std::string& voID){ 
		  return grantVOClusterAccess(store,req,cID,voID); });
	CROW_ROUTE(server, "/v1alpha2/clusters/<string>/allowed_vos/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& cID, const std::string& voID){ 
		  return revokeVOClusterAccess(store,req,cID,voID); });
	CROW_ROUTE(server, "/v1alpha2/clusters/<string>/allowed_vos/<string>/applications")
	  .methods("GET"_method)(
	  [&](const crow::request& req, const std::string& cID, const std::string& voID){ 
		  return listClusterVOAllowedApplications(store,req,cID,voID); });
	CROW_ROUTE(server, "/v1alpha2/clusters/<string>/allowed_vos/<string>/applications/<string>")
	  .methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& cID, const std::string& voID, const std::string& app){ 
		  return allowVOUseOfApplication(store,req,cID,voID,app); });
	CROW_ROUTE(server, "/v1alpha2/clusters/<string>/allowed_vos/<string>/applications/<string>")
	  .methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& cID, const std::string& voID, const std::string& app){ 
		  return denyVOUseOfApplication(store,req,cID,voID,app); });
	
	// == VO commands ==
	CROW_ROUTE(server, "/v1alpha2/vos").methods("GET"_method)(
	  [&](const crow::request& req){ return listVOs(store,req); });
	CROW_ROUTE(server, "/v1alpha2/vos").methods("POST"_method)(
	  [&](const crow::request& req){ return createVO(store,req); });
	CROW_ROUTE(server, "/v1alpha2/vos/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& voID){ return deleteVO(store,req,voID); });
	CROW_ROUTE(server, "/v1alpha2/vos/<string>/members").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& voID){ return listVOMembers(store,req,voID); });
	CROW_ROUTE(server, "/v1alpha2/vos/<string>/clusters").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& voID){ return listVOClusters(store,req,voID); });
	
	// == Application commands ==
	CROW_ROUTE(server, "/v1alpha2/apps").methods("GET"_method)(
	  [&](const crow::request& req){ return listApplications(store,req); });
	CROW_ROUTE(server, "/v1alpha2/apps/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& aID){ return fetchApplicationConfig(store,req,aID); });
	if(config.allowAdHocApps){
		CROW_ROUTE(server, "/v1alpha2/apps/ad-hoc").methods("POST"_method)(
		  [&](const crow::request& req){ return installAdHocApplication(store,req); });
	}
	else{
		CROW_ROUTE(server, "/v1alpha2/apps/ad-hoc").methods("POST"_method)(
		  [&](const crow::request& req){ return crow::response(400,generateError("Ad-hoc application installation is not permitted")); });
	}
	CROW_ROUTE(server, "/v1alpha2/apps/<string>").methods("POST"_method)(
	  [&](const crow::request& req, const std::string& aID){ return installApplication(store,req,aID); });
	CROW_ROUTE(server, "/v1alpha2/update_apps").methods("POST"_method)(
	  [&](const crow::request& req){ return updateCatalog(store,req); });
	
	// == Application Instance commands ==
	CROW_ROUTE(server, "/v1alpha2/instances").methods("GET"_method)(
	  [&](const crow::request& req){ return listApplicationInstances(store,req); });
	CROW_ROUTE(server, "/v1alpha2/instances/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& iID){ return fetchApplicationInstanceInfo(store,req,iID); });
	CROW_ROUTE(server, "/v1alpha2/instances/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& iID){ return deleteApplicationInstance(store,req,iID); });
	CROW_ROUTE(server, "/v1alpha2/instances/<string>/logs").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& iID){ return getApplicationInstanceLogs(store,req,iID); });
	
	// == Secret commands ==
	CROW_ROUTE(server, "/v1alpha2/secrets").methods("GET"_method)(
	  [&](const crow::request& req){ return listSecrets(store,req); });
	CROW_ROUTE(server, "/v1alpha2/secrets").methods("POST"_method)(
	  [&](const crow::request& req){ return createSecret(store,req); });
	CROW_ROUTE(server, "/v1alpha2/secrets/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& id){ return getSecret(store,req,id); });
	CROW_ROUTE(server, "/v1alpha2/secrets/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& id){ return deleteSecret(store,req,id); });
	
	CROW_ROUTE(server, "/v1alpha2/stats").methods("GET"_method)(
	  [&](){ return(store.getStatistics()); });
	
	CROW_ROUTE(server, "/version").methods("GET"_method)(&serverVersionInfo);
	
	//include a fallback to catch unexpected/unsupported things
	CROW_ROUTE(server, "/<string>/<path>").methods("GET"_method)(
	  [](std::string apiVersion, std::string path){
	  	return crow::response(400,generateError("Unsupported API version")); });
	
	server.loglevel(crow::LogLevel::Warning);
	if(!config.sslCertificate.empty())
		server.port(port).ssl_file(config.sslCertificate,config.sslKey).multithreaded().run();
		//server.port(port).ssl_file(config.sslCertificate,config.sslKey).concurrency(128).run();
	else
		server.port(port).multithreaded().run();
}
