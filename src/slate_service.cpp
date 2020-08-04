#include <cerrno>
#include <iostream>
#include <cctype>

#include <sys/stat.h>

#define CROW_ENABLE_SSL
#include <crow.h>

#include "Entities.h"
#include "Logging.h"
#include "PersistentStore.h"
#include "Process.h"
#include "ServerUtilities.h"

#include "ApplicationCommands.h"
#include "ApplicationInstanceCommands.h"
#include "ClusterCommands.h"
#include "GroupCommands.h"
#include "MonitoringCredentialCommands.h"
#include "UserCommands.h"
#include "SecretCommands.h"
#include "VersionCommands.h"
#include "KubeInterface.h"

void initializeHelm(){
	const static std::string helmRepoBase="https://jenkins.slateci.io/catalog";
	
	
	auto helmCheck=runCommand("helm");
	if(helmCheck.status!=0)
		log_fatal("`helm` is not available, error "+std::to_string(helmCheck.status)+" ("+strerror(helmCheck.status)+")");
	
	unsigned int helmMajorVersion=kubernetes::getHelmMajorVersion();
	
	if(helmMajorVersion==2){
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
	}
	{ //Ensure that necessary repositories are installed
		auto helmResult=runCommand("helm",{"repo","list"});
		//helm repo list failing is generally a problem we can't resolve internally.
		//However, helm 3 added the unhelpful behavior of exiting with status 1
		//when its repository list is empty instead of continuing to do the 
		//obvious thing and outputting the empty list, so we must detect this as
		//a special case.
		if(helmResult.status && 
		   !(helmMajorVersion>2 && 
		     helmResult.error.find("Error: no repositories to show")!=std::string::npos))
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
			int err=runCommand("helm",{"repo","add","slate",helmRepoBase+"/stable/"}).status;
			if(err)
				log_fatal("Unable to install main slate repository");
		}
		if(!hasDev){
			log_info("Slate development repository not installed; installing");
			int err=runCommand("helm",{"repo","add","slate-dev",helmRepoBase+"/incubator/"}).status;
			if(err)
				log_fatal("Unable to install slate development repository");
		}
	}
	{ //Ensure that repositories are up-to-date
		int err=runCommand("helm",{"repo","update"}).status;
		if(err)
			log_fatal("helm repo update failed");
	}
}

struct Configuration{
	struct ParamRef{
		enum Type{String,Bool,UInt} type;
		union{
			std::reference_wrapper<std::string> s;
			std::reference_wrapper<bool> b;
			std::reference_wrapper<unsigned int> u;
		};
		ParamRef(std::string& s):type(String),s(s){}
		ParamRef(bool& b):type(Bool),b(b){}
		ParamRef(unsigned int& u):type(UInt),u(u){}
		ParamRef(const ParamRef& p):type(p.type){
			switch(type){
				case String: s=p.s; break;
				case Bool: b=p.b; break;
				case UInt: u=p.u; break;
			}
		}
		
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
				case UInt:
				{
					try{
						u.get()=std::stoul(value);
					}catch(...){
						log_error("Unable to parse '" << value << "' as an unsigned integer");
						throw;
					}
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
	std::string geocodeEndpoint;
	std::string geocodeToken;
	std::string portString;
	std::string sslCertificate;
	std::string sslKey;
	std::string bootstrapUserFile;
	std::string encryptionKeyFile;
	std::string appLoggingServerName;
	std::string appLoggingServerPortString;
	bool allowAdHocApps;
	std::string mailgunEndpoint;
	std::string mailgunKey;
	std::string emailDomain;
	std::string opsEmail;
	unsigned int serverThreads;
	
	std::map<std::string,ParamRef> options;
	
	Configuration(int argc, char* argv[]):
	awsAccessKey("foo"),
	awsSecretKey("bar"),
	awsRegion("us-east-1"),
	awsURLScheme("http"),
	awsEndpoint("localhost:8000"),
	geocodeEndpoint("https://geocode.xyz"),
	portString("18080"),
	bootstrapUserFile("slate_portal_user"),
	encryptionKeyFile("encryptionKey"),
	appLoggingServerPortString("9200"),
	allowAdHocApps(false),
	mailgunEndpoint("api.mailgun.net"),
	emailDomain("slateci.io"),
	opsEmail("slateci-ops@googlegroups.com"),
	serverThreads(0),
	options{
		{"awsAccessKey",awsAccessKey},
		{"awsSecretKey",awsSecretKey},
		{"awsRegion",awsRegion},
		{"awsURLScheme",awsURLScheme},
		{"awsEndpoint",awsEndpoint},
		{"geocodeEndpoint",geocodeEndpoint},
		{"geocodeToken",geocodeToken},
		{"port",portString},
		{"sslCertificate",sslCertificate},
		{"sslKey",sslKey},
		{"bootstrapUserFile",bootstrapUserFile},
		{"encryptionKeyFile",encryptionKeyFile},
		{"appLoggingServerName",appLoggingServerName},
		{"appLoggingServerPort",appLoggingServerPortString},
		{"allowAdHocApps",allowAdHocApps},
		{"mailgunEndpoint",mailgunEndpoint},
		{"mailgunKey",mailgunKey},
		{"emailDomain",emailDomain},
		{"opsEmail",opsEmail},
		{"threads",serverThreads}
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

///Accept a dictionary describing several individual requests, execute them all 
///concurrently, and return the results in another dictionary. Currently very
///simplistic; a new thread will be spawned for every individual request. 
crow::response multiplex(crow::SimpleApp& server, PersistentStore& store, const crow::request& req){
	using namespace std::chrono;
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested execute a command bundle");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	rapidjson::Document body;
	try{
		body.Parse(req.body.c_str());
	}catch(std::runtime_error& err){
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	
	if(!body.IsObject())
		return crow::response(400,generateError("Multiplexed requests must have a JSON object/dictionary as the request body"));
	
	auto parseHTTPMethod=[](std::string method){
		std::transform(method.begin(),method.end(),method.begin(),[](char c)->char{return std::toupper(c);});
		if(method=="DELETE") return crow::HTTPMethod::Delete;
		if(method=="GET") return crow::HTTPMethod::Get;
		if(method=="HEAD") return crow::HTTPMethod::Head;
		if(method=="POST") return crow::HTTPMethod::Post;
		if(method=="PUT") return crow::HTTPMethod::Put;
		if(method=="CONNECT") return crow::HTTPMethod::Connect;
		if(method=="OPTIONS") return crow::HTTPMethod::Options;
		if(method=="TRACE") return crow::HTTPMethod::Trace;
		if(method=="PATCH") return crow::HTTPMethod::Patch;
		if(method=="PURGE") return crow::HTTPMethod::Purge;
		throw std::runtime_error(generateError("Unrecognized HTTP method: "+method));
	};
	
	std::vector<crow::request> requests;
	requests.reserve(body.GetObject().MemberCount());
	for(const auto& rawRequest : body.GetObject()){
		if(!rawRequest.value.IsObject())
			return crow::response(400,generateError("Individual requests must be represented as JSON objects/dictionaries"));
		if(!rawRequest.value.HasMember("method") || !rawRequest.value["method"].IsString())
			return crow::response(400,generateError("Individual requests must have a string member named 'method' indicating the HTTP method"));
		if(rawRequest.value.HasMember("body") && !rawRequest.value["method"].IsString())
			return crow::response(400,generateError("Individual requests must have bodies represented as strings"));
		std::string rawURL=rawRequest.name.GetString();
		std::string body;
		if(rawRequest.value.HasMember("body"))
			body=rawRequest.value["body"].GetString();
		requests.emplace_back(parseHTTPMethod(rawRequest.value["method"].GetString()), //method
		                      rawURL, //raw_url
		                      rawURL.substr(0, rawURL.find("?")), //url
		                      crow::query_string(rawURL), //url_params
		                      crow::ci_map{}, //headers, currently not handled
		                      body //body
		                      );
		requests.back().remote_endpoint=req.remote_endpoint;
	}
	
	std::vector<std::future<crow::response>> responses;
	responses.reserve(requests.size());
	
	for(const auto& request : requests)
		responses.emplace_back(std::async(std::launch::async,[&](){ 
			crow::response response;
			server.handle(request, response);
			return response;
		}));
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	for(std::size_t i=0; i<requests.size(); i++){
		const auto& request=requests[i];
		rapidjson::Value singleResult(rapidjson::kObjectType);
		try{
			crow::response response=responses[i].get();
			singleResult.AddMember("status",response.code,alloc);
			singleResult.AddMember("body",response.body,alloc);
		}
		catch(std::exception& ex){
			singleResult.AddMember("status",400,alloc);
			singleResult.AddMember("body",generateError(ex.what()),alloc);
		}
		catch(...){
			singleResult.AddMember("status",400,alloc);
			singleResult.AddMember("body",generateError("Exception"),alloc);
		}
		rapidjson::Value key(rapidjson::kStringType);
		key.SetString(requests[i].raw_url, alloc);
		result.AddMember(key, singleResult, alloc);
	}
	
	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	log_info("command bundle completed in " << duration_cast<duration<double>>(t2-t1).count() << " seconds");
	return crow::response(to_string(result));
}

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
	
	if(config.serverThreads==0)
		config.serverThreads=std::thread::hardware_concurrency();
	log_info("Using " << config.serverThreads << " web server threads");
	
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
	
	EmailClient emailClient(config.mailgunEndpoint,config.mailgunKey,config.emailDomain);
	
	PersistentStore store(credentials,clientConfig,
	                      config.bootstrapUserFile,config.encryptionKeyFile,
	                      config.appLoggingServerName,appLoggingServerPort);
	if(!config.geocodeEndpoint.empty() && !config.geocodeToken.empty())
		store.setGeocoder(Geocoder(config.geocodeEndpoint,config.geocodeToken));
	if(!config.mailgunEndpoint.empty() && !config.mailgunKey.empty() && !config.emailDomain.empty()){
		store.setEmailClient(EmailClient(config.mailgunEndpoint,config.mailgunKey,config.emailDomain));
		log_info("Email notifications configured");
	}
	else
		log_info("Email notifications not configured");
	store.setOpsEmail(config.opsEmail);
	
	// REST server initialization
	crow::SimpleApp server;
	
	CROW_ROUTE(server, "/v1alpha3/multiplex").methods("POST"_method)(
	  [&](const crow::request& req){ return multiplex(server,store,req); });
	
	// == User commands ==
	CROW_ROUTE(server, "/v1alpha3/users").methods("GET"_method)(
	  [&](const crow::request& req){ return listUsers(store,req); });
	CROW_ROUTE(server, "/v1alpha3/users").methods("POST"_method)(
	  [&](const crow::request& req){ return createUser(store,req); });
	CROW_ROUTE(server, "/v1alpha3/users/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& uID){ return getUserInfo(store,req,uID); });
	CROW_ROUTE(server, "/v1alpha3/users/<string>").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& uID){ return updateUser(store,req,uID); });
	CROW_ROUTE(server, "/v1alpha3/users/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& uID){ return deleteUser(store,req,uID); });
	CROW_ROUTE(server, "/v1alpha3/users/<string>/groups").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& uID){ return listUsergroups(store,req,uID); });
	CROW_ROUTE(server, "/v1alpha3/users/<string>/groups/<string>").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& uID, const std::string groupID){ return addUserToGroup(store,req,uID,groupID); });
	CROW_ROUTE(server, "/v1alpha3/users/<string>/groups/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& uID, const std::string groupID){ return removeUserFromGroup(store,req,uID,groupID); });
	CROW_ROUTE(server, "/v1alpha3/users/<string>/replace_token").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& uID){ return replaceUserToken(store,req,uID); });
	CROW_ROUTE(server, "/v1alpha3/find_user").methods("GET"_method)(
	  [&](const crow::request& req){ return findUser(store,req); });
	CROW_ROUTE(server, "/v1alpha3/whoami").methods("GET"_method)(
	  [&](const crow::request& req){ return whoAreThey(store,req); });
	
	// == Cluster commands ==
	CROW_ROUTE(server, "/v1alpha3/clusters").methods("GET"_method)(
	  [&](const crow::request& req){ return listClusters(store,req); });
	CROW_ROUTE(server, "/v1alpha3/clusters").methods("POST"_method)(
	  [&](const crow::request& req){ return createCluster(store,req); });
	CROW_ROUTE(server, "/v1alpha3/clusters/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& cID){ return getClusterInfo(store,req,cID); });
	CROW_ROUTE(server, "/v1alpha3/clusters/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& cID){ return deleteCluster(store,req,cID); });
	CROW_ROUTE(server, "/v1alpha3/clusters/<string>").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& cID){ return updateCluster(store,req,cID); });
	CROW_ROUTE(server, "/v1alpha3/clusters/<string>/ping").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& cID){ return pingCluster(store,req,cID); });
	CROW_ROUTE(server, "/v1alpha3/clusters/<string>/verify").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& cID){ return verifyCluster(store,req,cID); });
	CROW_ROUTE(server, "/v1alpha3/clusters/<string>/allowed_groups").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& cID){ return listClusterAllowedgroups(store,req,cID); });
	CROW_ROUTE(server, "/v1alpha3/clusters/<string>/allowed_groups/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& cID, const std::string& groupID){ 
		  return checkGroupClusterAccess(store,req,cID,groupID); });
	CROW_ROUTE(server, "/v1alpha3/clusters/<string>/allowed_groups/<string>").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& cID, const std::string& groupID){ 
		  return grantGroupClusterAccess(store,req,cID,groupID); });
	CROW_ROUTE(server, "/v1alpha3/clusters/<string>/allowed_groups/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& cID, const std::string& groupID){ 
		  return revokeGroupClusterAccess(store,req,cID,groupID); });
	CROW_ROUTE(server, "/v1alpha3/clusters/<string>/allowed_groups/<string>/applications")
	  .methods("GET"_method)(
	  [&](const crow::request& req, const std::string& cID, const std::string& groupID){ 
		  return listClusterGroupAllowedApplications(store,req,cID,groupID); });
	CROW_ROUTE(server, "/v1alpha3/clusters/<string>/allowed_groups/<string>/applications/<string>")
	  .methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& cID, const std::string& groupID, const std::string& app){ 
		  return allowGroupUseOfApplication(store,req,cID,groupID,app); });
	CROW_ROUTE(server, "/v1alpha3/clusters/<string>/allowed_groups/<string>/applications/<string>")
	  .methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& cID, const std::string& groupID, const std::string& app){ 
		  return denyGroupUseOfApplication(store,req,cID,groupID,app); });
	CROW_ROUTE(server, "/v1alpha3/clusters/<string>/monitoring_credential").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& cID){ return getClusterMonitoringCredential(store,req,cID); });
	CROW_ROUTE(server, "/v1alpha3/clusters/<string>/monitoring_credential").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& cID){ return removeClusterMonitoringCredential(store,req,cID); });
	
	// == Monitoring Credential commands ==
	CROW_ROUTE(server, "/v1alpha3/monitoring_credentials").methods("GET"_method)(
	  [&](const crow::request& req){ return listMonitoringCredentials(store,req); });
	CROW_ROUTE(server, "/v1alpha3/monitoring_credentials").methods("POST"_method)(
	  [&](const crow::request& req){ return addMonitoringCredential(store,req); });
	CROW_ROUTE(server, "/v1alpha3/monitoring_credentials/<string>/revoke").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& cID){ return revokeMonitoringCredential(store,req,cID); });
	CROW_ROUTE(server, "/v1alpha3/monitoring_credentials/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& cID){ return deleteMonitoringCredential(store,req,cID); });
	
	// == Group commands ==
	CROW_ROUTE(server, "/v1alpha3/groups").methods("GET"_method)(
	  [&](const crow::request& req){ return listGroups(store,req); });
	CROW_ROUTE(server, "/v1alpha3/groups").methods("POST"_method)(
	  [&](const crow::request& req){ return createGroup(store,req); });
	CROW_ROUTE(server, "/v1alpha3/groups/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& groupID){ return getGroupInfo(store,req,groupID); });
	CROW_ROUTE(server, "/v1alpha3/groups/<string>").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& groupID){ return updateGroup(store,req,groupID); });
	CROW_ROUTE(server, "/v1alpha3/groups/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& groupID){ return deleteGroup(store,req,groupID); });
	CROW_ROUTE(server, "/v1alpha3/groups/<string>/members").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& groupID){ return listGroupMembers(store,req,groupID); });
	CROW_ROUTE(server, "/v1alpha3/groups/<string>/clusters").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& groupID){ return listGroupClusters(store,req,groupID); });
	
	// == Application commands ==
	CROW_ROUTE(server, "/v1alpha3/apps").methods("GET"_method)(
	  [&](const crow::request& req){ return listApplications(store,req); });
	CROW_ROUTE(server, "/v1alpha3/apps/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& aID){ return fetchApplicationConfig(store,req,aID); });
	CROW_ROUTE(server, "/v1alpha3/apps/<string>/info").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& aID){ return fetchApplicationDocumentation(store,req,aID); });
	if(config.allowAdHocApps){
		CROW_ROUTE(server, "/v1alpha3/apps/ad-hoc").methods("POST"_method)(
		  [&](const crow::request& req){ return installAdHocApplication(store,req); });
	}
	else{
		CROW_ROUTE(server, "/v1alpha3/apps/ad-hoc").methods("POST"_method)(
		  [&](const crow::request& req){ return crow::response(400,generateError("Ad-hoc application installation is not permitted")); });
	}
	CROW_ROUTE(server, "/v1alpha3/apps/<string>").methods("POST"_method)(
	  [&](const crow::request& req, const std::string& aID){ return installApplication(store,req,aID); });
	CROW_ROUTE(server, "/v1alpha3/update_apps").methods("POST"_method)(
	  [&](const crow::request& req){ return updateCatalog(store,req); });
	
	// == Application Instance commands ==
	CROW_ROUTE(server, "/v1alpha3/instances").methods("GET"_method)(
	  [&](const crow::request& req){ return listApplicationInstances(store,req); });
	CROW_ROUTE(server, "/v1alpha3/instances/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& iID){ return fetchApplicationInstanceInfo(store,req,iID); });
	CROW_ROUTE(server, "/v1alpha3/instances/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& iID){ return deleteApplicationInstance(store,req,iID); });
	CROW_ROUTE(server, "/v1alpha3/instances/<string>/restart").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& iID){ return restartApplicationInstance(store,req,iID); });
	CROW_ROUTE(server, "/v1alpha3/instances/<string>/logs").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& iID){ return getApplicationInstanceLogs(store,req,iID); });
	CROW_ROUTE(server, "/v1alpha3/instances/<string>/scale").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& iID){ return getApplicationInstanceScale(store,req,iID); });
	CROW_ROUTE(server, "/v1alpha3/instances/<string>/scale").methods("PUT"_method)(
	  [&](const crow::request& req, const std::string& iID){ return scaleApplicationInstance(store,req,iID); });
	
	// == Secret commands ==
	CROW_ROUTE(server, "/v1alpha3/secrets").methods("GET"_method)(
	  [&](const crow::request& req){ return listSecrets(store,req); });
	CROW_ROUTE(server, "/v1alpha3/secrets").methods("POST"_method)(
	  [&](const crow::request& req){ return createSecret(store,req); });
	CROW_ROUTE(server, "/v1alpha3/secrets/<string>").methods("GET"_method)(
	  [&](const crow::request& req, const std::string& id){ return getSecret(store,req,id); });
	CROW_ROUTE(server, "/v1alpha3/secrets/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, const std::string& id){ return deleteSecret(store,req,id); });
	
	CROW_ROUTE(server, "/v1alpha3/stats").methods("GET"_method)(
	  [&](){ return(store.getStatistics()); });
	
	CROW_ROUTE(server, "/version").methods("GET"_method)(&serverVersionInfo);
	
	//include a fallback to catch unexpected/unsupported things
	CROW_ROUTE(server, "/<string>/<path>").methods("GET"_method)(
	  [](std::string apiVersion, std::string path){
	  	return crow::response(400,generateError("Unsupported API version")); });
	
	server.loglevel(crow::LogLevel::Warning);
	if(!config.sslCertificate.empty())
		server.port(port).ssl_file(config.sslCertificate,config.sslKey).concurrency(config.serverThreads).run();
	else
		server.port(port).concurrency(config.serverThreads).run();
}
