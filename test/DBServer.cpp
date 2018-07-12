//In order to run units tests in clean, controlled environments, it is desirable 
//that each should use a distinct database instance. However, database instances 
//must be assigned port numbers, and these must not collide, so some central 
//authority must coordinate this. This program provides that service by running
//a server on a known port (52000), creating database instances on demand and
//returning the ports on which they are listening. 

#include <cstdlib>
#include <map>
#include <random>

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#define BOOST_COROUTINES_NO_DEPRECATION_WARNING
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <crow.h>
#include <libcuckoo/cuckoohash_map.hh>

#include "Process.h"

bool fetchFromEnvironment(const std::string& name, std::string& target){
	char* val=getenv(name.c_str());
	if(val){
		target=val;
		return true;
	}
	return false;
}

//ProcessHandle runDynamo(unsigned int port){
//	std::string dynamoJar="DynamoDBLocal.jar";
//	std::string dynamoLibs="DynamoDBLocal_lib";
//	fetchFromEnvironment("DYNAMODB_JAR",dynamoJar);
//	fetchFromEnvironment("DYNAMODB_LIB",dynamoLibs);
//	/*std::ostringstream command;
//	command << "java -Djava.library.path=" << dynamoLibs 
//	<< " -jar " << dynamoJar << " -port " << port 
//	<< " -inMemory -delayTransientStatuses";
//	std::cout << "Starting Dynamo on port " << port << std::endl;
//	pid_t child=fork();
//	if(child<0){ //fork failed
//		auto err=errno;
//		std::cerr << "Failed to start child process: Error " << err << std::endl;
//		return ProcessHandle{};
//	}
//	if(!child){ //if we don't know who the child is, it is us
//		//be the child process
//		execl("/bin/sh","sh","-c",command.str().c_str(),NULL);
//	}
//	std::cout << "Started child process " << child << std::endl;
//	return ProcessHandle(child);*/
//	return startProcessAsync("java",{
//		"-Djava.library.path="+dynamoLibs,
//		"-jar",
//		dynamoJar,
//		"-port",
//		std::to_string(port),
//		"-inMemory"
//	});
//}

class DynamoLauncher{
public:
	DynamoLauncher(boost::asio::io_service& io_service,
			 boost::asio::local::datagram_protocol::socket& input_socket,
				   boost::asio::local::datagram_protocol::socket& output_socket)
    : io_service(io_service),
	input_socket(input_socket),
	output_socket(output_socket)
	{}
	
	void operator()(boost::asio::yield_context yield){
		std::vector<char> buffer;
		while(true){
			// Wait for server to write data.
			input_socket.async_receive(boost::asio::null_buffers(), yield);
			
			// Resize buffer and read all data.
			buffer.resize(input_socket.available());
			input_socket.receive(boost::asio::buffer(buffer));
			
			struct ASIOForkCallbacks : public ForkCallbacks{
				boost::asio::io_service& io_service;
				ASIOForkCallbacks(boost::asio::io_service& ios):io_service(ios){}
				void beforeFork() override{
					io_service.notify_fork(boost::asio::io_service::fork_prepare);
				}
				void inChild() override{
					io_service.notify_fork(boost::asio::io_service::fork_child);
				}
				void inParent() override{
					io_service.notify_fork(boost::asio::io_service::fork_parent);
				}
			};
			
			std::string dynamoJar="DynamoDBLocal.jar";
			std::string dynamoLibs="DynamoDBLocal_lib";
			fetchFromEnvironment("DYNAMODB_JAR",dynamoJar);
			fetchFromEnvironment("DYNAMODB_LIB",dynamoLibs);
			
			std::string portStr;
			portStr.assign(buffer.begin(), buffer.end());
			//std::cout << getpid() << " Got port " << command << std::endl;
			unsigned int port=std::stoul(portStr);
			
			auto proc=
			startProcessAsync("java",{
				"-Djava.library.path="+dynamoLibs,
				"-jar",
				dynamoJar,
				"-port",
				std::to_string(port),
				"-inMemory"
			},ASIOForkCallbacks{io_service},true);
			//send the pid of the new process back to our parent
			output_socket.send(boost::asio::buffer(std::to_string(proc.getPid())));
			//give up responsibility for stopping the child process
			proc.detach();
		}
	}
	
private:
	boost::asio::io_service& io_service;
	boost::asio::local::datagram_protocol::socket& input_socket;
	boost::asio::local::datagram_protocol::socket& output_socket;
};

int main(){
	{ //demonize
		auto group=setsid();
		
		for(int i = 0; i<OPEN_MAX; i++)
			close(i);
		//redirect fds 0,1,2 to /dev/null
		open("/dev/null", O_RDWR); //stdin
		dup(0); //stdout
		dup(0); //stderr
	}
	
	boost::asio::io_service io_service;
	//create a set of connected sockets for inter-process communication
	boost::asio::local::datagram_protocol::socket parent_output_socket(io_service);
	boost::asio::local::datagram_protocol::socket child_input_socket(io_service);
	boost::asio::local::connect_pair(parent_output_socket, child_input_socket);
	
	boost::asio::local::datagram_protocol::socket parent_input_socket(io_service);
	boost::asio::local::datagram_protocol::socket child_output_socket(io_service);
	boost::asio::local::connect_pair(child_output_socket, parent_input_socket);
	
	io_service.notify_fork(boost::asio::io_service::fork_prepare);
	int child=fork();
	if(child<0){
		std::cerr << "fork failed: Error " << child;
		return 1;
	}
	if(child==0){
		io_service.notify_fork(boost::asio::io_service::fork_child);
		parent_input_socket.close();
		parent_output_socket.close();
		startReaper();
		boost::asio::spawn(io_service,
						   DynamoLauncher(io_service, child_input_socket, child_output_socket));
		io_service.run();
		return 0;
	}
	ProcessHandle launcher(child);
	//else still the parent process
	io_service.notify_fork(boost::asio::io_service::fork_parent);
	child_input_socket.close();
	child_output_socket.close();
	
	cuckoohash_map<unsigned int, ProcessHandle> soManyDynamos;
	const unsigned int minPort=52001, maxPort=53000;
	
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
		parent_output_socket.send(boost::asio::buffer(std::to_string(port)));
		//should be easily large enough to hold the string representation of any pid
		std::vector<char> buffer(128,'\0');
		parent_input_socket.receive(boost::asio::buffer(buffer));
		return ProcessHandle(std::stoul(buffer.data()));
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
	CROW_ROUTE(server, "/stop").methods("PUT"_method)(stop);
	
	server.loglevel(crow::LogLevel::Warning);
	server.port(52000).run();
}