#include <fstream>
#include <set>
#include <stdexcept>

#define CROW_ENABLE_SSL
#include <crow.h>

#include <yaml-cpp/exceptions.h>
#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/impl.h>
#include "yaml-cpp/node/convert.h"
#include "yaml-cpp/node/detail/impl.h"
#include <yaml-cpp/node/parse.h>

struct Configuration{
	struct ParamRef{
		enum Type{String,Bool} type;
		union{
			std::reference_wrapper<std::string> s;
			std::reference_wrapper<bool> b;
		};
		ParamRef(std::string& s):type(String),s(s){}
		ParamRef(bool& b):type(Bool),b(b){}
		ParamRef(const ParamRef& p):type(p.type){
			switch(type){
				case String: s=p.s; break;
				case Bool: b=p.b; break;
			}
		}
		
		ParamRef& operator=(const std::string& value){
			switch(type){
				case String:
					s.get()=value;
					break;
				case Bool:
				{
					if (value == "true" || value == "True" || value == "1") {
						b.get() = true;
					} else {
						b.get() = false;
					}
					break;
				}
			}
			return *this;
		}
	};

	std::string pathPrefix;
	std::string portString;
	
	std::map<std::string,ParamRef> options;
	
	Configuration(int argc, char* argv[]):
	pathPrefix(""),
	portString("8879"),
	options{
		{"pathPrefix",pathPrefix},
		{"port",portString},
	}
	{
		//interpret command line arguments
		for(int i=1; i<argc; i++){
			std::string arg(argv[i]);
			if(arg.size()<=2 || arg[0]!='-' || arg[1]!='-'){
				std::cerr << "Unknown argument ignored: '" << arg << '\'' << std::endl;
				continue;
			}
			auto eqPos=arg.find('=');
			std::string optName=arg.substr(2,eqPos-2);
			if(options.count(optName)){
				if (eqPos != std::string::npos) {
					options.find(optName)->second = arg.substr(eqPos + 1);
				} else {
					if (i == argc - 1) {
						throw std::runtime_error("Missing value after " + arg);
					}
					i++;
					options.find(arg.substr(2))->second = argv[i];
				}
			}
			else if (optName == "config") {
				if (eqPos != std::string::npos) {
					parseFile({arg.substr(eqPos + 1)});
				} else {
					if (i == argc - 1) {
						throw std::runtime_error("Missing value after " + arg);
					}
					i++;
					parseFile({argv[i]});
				}
			} else {
				std::cerr << "Unknown argument ignored: '" << arg << '\'' << std::endl;
			}
		}
	}
	
	//attempt to read the last file in files, checking that it does not appear
	//previously
	void parseFile(const std::vector<std::string>& files){
		assert(!files.empty());
		if(std::find(files.begin(),files.end(),files.back())<(files.end()-1)){
			std::cerr << "Configuration file loop: \n";
			for (const auto file: files) {
				std::cerr << "  " << file << '\n';
			}
			throw std::runtime_error("Configuration parsing terminated");
		}
		std::ifstream infile(files.back());
		if (!infile) {
			throw std::runtime_error("Unable to open " + files.back() + " for reading");
		}
		std::string line;
		unsigned int lineNumber=1;
		while(std::getline(infile,line)){
			auto eqPos=line.find('=');
			std::string optName=line.substr(0,eqPos);
			std::string value=line.substr(eqPos+1);
			if (options.count(optName)) {
				options.find(optName)->second = value;
			} else if (optName == "config") {
				auto newFiles = files;
				newFiles.push_back(value);
				parseFile(newFiles);
			} else {
				std::cerr << files.back() << ':' << lineNumber
					  << ": Unknown option ignored: '" << line << '\'' << std::endl;
			}
			lineNumber++;
		}
	}
};

int main(int argc, char* argv[]){
	Configuration config(argc, argv);
	unsigned int port=0;
	{
		std::istringstream is(config.portString);
		is >> port;
		if(!port || is.fail()){
			std::cerr << "Unable to parse \"" << config.portString 
			  << "\" as a valid port number" << std::endl;
			return 1;
		}
	}

	auto readFile=[&](const std::string name){
		std::ifstream file(config.pathPrefix+name);
		if (!file) {
			throw std::runtime_error("Unable to read " + config.pathPrefix + name);
		}
		file.seekg(0,std::ios_base::end);
		std::size_t fileLen=file.tellg();
		file.seekg(0,std::ios_base::beg);
		std::string data;
		data.resize(fileLen);
		file.read(&data[0],fileLen);
		return data;
	};

	std::string indexData=readFile("index.yaml");
	std::set<std::string> charts;
	{
		std::vector<YAML::Node> parsedIndex=YAML::LoadAll(indexData);
		for(const auto& document : parsedIndex){
			if(document.IsMap() && document["entries"] && document["entries"].IsMap()){
				for(const auto& chart : document["entries"]){
					if (!chart.second.IsSequence()) {
						continue;
					}
					for(const auto& entry : chart.second){
						if (!entry.IsMap()) {
							continue;
						}
						if (!entry["urls"] || !entry["urls"].IsSequence()) {
							continue;
						}
						for (const auto &url: entry["urls"]) {
							charts.insert(url.as<std::string>());
						}
					}
				}
			}
		}
	}
	for (const auto &chart: charts) {
		std::cout << "chart: " << chart << std::endl;
	}
	
	auto sendIndex=[&](const crow::request& req){ return crow::response(indexData); };
	auto sendChart=[&](const crow::request& req, const std::string chartPath){
		if (!charts.count(chartPath)) {
			return crow::response(404);
		}
		try{
			return crow::response(readFile(chartPath));
		}catch(...){
			return crow::response(500);
		}
	};
	
	crow::SimpleApp server;
	CROW_ROUTE(server, "/").methods("GET"_method)(sendIndex);
	CROW_ROUTE(server, "/index.yaml").methods("GET"_method)(sendIndex);
	CROW_ROUTE(server, "/<path>").methods("GET"_method)(sendChart);
	
	server.loglevel(crow::LogLevel::Warning);
	server.port(port).run();
}