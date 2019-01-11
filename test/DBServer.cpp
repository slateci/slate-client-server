//In order to run units tests in clean, controlled environments, it is desirable 
//that each should use a distinct database instance. However, database instances 
//must be assigned port numbers, and these must not collide, so some central 
//authority must coordinate this. This program provides that service by running
//a server on a known port (52000), creating database instances on demand and
//returning the ports on which they are listening. 

#include <cstdlib>
#include <cstdio> //remove
#include <cerrno>
#include <chrono>
#include <map>
#include <random>

#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <crow.h>
#include <libcuckoo/cuckoohash_map.hh>

#include "Process.h"
#include "Archive.h"
#include "FileHandle.h"

bool fetchFromEnvironment(const std::string& name, std::string& target){
	char* val=getenv(name.c_str());
	if(val){
		target=val;
		return true;
	}
	return false;
}

std::string dynamoJar="DynamoDBLocal.jar";
std::string dynamoLibs="DynamoDBLocal_lib";

ProcessHandle launchDynamo(unsigned int port){
	auto proc=
		startProcessAsync("java",{
			"-Djava.library.path="+dynamoLibs,
			"-jar",
			dynamoJar,
			"-port",
			std::to_string(port),
			"-inMemory"
		},{},ForkCallbacks{},true);
	
	return proc;
}

ProcessHandle launchHelmServer(){
	auto proc=
		startProcessAsync("helm",{
			"serve"
		},{},ForkCallbacks{},true);
	
	return proc;
}

std::string allocateNamespace(const unsigned int index, const std::string tmpDir){
	std::string name="test-"+std::to_string(index);
	
	FileHandle configPath=makeTemporaryFile(tmpDir+"/config_");
	std::ofstream configFile(configPath);
	configFile << R"(apiVersion: nrp-nautilus.io/v1alpha1
kind: Cluster
metadata: 
  name: )" << name << std::endl;
	
	auto res=runCommand("kubectl",
	  {"create","-f",configPath.path()});
	if(res.status){
		std::cout << "Namespace " << index  << ": Cluster/namespace creation failed: " << res.error << std::endl;
		return "";
	}
	
	//wait for the corresponding namespace to be ready
	while(true){
		res=runCommand("kubectl",{"get","namespace",name,"-o","jsonpath={.status.phase}"});
		if(res.status==0 && res.output=="Active")
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	
	std::cout << " Getting serviceaccount for namespace " << index << std::endl;
	res=runCommand("kubectl",
	  {"get","serviceaccount",name,"-n",name,"-o","jsonpath={.secrets[].name}"});
	if(res.status){
		std::cout << "Namespace " << index  << ": Finding ServiceAccount failed: " << res.error << std::endl;
		return "";
	}
	std::string credName=res.output;
	
	std::cout << " Getting CA data for namespace " << index << std::endl;
	res=runCommand("kubectl",
	  {"get","secret",credName,"-n",name,"-o","jsonpath={.data.ca\\.crt}"});
	if(res.status){
		std::cout << "Namespace " << index  << ": Extracting CA data failed: " << res.error << std::endl;
		return "";
	}
	std::string caData=res.output;
	
	std::cout << " Getting cluster-info for namespace " << index << std::endl;
	res=runCommand("kubectl",{"cluster-info"});
	if(res.status){
		std::cout << "Namespace " << index  << ": Getting cluster info failed: " << res.error << std::endl;
		return "";
	}
	//sift out the first URL
	auto startPos=res.output.find("http");
	if(startPos==std::string::npos){
		std::cout << "Namespace " << index  << ": Could not find 'http' in cluster info" << std::endl;
		return "";
	}
	auto endPos=res.output.find((char)0x1B,startPos);
	if(endPos==std::string::npos){
		std::cout << "Namespace " << index  << ": Could not find '0x1B' in cluster info" << std::endl;
		return "";
	}
	std::string server=res.output.substr(startPos,endPos-startPos);
	
	std::cout << " Getting token for namespace " << index << std::endl;
	res=runCommand("kubectl",{"get","secret","-n",name,credName,"-o","jsonpath={.data.token}"});
	if(res.status){
		std::cout << "Namespace " << index  << ": Extracting token failed: " << res.error << std::endl;
		return "";
	}
	std::string encodedToken=res.output;

	std::cout << " Decoding token for namespace " << index << std::endl;
	std::string token=decodeBase64(encodedToken);
	
	std::ostringstream os;
	os << R"(apiVersion: v1
clusters:
- cluster:
    certificate-authority-data: )"
	  << caData << '\n'
	  << "    server: " << server << '\n'
	  << R"(  name: cluster
contexts:
- context:
    cluster: cluster
    namespace: )" << name << '\n'
	  << "    user: " << name << '\n'
	  << R"(  name: cluster
current-context: cluster
kind: Config
preferences: {}
users:
- name: )" << name << '\n'
	  << R"(  user:
    token: )" << token << '\n';
	
	std::cout << " Done creating namespace " << index << std::endl;
	return os.str();
}

int main(){
	startReaper();
	//figure out where dynamo is
	fetchFromEnvironment("DYNAMODB_JAR",dynamoJar);
	fetchFromEnvironment("DYNAMODB_LIB",dynamoLibs);
	
	struct stat info;
	int err=stat(dynamoJar.c_str(),&info);
	if(err){
		err=errno;
		if(err!=ENOENT)
			std::cerr << "Unable to stat DynamoDBLocal.jar at "+dynamoJar+"; error "+std::to_string(err) << std::endl;
		else
			std::cerr << "Unable to stat DynamoDBLocal.jar; "+dynamoJar+" does not exist" << std::endl;
		return(1);
	}
	err=stat(dynamoLibs.c_str(),&info);
	if(err){
		err=errno;
		if(err!=ENOENT)
			std::cerr << "Unable to stat DynamoDBLocal_lib at "+dynamoLibs+"; error "+std::to_string(err) << std::endl;
		else
			std::cerr << "Unable to stat DynamoDBLocal_lib; "+dynamoLibs+" does not exist" << std::endl;
		return(1);
	}
	
	{ //make sure kubernetes is in the right state for federation
		std::cout << "Installing federation role" << std::endl;
		auto res=runCommand("kubectl",
		  {"apply","-f","https://gitlab.com/ucsd-prp/nrp-controller/raw/master/federation-role.yaml"});
		if(res.status){
			std::cerr << "Unable to deploy federation role: " << res.error << std::endl;
			return(1);
		}
		
		std::cout << "Installing federation controller" << std::endl;
		res=runCommand("kubectl",
		  {"apply","-f","https://gitlab.com/ucsd-prp/nrp-controller/raw/master/deploy.yaml"});
		if(res.status){
			std::cerr << "Unable to deploy federation controller: " << res.error << std::endl;
			return(1);
		}
		std::cout << "Done initializing kubernetes" << std::endl;
	}
	
	{ //demonize
		auto group=setsid();
		
		for(int i = 0; i<FOPEN_MAX; i++)
			close(i);
		//redirect fds 0,1,2 to /dev/null
		open("/dev/null", O_RDWR); //stdin
		dup(0); //stdout
		dup(0); //stderr
	}
	
	std::ofstream logfile;
	logfile.open("DBServer.log");
	auto old_cout_buf=std::cout.rdbuf();
	std::cout.rdbuf(logfile.rdbuf());
	
	cuckoohash_map<unsigned int, ProcessHandle> soManyDynamos;
	std::mutex helmLock;
	std::mutex launcherLock;
	ProcessHandle helmHandle;
	const unsigned int minPort=52001, maxPort=53000;
	unsigned int namespaceIndex=0;
	auto configTmpDir=makeTemporaryDir(".tmp_cluster_configs");
	
	auto allocatePort=[&]()->unsigned int{
		///insert an empty handle into the table to reserve a port number
		unsigned int port=minPort;
		while(true){ //TODO: maybe at some point stop looping
			bool success=true;
			soManyDynamos.upsert(port,[&](const ProcessHandle&){
				success=false;
			},ProcessHandle{});
			if(success)
				break;
			port++;
			if(port==maxPort)
				port=minPort;
		}
		return port;
	};
	
	auto runDynamo=[&](unsigned int port)->ProcessHandle{
		std::lock_guard<std::mutex> lock(launcherLock);
		return launchDynamo(port);
	};
	
	auto startHelm=[&](){
		std::cout << "Got request to start helm" << std::endl;
		std::lock_guard<std::mutex> lock(helmLock);
		//at this point we have ownership to either create or use the process 
		//handle for helm
		if(helmHandle)
			return crow::response(200); //already good, release lock and exit
		//otherwise, create child process
		helmHandle=launchHelmServer();
		return crow::response(200);
	};
	
	auto stopHelm=[&](){
		std::cout << "Got request to stop helm" << std::endl;
		//need ownership
		std::lock_guard<std::mutex> lock(helmLock);
		helmHandle=ProcessHandle(); //destroy by replacing with empty handle
		return 200;
	};
	
	auto allocateNamespace=[&](){
		std::cout << "Got request for a namespace" << std::endl;
		std::lock_guard<std::mutex> lock(launcherLock);
		std::string config=::allocateNamespace(namespaceIndex,configTmpDir);
		namespaceIndex++;
		return crow::response(200,config);
	};
	
	auto getPort=[&]{
		auto port=allocatePort();
		return std::to_string(port);
	};
	
	auto freePort=[&](unsigned int port){
		soManyDynamos.erase(port);
		return crow::response(200);
	};
	
	auto create=[&]{
		std::cout << "Got request to start dynamo" << std::endl;
		//if(launcher.done())
		//	return crow::response(500,"Child launcher process has ended");
		auto port=allocatePort();
		//at this point we own this port; start the instance
		ProcessHandle dyn=runDynamo(port);
		if(!dyn){
			freePort(port);
			return crow::response(500,"Unable to start Dynamo");
		}
		std::cout << "Started child process " << dyn.getPid() << std::endl;
		//insert the handle into the table, replacing we dummy put there earlier
		soManyDynamos.upsert(port,[&](ProcessHandle& dummy){
			dummy=std::move(dyn);
		},ProcessHandle{});
		return crow::response(200,std::to_string(port));
	};
	
	auto remove=[&](unsigned int port){
		std::cout << "Got request to stop dynamo on port " << port << std::endl;
		soManyDynamos.erase(port);
		std::cout << "Erased process handle for port " << port << std::endl;
		return crow::response(200);
	};
	
	auto stop=[](){
		std::cout << "Got request to stop dynamo server" << std::endl;
		kill(getpid(),SIGTERM);
		return crow::response(200);
	};
	
	crow::SimpleApp server;
	
	CROW_ROUTE(server, "/port/allocate").methods("GET"_method)(getPort);
	CROW_ROUTE(server, "/port/<int>").methods("DELETE"_method)(freePort);
	CROW_ROUTE(server, "/dynamo/create").methods("GET"_method)(create);
	CROW_ROUTE(server, "/dynamo/<int>").methods("DELETE"_method)(remove);
	CROW_ROUTE(server, "/helm").methods("GET"_method)(startHelm);
	CROW_ROUTE(server, "/helm").methods("DELETE"_method)(stopHelm);
	CROW_ROUTE(server, "/namespace").methods("GET"_method)(allocateNamespace);
	CROW_ROUTE(server, "/stop").methods("PUT"_method)(stop);

	std::cout << "Starting http server" << std::endl;
	{
		std::ofstream touch(".test_server_ready");
	}
	server.loglevel(crow::LogLevel::Warning);
	server.port(52000).multithreaded().run();
	{
		::remove(".test_server_ready");
	}
	std::cout.rdbuf(old_cout_buf);
}