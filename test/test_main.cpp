#include <array>
#include <iostream>
#include <thread>
#include <stdexcept>

#include <unistd.h>

#include <rapidjson/istreamwrapper.h>
#include "rapidjson/writer.h"

#include <yaml-cpp/yaml.h>
#include <yaml-cpp/node/parse.h>

#include "test.h"
#include "FileHandle.h"
#include "PersistentStore.h"

namespace{
	bool fetchFromEnvironment(const std::string& name, std::string& target){
		char* val=getenv(name.c_str());
		if(val){
			target=val;
			return true;
		}
		return false;
	}
}

struct test_exception : public std::runtime_error{
	test_exception(const std::string& msg):std::runtime_error(msg){}
};

void emit_error(const std::string& file, size_t line,
				const std::string& criterion, const std::string& message){
	std::ostringstream ss;
	ss << file << ':' << line << "\n\t";
	if (message.empty()) {
		ss << "Assertion failed: \n";
	} else {
		ss << message << ": \n";
	}
	ss << '\t' << criterion << std::endl;
	throw test_exception(ss.str());
}

void emit_schema_error(const std::string& file, size_t line,
		       const rapidjson::SchemaValidator& validator,
		       const std::string& message) {

	rapidjson::StringBuffer sb;
	rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
	validator.GetError().Accept(writer);

	std::ostringstream ss;
	ss << file << ':' << line << "\n\t";
	if (message.empty()) {
		ss << "Schema validation failed: ";
	} else {
		ss << message << ": ";
	}
	ss << sb.GetString();
	throw test_exception(ss.str());
}

std::map<std::string,void(*)()>&
test_registry()
{
	static auto *registry = new std::map<std::string,void(*)()>;
	return *registry;
}

DatabaseContext::DatabaseContext():
configDir(makeTemporaryDir(".storeConfig")){
	using namespace httpRequests;

	auto dbResp=httpGet("http://localhost:52000/dynamo/create");
	ENSURE_EQUAL(dbResp.status,200);
	dbPort=dbResp.body;

	{
		baseUser.id="user_testtesttest";
		baseUser.name="TestPortalUser";
		baseUser.email="unit-test@slateci.io";
		baseUser.phone="555-5555";
		baseUser.institution="SLATE";
		baseUser.token="JseHherTFh4GbrDenSe2KVshGBI6ktTE";
		baseUser.globusID="No Globus ID";
		baseUser.admin=true;
		baseUser.valid=true;

		std::ofstream profile(getPortalUserConfigPath());
		profile << baseUser.id << '\n' << baseUser.name << '\n' << baseUser.email
		<< '\n' << baseUser.phone << '\n' << baseUser.institution << '\n'
		<< baseUser.token << '\n';
		if(!profile)
			throw std::runtime_error("Unable to write portal user config file");
	}
	{
		const std::size_t bufferSize=1024;
		std::vector<char> buffer(bufferSize);
		std::ifstream urandom("/dev/urandom");
		urandom.read(buffer.data(),bufferSize);
		if(!urandom)
			throw std::runtime_error("Unable to read from /dev/urandom");
		std::ofstream encKey(getEncryptionKeyPath());
		encKey.write(buffer.data(),bufferSize);
		if(!encKey)
			throw std::runtime_error("Unable to write encryption key file");
	}
}

DatabaseContext::~DatabaseContext(){
	httpRequests::httpDelete("http://localhost:52000/dynamo/"+dbPort);
}

std::unique_ptr<PersistentStore> DatabaseContext::makePersistentStore() const{
	Aws::Auth::AWSCredentials credentials("foo","bar"); //the credentials can be made up here
	Aws::Client::ClientConfiguration clientConfig;
	clientConfig.region="us-east-1"; //also arbitrary
	clientConfig.scheme=Aws::Http::Scheme::HTTP;
	clientConfig.endpointOverride="localhost:"+getDBPort();
	
	return std::unique_ptr<PersistentStore>(new PersistentStore(credentials,
	                                                            clientConfig,
	                                                            getPortalUserConfigPath(),
	                                                            getEncryptionKeyPath(),
	                                                            "",
																0,
																"slateci.net",
	                                                            getTracer()));
}

void TestContext::waitServerReady(){
	std::cout << "Waiting for API server to be ready" << std::endl;
	//watch the server's output until it indicates that it has its database 
	//connection up and running
	std::string line;
	bool serverUp=false;
	while(!serverUp && !server.getStdout().eof() && !server.getStderr().eof()){
		std::array<char,1024> buf;
		while(!server.getStdout().eof() && server.getStdout().rdbuf()->in_avail()){
			char* ptr=buf.data();
			server.getStdout().read(ptr,1);
			ptr+=server.getStdout().gcount();
			server.getStdout().readsome(ptr,1023);
			ptr+=server.getStdout().gcount();
			std::cout.write(buf.data(),ptr-buf.data());
			line+=std::string(buf.data(),ptr-buf.data());
			if (line.find("Database client ready") != std::string::npos) {
				serverUp = true;
			}
			auto pos=line.rfind('\n');
			if (pos != std::string::npos) {
				line = line.substr(pos + 1);
			}
		}
		std::cout.flush();
		while(!server.getStderr().eof() && server.getStderr().rdbuf()->in_avail()){
			char* ptr=buf.data();
			server.getStderr().read(ptr,1);
			ptr+=server.getStderr().gcount();
			server.getStderr().readsome(ptr,1023);
			ptr+=server.getStderr().gcount();
			std::cout.write(buf.data(),ptr-buf.data());
		}
		std::cout.flush();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	
	if(server.getStdout().eof()){
		std::array<char,1024> buf;
		while(!server.getStderr().eof() && server.getStderr().rdbuf()->in_avail()){
				char* ptr=buf.data();
				server.getStderr().read(ptr,1);
				ptr+=server.getStderr().gcount();
				server.getStderr().readsome(ptr,1023);
				ptr+=server.getStderr().gcount();
				std::cerr.write(buf.data(),ptr-buf.data());
			}
			std::cerr.flush();
		throw std::runtime_error("Child process output ended");
	}
	//wait just until the server begins responding to requests
	while(true){
		try{
			auto resp=httpRequests::httpGet(getAPIServerURL()+"/"+currentAPIVersion+"/stats");
			break; //if we got any response, assume that we're done
		}catch(std::exception& ex){
			std::cout << "Exception: " << ex.what() << std::endl;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
	std::cout << "Server should be ready" << std::endl;
}

TestContext::Logger::Logger():running(false),stop(false){}

void TestContext::Logger::start(ProcessHandle& server){
	loggerThread=std::thread([&](){
		while(!server.getStdout().eof() && !server.getStderr().eof()){
			if (stop.load()) {
				break;
			}
			std::array<char,1024> buf;
			while(!server.getStdout().eof() && server.getStdout().rdbuf()->in_avail()){
				char* ptr=buf.data();
				server.getStdout().read(ptr,1);
				ptr+=server.getStdout().gcount();
				server.getStdout().readsome(ptr,1023);
				ptr+=server.getStdout().gcount();
				std::cout.write(buf.data(),ptr-buf.data());
			}
			std::cout.flush();
			while(!server.getStderr().eof() && server.getStderr().rdbuf()->in_avail()){
				char* ptr=buf.data();
				server.getStderr().read(ptr,1);
				ptr+=server.getStderr().gcount();
				server.getStderr().readsome(ptr,1023);
				ptr+=server.getStderr().gcount();
				std::cout.write(buf.data(),ptr-buf.data());
			}
			std::cout.flush();
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	});
	running=true;
}

TestContext::Logger::~Logger(){
	if(running){
		//wait just a little to give the logger time for at least one more sweep for
		//messages still to be printed
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		stop.store(true);
		loggerThread.join();
	}
}


TestContext::TestContext(std::vector<std::string> options){
	using namespace httpRequests;

	auto portResp=httpGet("http://localhost:52000/port/allocate");
	ENSURE_EQUAL(portResp.status,200);
	serverPort=portResp.body;
	
	options.insert(options.end(),{"--awsEndpoint","localhost:"+db.getDBPort(),
	                              "--port",serverPort,
								  "--disableTelemetry", "true",
	                              "--bootstrapUserFile",db.getPortalUserConfigPath(),
	                              "--encryptionKeyFile",db.getEncryptionKeyPath()});
	server=startProcessAsync("./slate-service",options);
	waitServerReady();
	logger.start(server);
}

TestContext::~TestContext(){
	httpRequests::httpDelete("http://localhost:52000/port/"+serverPort);
	server.kill();
	if (!namespaceName.empty()) {
		httpRequests::httpDelete("http://localhost:52000/namespace/" + namespaceName);
	}
}

std::string TestContext::getAPIServerURL() const{
	return "http://localhost:"+serverPort;
}

std::string TestContext::getKubeConfig(){
	if(kubeconfig.empty()){
		auto resp=httpRequests::httpGet("http://localhost:52000/namespace");
		ENSURE_EQUAL(resp.status,200);
		ENSURE_EQUAL(resp.body.find('\0'),std::string::npos,"kubeconfig should contain no NULs");
		kubeconfig=resp.body;
		ENSURE(!kubeconfig.empty());
		// get values from kubeconfig
		auto kubeYaml = YAML::Load(kubeconfig);
		if ((kubeYaml["clusters"]) && (kubeYaml["clusters"].IsSequence())) {
			caData = kubeYaml["clusters"][0]["cluster"]["certificate-authority-data"].as<std::string>();
			serverAddress = kubeYaml["clusters"][0]["cluster"]["server"].as<std::string>();
		}
		if ((kubeYaml["users"]) && (kubeYaml["users"].IsSequence())) {
			userToken = kubeYaml["users"][0]["user"]["token"].as<std::string>();
		}

		FileHandle configFile=makeTemporaryFile(".tmp_config_");
		{
			std::ofstream configStream(configFile);
			configStream << kubeconfig;
		}
		startReaper();
		auto result=runCommand("kubectl",
		  {"--kubeconfig="+configFile.path(),"get","serviceaccounts","-o=jsonpath={.items[*].metadata.name}"});
		stopReaper();
		std::string account;
		std::istringstream accounts(result.output);
		while(accounts >> account){
			if (account != "default") {
				break;
			}
		}
		namespaceName=account;
	}
	return kubeconfig;
}

std::string TestContext::getKubeNamespace() {
	return namespaceName;
}

std::string TestContext::getServerCAData() {
	return caData;
}

std::string TestContext::getUserToken() {
	return userToken;
}

std::string TestContext::getServerAddress() {
	return serverAddress;
}

std::string getPortalUserID(){
	std::string uid;
	std::string line;
	std::ifstream in("slate_portal_user");
	if (!in) {
		FAIL("Unable to read test slate_portal_user values");
	}
	while(std::getline(in,line)){
		if(!line.empty()){
			uid=line;
			break;
		}
	}
	return uid;
}

std::string getPortalToken(){
	std::string adminKey;
	std::string line;
	std::ifstream in("slate_portal_user");
	if (!in) {
		FAIL("Unable to read test slate_portal_user values");
	}
	while(std::getline(in,line)){
		if (!line.empty()) {
			adminKey = line;
		}
	}
	return adminKey;
}

std::string getSchemaDir(){
	std::string schemaDir="../resources/api_specification";
	fetchFromEnvironment("SLATE_SCHEMA_DIR",schemaDir);
	return schemaDir;
}

rapidjson::SchemaDocument loadSchema(const std::string& path){
	rapidjson::Document sd;
	std::ifstream schemaStream(path);
	if (!schemaStream) {
		throw std::runtime_error("Unable to read schema file " + path);
	}
	rapidjson::IStreamWrapper wrapper(schemaStream);
	sd.ParseStream(wrapper);
	return rapidjson::SchemaDocument(sd);
	//return rapidjson::SchemaValidator(schema);
}

const std::string currentAPIVersion="v1alpha3";

int main(int argc, char* argv[]){
	for(int i=1; i<argc; i++){
		std::string arg=argv[i];
		if(arg=="WORKING_DIRECTORY"){
			if(i+1>=argc){
				std::cerr << "WORKING_DIRECTORY not specified" << std::endl;
				return(1);
			}
			if(chdir(argv[i+1])!=0){
				std::cerr << "Failed to change working directory to " << argv[i+1] << std::endl;
				return(1);
			}
			i++;
		}
	}
	
	using AWSOptionsHandle=std::unique_ptr<Aws::SDKOptions,void(*)(Aws::SDKOptions*)>;
	AWSOptionsHandle awsOptions(new Aws::SDKOptions, 
	                            [](Aws::SDKOptions* awsOptions){
									Aws::ShutdownAPI(*awsOptions); 
								});
	Aws::InitAPI(*awsOptions);
	
	std::cout << "Running " << test_registry().size() << " tests" << std::endl;
	bool all_pass=true;
	size_t passes=0, failures=0;
	for(std::map<std::string,void(*)()>::const_iterator test=test_registry().begin();
		test!=test_registry().end(); test++){
		bool pass=false;
		std::cout << test->first << ": ";
		std::cout.flush();
		try{
			(test->second)();
			pass=true;
		}catch(test_exception& ex){
			std::cout << "FAIL\n " << ex.what() << std::endl;
		}catch(std::exception& ex){
			std::cout << "FAIL\n Exception: " << ex.what() << std::endl;
		}catch(...){
			std::cout << "FAIL\n Unknown object thrown" << std::endl;
		}
		if (pass) {
			std::cout << "PASS" << std::endl;
		}
		(pass?passes:failures)++;
		all_pass &= pass;
	}
	std::cout << passes << " test" << (passes != 1 ? "s" : "") << " pass"
	          << (passes != 1 ? "" : "es") << ", "
	          << failures << " fail" << (failures != 1 ? "" : "s") << std::endl;
	return(all_pass ? 0 : 1);
}
