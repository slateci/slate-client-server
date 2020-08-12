#include <algorithm>
#include <chrono>
#include <future>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "rapidjson/document.h"
#include "rapidjson/pointer.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "HTTPRequests.h"
#include "Utilities.h"

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
						std::cerr << "Error: Unable to parse '" << value << "' as an unsigned integer" << std::endl;
						throw;
					}
				}
			}
			return *this;
		}
	};
	
	std::string apiEndpoint;
	std::string apiToken;
	unsigned int concurrency;
	unsigned int iterations;
	std::string testMode;
	
	std::map<std::string,ParamRef> options;
	
	Configuration(int argc, char* argv[]):
	apiEndpoint("https://api.slateci.io"),
	apiToken(""),
	concurrency(64),
	iterations(32),
	options{
		{"apiEndpoint",apiEndpoint},
		{"apiToken",apiToken},
		{"concurrency",concurrency},
		{"iterations",iterations},
		{"mode",testMode},
	}
	{
		//check for environment variables
		for(auto& option : options)
			fetchFromEnvironment("SLATE_STRESS_"+option.first,option.second);
		
		//interpret command line arguments
		for(int i=1; i<argc; i++){
			std::string arg(argv[i]);
			if(arg.size()<=2 || arg[0]!='-' || arg[1]!='-'){
				std::cerr << "Error: Unknown argument ignored: '" << arg << '\'' << std::endl;
				continue;
			}
			auto eqPos=arg.find('=');
			std::string optName=arg.substr(2,eqPos-2);
			if(options.count(optName)){
				if(eqPos!=std::string::npos)
					options.find(optName)->second=arg.substr(eqPos+1);
				else{
					if(i==argc-1)
						throw std::runtime_error("Missing value after "+arg);
					i++;
					options.find(arg.substr(2))->second=argv[i];
				}
			}
			else if(optName=="config"){
				if(eqPos!=std::string::npos)
					parseFile({arg.substr(eqPos+1)});
				else{
					if(i==argc-1)
						throw std::runtime_error("Missing value after "+arg);
					i++;
					parseFile({argv[i]});
				}
			}
			else
				std::cerr << "Error: Unknown argument ignored: '" << arg << '\'' << std::endl;
		}
	}
	
	//attempt to read the last file in files, checking that it does not appear
	//previously
	void parseFile(const std::vector<std::string>& files){
		assert(!files.empty());
		if(std::find(files.begin(),files.end(),files.back())<(files.end()-1)){
			std::cerr << "Error: Configuration file loop: " << std::endl;
			for(const auto file : files)
				std::cerr << "  " << file << std::endl;
			throw std::runtime_error("Configuration parsing terminated");
		}
		std::ifstream infile(files.back());
		if(!infile)
			throw std::runtime_error("Unable to open " + files.back() + " for reading");
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
				std::cerr << "Error: " << files.back() << ':' << lineNumber 
				<< ": Unknown option ignored: '" << line << '\'' << std::endl;
			lineNumber++;
		}
	}
	
};

struct Timer{
public:
	Timer():t0(std::chrono::high_resolution_clock::now()),t1(t0){}
	void stop(){ t1=std::chrono::high_resolution_clock::now(); }
	double elapsed() const{
		std::chrono::high_resolution_clock::time_point end=
		(t1==t0 ? std::chrono::high_resolution_clock::now() : t1);
		return std::chrono::duration_cast<std::chrono::duration<double>>(end-t0).count();
	}
private:
	std::chrono::high_resolution_clock::time_point t0, t1;
};

struct Histogram{
public:
	Histogram(double min, double max, std::size_t bins):
	min(min),max(max),bins(bins),step((max-min)/bins),
	counts(bins,0),underflow(0),overflow(0){}
	
	void add(double val){
		if(val<min)
			underflow++;
		else if(val>max)
			overflow++;
		else{
			std::size_t idx=(val-min)/step;
			if(idx>=bins)
				idx=bins-1;
			counts[idx]++;
		}
	}
	
	std::ostream& print(std::ostream& os) const{
		if(underflow)
			os << "underflow: " << underflow << '\n';
		for(std::size_t i=0; i<bins; i++)
			os << i*step+min << ": " << counts[i] << '\n';
		if(overflow)
			os << "overflow: " << overflow << '\n';
		return os;
	}
	
	void operator+=(const Histogram& h){
		if(h.min!=min || h.max!=max || h.bins!=bins)
			throw std::runtime_error("Refusing to add incompatibly binned histograms");
		for(std::size_t i=0; i<bins; i++)
			counts[i]+=h.counts[i];
		underflow+=h.underflow;
		overflow+=h.overflow;
	}
	
private:
	double min;
	double max;
	std::size_t bins;
	double step;
	std::vector<unsigned int> counts;
	unsigned int underflow;
	unsigned int overflow;
};

std::ostream& operator<<(std::ostream& os, const Histogram& h){
	return h.print(os);
}

class Stressor{
public:
	Stressor(std::string endpoint, std::string token):
	endpoint(endpoint),token(token){}
protected:
	std::string endpoint;
	std::string token;
	
	std::string makeURL(const std::string& path, const std::string& query="") const{
		std::string url=endpoint+"/v1alpha3/"+path+"?token="+token;
		if(!query.empty())
			url+="&"+query;
		return url;
	}
};

class ClusterAccessStressor : protected Stressor{
public:
	ClusterAccessStressor(std::string endpoint, std::string token):
	Stressor(endpoint,token){}
	
	Histogram operator()(std::size_t iterations, std::string group){
		Histogram latencies(0,10,100);
		for(std::size_t i=0; i!=iterations; i++){
			try{
			//std::map<std::string,std::string> urlsToClustersAllowedGroups;
			rapidjson::Document accessRequest(rapidjson::kObjectType);
			
			Timer listTime;
			auto listResp=httpRequests::httpGet(makeURL("clusters"));
			listTime.stop();
			std::cout << "Got cluster list response after " << listTime.elapsed() << " seconds" << std::endl;
				latencies.add(listTime.elapsed());
			if(listResp.status==200){
				rapidjson::Document json;
				try{
					json.Parse(listResp.body.c_str());
					if(!json.HasMember("items") || !json["items"].IsArray())
						throw std::runtime_error("Cluster list response does not have expected structure");
					auto& requestAlloc=accessRequest.GetAllocator();
					for(const auto& cluster : json["items"].GetArray()){
						if(!cluster.HasMember("metadata") || !cluster["metadata"].IsObject()
						   || !cluster["metadata"].HasMember("name") || !cluster["metadata"]["name"].IsString())
							continue;
						const rapidjson::Value& name = cluster["metadata"]["name"];
						std::string requestURL="/v1alpha3/clusters/"+std::string(name.GetString())+"/allowed_groups/"+group+"?token="+token;
						rapidjson::Value request(rapidjson::kObjectType);
						request.AddMember("method","GET",requestAlloc);
						request.AddMember("body","",requestAlloc);
						accessRequest.AddMember(rapidjson::Value().SetString(requestURL,requestAlloc),request,requestAlloc);
						//urlsToClustersAllowedGroups.emplace(requestURL,name.GetString());
					}
				}catch(std::exception& ex){
					std::cerr << "Failure: Received malformed JSON as cluster list response: " << ex.what() << std::endl;
				}
			}
			else{
				std::cerr << "Failure: Got unexpected status for cluster list response: " << listResp.status << std::endl;
			}
			
			//----
			
			rapidjson::StringBuffer buffer;
			rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
			accessRequest.Accept(writer);
			Timer accessTime;
			auto accessResp=httpRequests::httpPost(makeURL("multiplex"),buffer.GetString());
			accessTime.stop();
			std::cout << "Got cluster access response after " << accessTime.elapsed() << " seconds" << std::endl;
				latencies.add(accessTime.elapsed());
			if(accessResp.status==200){
				rapidjson::Document json;
				try{
					json.Parse(accessResp.body.c_str());
				}catch(std::exception& ex){
					std::cerr << "Failure: Received malformed JSON as cluster access response: " << ex.what() << std::endl;
				}
			}
			else{
				std::cerr << "Failure: Got unexpected status for cluster access response: " << listResp.status << std::endl;
			}
			}catch(std::exception& ex){
				std::cerr << "Exception: " << ex.what() << std::endl;
			}
			
			//std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		return latencies;
	}
};

Histogram stressClusterAccessPermissions(const Configuration& config){
	std::vector<std::string> prodGroups;
	{ //look up all groups
		auto listResp=httpRequests::httpGet(config.apiEndpoint+"/v1alpha3/groups?token="+config.apiToken);
		rapidjson::Document json;
		try{
			json.Parse(listResp.body.c_str());
			if(!json.HasMember("items") || !json["items"].IsArray())
				throw std::runtime_error("Group list response does not have expected structure");
			for(const auto& item : json["items"].GetArray()){
				if(!item.HasMember("metadata") || !item["metadata"].IsObject() ||
				   !item["metadata"].HasMember("name") || !item["metadata"]["name"].IsString())
					continue;
				prodGroups.push_back(item["metadata"]["name"].GetString());
			}
		}catch(std::exception& ex){
			throw std::runtime_error("Failed to list groups: "+std::string(ex.what()));
		}
	}
	
	Histogram latencies(0,10,100);
	std::vector<std::future<Histogram>> results;
	for(unsigned int i=0; i<config.concurrency; i++)
		results.emplace_back(std::async(std::launch::async,
										ClusterAccessStressor(config.apiEndpoint,config.apiToken),config.iterations,prodGroups[i%prodGroups.size()]));
	for(unsigned int i=0; i<config.concurrency; i++){
		Histogram result=results[i].get();
		latencies+=result;
	}
	return latencies;
}

class ClusterPingStressor : protected Stressor{
public:
	ClusterPingStressor(std::string endpoint, std::string token):
	Stressor(endpoint,token){}
	
	Histogram operator()(std::size_t iterations){
		Histogram latencies(0,10,100);
		for(std::size_t i=0; i!=iterations; i++){
			try{
				rapidjson::Document accessRequest(rapidjson::kObjectType);
				
				Timer listTime;
				auto listResp=httpRequests::httpGet(makeURL("clusters"));
				listTime.stop();
				std::cout << "Got cluster list response after " << listTime.elapsed() << " seconds" << std::endl;
				latencies.add(listTime.elapsed());
				if(listResp.status==200){
					rapidjson::Document json;
					try{
						json.Parse(listResp.body.c_str());
						if(!json.HasMember("items") || !json["items"].IsArray())
							throw std::runtime_error("Cluster list response does not have expected structure");
						auto& requestAlloc=accessRequest.GetAllocator();
						for(const auto& cluster : json["items"].GetArray()){
							if(!cluster.HasMember("metadata") || !cluster["metadata"].IsObject()
							   || !cluster["metadata"].HasMember("name") || !cluster["metadata"]["name"].IsString())
								continue;
							const rapidjson::Value& name = cluster["metadata"]["name"];
							std::string requestURL="/v1alpha3/clusters/"+std::string(name.GetString())+"/ping?token="+token;
							rapidjson::Value request(rapidjson::kObjectType);
							request.AddMember("method","GET",requestAlloc);
							request.AddMember("body","",requestAlloc);
							accessRequest.AddMember(rapidjson::Value().SetString(requestURL,requestAlloc),request,requestAlloc);
						}
					}catch(std::exception& ex){
						std::cerr << "Failure: Received malformed JSON as cluster list response: " << ex.what() << std::endl;
					}
				}
				else{
					std::cerr << "Failure: Got unexpected status for cluster list response: " << listResp.status << std::endl;
				}
				
				//----
				
				rapidjson::StringBuffer buffer;
				rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
				accessRequest.Accept(writer);
				Timer accessTime;
				auto accessResp=httpRequests::httpPost(makeURL("multiplex"),buffer.GetString());
				accessTime.stop();
				std::cout << "Got cluster ping response after " << accessTime.elapsed() << " seconds" << std::endl;
				latencies.add(accessTime.elapsed());
				if(accessResp.status==200){
					rapidjson::Document json;
					try{
						json.Parse(accessResp.body.c_str());
					}catch(std::exception& ex){
						std::cerr << "Failure: Received malformed JSON as cluster ping response: " << ex.what() << std::endl;
					}
				}
				else{
					std::cerr << "Failure: Got unexpected status for cluster ping response: " << listResp.status << std::endl;
				}
			}catch(std::exception& ex){
				std::cerr << "Exception: " << ex.what() << std::endl;
			}
		}
		return latencies;
	}
};

Histogram stressClusterPing(const Configuration& config){
	Histogram latencies(0,10,100);
	std::vector<std::future<Histogram>> results;
	for(unsigned int i=0; i<config.concurrency; i++)
		results.emplace_back(std::async(std::launch::async,
										ClusterPingStressor(config.apiEndpoint,config.apiToken),config.iterations));
	for(unsigned int i=0; i<config.concurrency; i++){
		Histogram result=results[i].get();
		latencies+=result;
	}
	return latencies;
}

int main(int argc, char* argv[]){
	Configuration config(argc, argv);
	if(config.apiToken.empty()){
		std::cerr << "Must specify an API token" << std::endl;
		return 1;
	}
	
	Histogram latencies(0,0,0);
	
	if(config.testMode=="clusterAccessLookup")
		latencies=stressClusterAccessPermissions(config);
	else if(config.testMode=="clusterPing")
		latencies=stressClusterPing(config);
	else{
		std::cerr << "Unknown test mode" << std::endl;
		return 1;
	}
	std::cout << "Response latencies:\n" << latencies << std::endl;
	return 0;
}
