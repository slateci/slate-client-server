#include <array>
#include <fstream>
#include <iostream>
#include <thread>
#include <stdexcept>

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include <rapidjson/istreamwrapper.h>
#include "rapidjson/writer.h"

#include "test.h"

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
	if(message.empty())
		ss << "Assertion failed: \n";
	else
		ss << message << ": \n";
	ss << '\t' << criterion << std::endl;
	throw test_exception(ss.str());
}

void emit_schema_error(const std::string& file, size_t line,
                       const rapidjson::SchemaValidator& validator, 
                       const std::string& message){
	
	rapidjson::StringBuffer sb;
	rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
	validator.GetError().Accept(writer);
	
	std::ostringstream ss;
	ss << file << ':' << line << "\n\t";
	if(message.empty())
		ss << "Schema validation failed: ";
	else
		ss << message << ": ";
	ss << sb.GetString();
	throw test_exception(ss.str());
}

std::map<std::string,void(*)()>&
test_registry()
{
	static std::map<std::string,void(*)()> *registry = new std::map<std::string,void(*)()>;
	return *registry;
}

void TestContext::waitServerReady(){
	//watch the server's output until it indicates that it has its database 
	//connection up and running
	std::string line;
	while(getline(server.getStdout(),line)){
		std::cout << line << std::endl;
		if(line.find("Database client ready")!=std::string::npos){
			break;
		}
	}
	if(server.getStdout().eof())
		throw std::runtime_error("Child process output ended");
	//wait just until the server begins responding to requests
	while(true){
		try{
			auto resp=httpRequests::httpGet(getAPIServerURL()+"/v1alpha1/stats");
			break; //if we got any reponse, assume that we're done
		}catch(std::exception& ex){
			//std::cout << "Exception: " << ex.what() << std::endl;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
	std::cout << "Server should be ready" << std::endl;
}

TestContext::TestContext(){
	using namespace httpRequests;
	
	auto dbResp=httpGet("http://localhost:52000/dynamo/create");
	ENSURE_EQUAL(dbResp.status,200);
	dbPort=dbResp.body;
	auto portResp=httpGet("http://localhost:52000/port/allocate");
	ENSURE_EQUAL(portResp.status,200);
	serverPort=portResp.body;
	
	server=startProcessAsync("./slate-service",
	                         {"--awsEndpoint","localhost:"+dbPort,"--port",serverPort});
	waitServerReady();
}

TestContext::~TestContext(){
	httpRequests::httpDelete("http://localhost:52000/port/"+serverPort);
	httpRequests::httpDelete("http://localhost:52000/dynamo/"+dbPort);
	server.kill();
	std::cout << "Server log: " << std::endl;
	while(!server.getStdout().eof() && !server.getStderr().eof()){
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
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
}

std::string TestContext::getAPIServerURL() const{
	return "http://localhost:"+serverPort;
}

std::string getPortalUserID(){
	std::string uid;
	std::string line;
	std::ifstream in("slate_portal_user");
	if(!in)
		FAIL("Unable to read test slate_portal_user values");
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
	if(!in)
		FAIL("Unable to read test slate_portal_user values");
	while(std::getline(in,line)){
		if(!line.empty())
			adminKey=line;
	}
	return adminKey;
}

rapidjson::SchemaDocument loadSchema(const std::string& path){
	rapidjson::Document sd;
	std::ifstream schemaStream(path);
	if(!schemaStream)
		throw std::runtime_error("Unable to read schema file "+path);
	rapidjson::IStreamWrapper wrapper(schemaStream);
	sd.ParseStream(wrapper);
	return rapidjson::SchemaDocument(sd);
	//return rapidjson::SchemaValidator(schema);
}

std::string getKubeConfig(){
	char *dir = getenv("HOME");
	std::string home(dir);
	std::string path = home + "/.kube/config";
	
	std::ifstream kubetest(path);
	std::stringstream buffer;
	buffer << kubetest.rdbuf();
	return buffer.str();
}


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
		if(pass)
			std::cout << "PASS" << std::endl;
		(pass?passes:failures)++;
		all_pass &= pass;
	}
	std::cout << passes << " test" << (passes!=1?"s":"") << " pass"
	<< (passes!=1?"":"es") << ", "
	<< failures << " fail" << (failures!=1?"":"s") << std::endl;
	return(all_pass ? 0 : 1);
}
