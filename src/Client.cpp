#include "Client.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
	
bool fetchFromEnvironment(const std::string& name, std::string& target){
	char* val=getenv(name.c_str());
	if(val){
		target=val;
		return true;
	}
	return false;
}

namespace{
///Get the path to the user's home directory
///\return home directory path, with a trailing slash
std::string getHomeDirectory(){
	std::string path;
	fetchFromEnvironment("HOME",path);
	if(path.empty())
		throw std::runtime_error("Unable to loacte home directory");
	if(path.back()!='/')
		path+='/';
	return path;
}

enum class PermState{
	VALID,
	INVALID,
	DOES_NOT_EXIST //This really is FILE_NOT_FOUND
};

///Ensure that the given path is readable only by the owner
PermState checkPermissions(const std::string& path){
	struct stat data;
	int err=stat(path.c_str(),&data);
	if(err!=0){
		err=errno;
		if(err==ENOENT)
			return PermState::DOES_NOT_EXIST;
		//TODO: more detail on what failed?
		throw std::runtime_error("Unable to stat "+path);
	}
	//check that the current user is actually the file's owner
	if(data.st_uid!=getuid())
		return PermState::INVALID;
	return((data.st_mode&((1<<9)-1))==0600 ? PermState::VALID : PermState::INVALID);
}
	
//assumes that an introductory message has already been printed, without a newline
//attmepts to extract a JSON error message and prints it if successful
//always prints a conclusing newline.
void showError(const std::string& maybeJSON){
	try{
		rapidjson::Document resultJSON;
		resultJSON.Parse(maybeJSON.c_str());
		if(resultJSON.IsObject() && resultJSON.HasMember("message"))
			std::cout << ": " << resultJSON["message"].GetString();
		else if(!maybeJSON.empty())
			std::cout << ": " << maybeJSON;
		else
			std::cout << ": (empty response)";
	}catch(...){}
	std::cout << std::endl;
}
	
} //anonymous namespace
	
std::string Client::underline(std::string s) const{
	if(useANSICodes)
		return("\x1B[4m"+s+"\x1B[24m");
	return s;
}
std::string Client::bold(std::string s) const{
	if(useANSICodes)
		return("\x1B[1m"+s+"\x1B[22m");
	return s;
}
	
std::string Client::formatTable(const std::vector<std::vector<std::string>>& items,
                                const std::vector<columnSpec>& columns) const{
	//try to determine to desired minimum width for every column
	//this will give wrong answers for multi-byte unicode sequences
	std::vector<std::size_t> minColumnWidths;
	for(std::size_t i=0; i<items.size(); i++){
		if(items[i].size()>minColumnWidths.size())
			minColumnWidths.resize(items[i].size(),0);
		for(std::size_t j=0; j<items[i].size(); j++)
			minColumnWidths[j]=std::max(minColumnWidths[j],items[i][j].size());
	}
	//figure out total size needed
	std::size_t totalWidth=0;
	for(std::size_t w : minColumnWidths)
		totalWidth+=w;
	std::size_t paddingWidth=(minColumnWidths.empty()?0:minColumnWidths.size()-1);
	totalWidth+=paddingWidth;
	
	if(totalWidth<=outputWidth){ //good case, everything fits
		std::ostringstream os;
		os << std::left;
		for(std::size_t i=0; i<items.size(); i++){
			for(std::size_t j=0; j<items[i].size(); j++){
				if(j)
					os << ' ';
				os << std::setw(minColumnWidths[j]+(useANSICodes && !i?9:0)) 
				   << (useANSICodes && i?items[i][j]:underline(items[i][j]));
			}
			os << '\n';
		}
		return os.str();
	}
	else{
		//std::cout << "Table too wide: " << totalWidth << " columns when " << 
		//             outputWidth << " are allowed" << std::endl;
		
		//for now, try to shorten all columns which allow wrapping proportionally
		std::size_t wrappableWidth=0;
		for(unsigned int i=0; i<columns.size() && i<minColumnWidths.size(); i++){
			if(columns[i].allowWrap)
				wrappableWidth+=minColumnWidths[i];
		}
		//std::cout << "Wrappable width is " << wrappableWidth << std::endl;
		if(wrappableWidth>2){
			//determine a wrapping factor such that:
			//wrappableWidth*wrapFactor + (totalWidth-wrappableWidth) = outputWidth
			double wrapFactor=((double)outputWidth-(totalWidth-wrappableWidth))/wrappableWidth;
			//std::cout << "Wrap factor: " << wrapFactor << std::endl;
			//figure out which columns are the wrappable ones and how short they get
			for(unsigned int i=0; i<columns.size() && i<minColumnWidths.size(); i++){
				if(columns[i].allowWrap){
					minColumnWidths[i]=std::floor(minColumnWidths[i]*wrapFactor);
					if(!minColumnWidths[i])
						minColumnWidths[i]=1;
				}
			}
		}
		
		//whether the data in a given column is done for this row
		std::vector<bool> done(minColumnWidths.size(),false);
		//amount of each item which has been printed so far
		std::vector<std::size_t> printed(minColumnWidths.size(),false);
		
		std::ostringstream os;
		os << std::left;
		for(std::size_t i=0; i<items.size(); i++){
			//initially no column is done printing
			std::fill(done.begin(),done.end(),false);
			std::fill(printed.begin(),printed.end(),0);
			//need to continue until all coulmns are done
			while(!std::all_of(done.begin(),done.end(),[](bool b){return b;})){
				for(std::size_t j=0; j<items[i].size(); j++){
					if(j)
						os << ' ';
					if(done[j]){
						os << std::setw(minColumnWidths[j]) << ' ';
						continue;
					}
					//figure out how much more of this column to print
					//start by assuming we will use up to the full column width
					std::size_t len_to_print=minColumnWidths[j];
					if(columns[j].allowWrap){
						//if this is a wrapped coulmn, prefer to break after 
						//spaces and dashes.
						auto break_pos=items[i][j].find_first_of(" -_",printed[j]);
						while(break_pos!=std::string::npos && 
							  break_pos>=printed[j] && 
							  break_pos-printed[j]<minColumnWidths[j]){
							len_to_print=break_pos-printed[j]+1;
							break_pos=items[i][j].find_first_of(" -_",printed[j]+len_to_print);
						}
						//unless doing so would waste half or more of this line
						if(len_to_print*2<=minColumnWidths[j])
							len_to_print=minColumnWidths[j];
					}
					std::string to_print=items[i][j].substr(printed[j],len_to_print);
					
					os << std::setw(minColumnWidths[j]+(useANSICodes && !i?9:0)) 
					   << (useANSICodes && i?to_print:underline(to_print));
					
					if(printed[j]+len_to_print>=items[i][j].size()){
						done[j]=true;
					}
					else{
						printed[j]+=len_to_print;
					}
				}
				os << '\n';
			}
		}
		return os.str();
	}
}

std::string Client::jsonListToTable(const rapidjson::Value& jdata,
                                    const std::vector<columnSpec>& columns) const{
	std::vector<std::vector<std::string>> data;
	{
		data.emplace_back();
		auto& row=data.back();
		for(const auto& col : columns)
			row.push_back(col.label);
	}
	
	if(jdata.IsArray()){
		for(auto& jrow : jdata.GetArray()){
			data.emplace_back();
			auto& row=data.back();
			for(const auto& col : columns)
				row.push_back(rapidjson::Pointer(col.attribute.c_str()).Get(jrow)->GetString());
		}
	}
	else if(jdata.IsObject()){
		data.emplace_back();
		auto& row=data.back();
		for(const auto& col : columns)
			row.push_back(rapidjson::Pointer(col.attribute.c_str()).Get(jdata)->GetString());
	}
	
	return formatTable(data, columns);
}

Client::Client(bool useANSICodes, std::size_t outputWidth):
apiVersion("v1alpha1"),
useANSICodes(useANSICodes),
outputWidth(outputWidth)
{
	if(isatty(STDOUT_FILENO)){
		if(!this->outputWidth){ //determine width to use automatically
			struct winsize ws;
			ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
			this->outputWidth=ws.ws_col;
		}
	}
	else
		this->useANSICodes=false;
}

void Client::setOutputWidth(std::size_t width){
	outputWidth=width;
}

void Client::setUseANSICodes(bool use){
	useANSICodes=use;
}

void Client::createVO(const VOCreateOptions& opt){
	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();
  
	request.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", rapidjson::StringRef(opt.voName.c_str()), alloc);
	request.AddMember("metadata", metadata, alloc);
  
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	request.Accept(writer);
  
	auto response=httpRequests::httpPost(makeURL("vos"),buffer.GetString());
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
		std::cout << "Successfully created VO " 
			  << resultJSON["metadata"]["name"].GetString()
			  << " with ID " << resultJSON["metadata"]["id"].GetString() << std::endl;
	}
	else{
		std::cout << "Failed to create VO " << opt.voName;
		showError(response.body);
	}
}

void Client::deleteVO(const VODeleteOptions& opt){
	auto response=httpRequests::httpDelete(makeURL("vos/"+opt.voName));
	//TODO: other output formats
	if(response.status==200)
		std::cout << "Successfully deleted VO " << opt.voName << std::endl;
	else{
		std::cout << "Failed to delete VO " << opt.voName;
		showError(response.body);
	}
}

void Client::listVOs(const VOListOptions& opt){
	auto url = makeURL("vos");
	if (opt.user)
		url += "&user=true";
	auto response=httpRequests::httpGet(url);
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << jsonListToTable(json["items"], {{"Name", "/metadata/name"},{"ID", "/metadata/id", true}});
	}
	else{
		std::cout << "Failed to list VOs";
		showError(response.body);
	}
}

void Client::createCluster(const ClusterCreateOptions& opt){
	//find the config information
	std::string configPath;
	if(!opt.kubeconfig.empty())
		configPath=opt.kubeconfig;
	if(configPath.empty()) //try environment
		fetchFromEnvironment("KUBECONFIG",configPath);
	if(configPath.empty()) //try stardard default path
		configPath=getHomeDirectory()+".kube/config";
	//read the config information

	std::ifstream configFile(configPath.c_str());
	if(!configFile)
		throw std::runtime_error("Unable to read kubernetes config from "+configPath);
	std::string config, line;
	while(std::getline(configFile,line))
		config+=line+"\n";

	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();
	
	request.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", opt.clusterName, alloc);
	metadata.AddMember("vo", opt.voName, alloc);
	metadata.AddMember("kubeconfig", config, alloc);
	request.AddMember("metadata", metadata, alloc);
        
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	request.Accept(writer);

	auto response=httpRequests::httpPost(makeURL("clusters"),buffer.GetString());
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
	  	std::cout << "Successfully created cluster " 
			  << resultJSON["metadata"]["name"].GetString()
			  << " with ID " << resultJSON["metadata"]["id"].GetString() << std::endl;
	}
	else{
		std::cout << "Failed to create cluster " << opt.clusterName;
		showError(response.body);
	}
}

void Client::deleteCluster(const ClusterDeleteOptions& opt){
	auto response=httpRequests::httpDelete(makeURL("clusters/"+opt.clusterName));
	//TODO: other output formats
	if(response.status==200)
		std::cout << "Successfully deleted cluster " << opt.clusterName << std::endl;
	else{
		std::cout << "Failed to delete cluster " << opt.clusterName;
		showError(response.body);
	}

}

void Client::listClusters(){
	auto response=httpRequests::httpGet(makeURL("clusters"));
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << jsonListToTable(json["items"],
		                             {{"Name","/metadata/name"},
		                              {"ID","/metadata/id",true},
		                              {"Owned By","/metadata/owningVO"}});
	}
	else{
		std::cout << "Failed to list clusters";
		showError(response.body);
	}
}

void Client::listApplications(const ApplicationOptions& opt){
	std::string url=makeURL("apps");
	if(opt.devRepo)
		url+="&dev";
	if(opt.testRepo)
		url+="&test";
	auto response=httpRequests::httpGet(url);
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << jsonListToTable(json["items"],
		                             {{"Name","/metadata/name"},
		                              {"App Version","/metadata/app_version"},
		                              {"Chart Version","/metadata/chart_version"},
		                              {"Description","/metadata/description",true}});
	}
	else{
		std::cout << "Failed to list clusters";
		showError(response.body);
	}
}
	
void Client::getApplicationConf(const ApplicationConfOptions& opt){
	std::string url=makeURL("apps/"+opt.appName);
	if(opt.devRepo)
		url+="&dev";
	if(opt.testRepo)
		url+="&test";
	auto response=httpRequests::httpGet(url);
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
		std::string configuration=resultJSON["spec"]["body"].GetString();
		//if the user specified a file, write there
		if(!opt.outputFile.empty()){
			std::ofstream confFile(opt.outputFile);
			if(!confFile)
				throw std::runtime_error("Unable to write configuration to "+opt.outputFile);
			confFile << configuration;
		}
		else //otherwise print to stdout
			std::cout << configuration << std::endl;
	}
	else{
		std::cout << "Failed to get configuration for application " << opt.appName;
		showError(response.body);
	}
}
	
void Client::installApplication(const ApplicationInstallOptions& opt){
	std::string configuration;
	if(!opt.configPath.empty()){
		//read in user-specified configuration
		std::ifstream confFile(opt.configPath);
		if(!confFile)
			throw std::runtime_error("Unable to read application instance configuration from "+opt.configPath);
		std::string line;
		while(std::getline(confFile,line))
			configuration+=line+"\n";
	}

	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();
	
	request.AddMember("apiVersion", "v1alpha1", alloc);
	request.AddMember("vo", rapidjson::StringRef(opt.vo.c_str()), alloc);
	request.AddMember("cluster", rapidjson::StringRef(opt.cluster.c_str()), alloc);
	request.AddMember("tag", rapidjson::StringRef(opt.tag.c_str()), alloc);
	request.AddMember("configuration", rapidjson::StringRef(configuration.c_str()), alloc);
	
	std::string url=makeURL("apps/"+opt.appName);
	if(opt.devRepo)
		url+="&dev";
	if(opt.testRepo)
		url+="&test";

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	request.Accept(writer);

	auto response=httpRequests::httpPost(url,buffer.GetString());
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
		std::cout << "Successfully installed application " 
			  << resultJSON["metadata"]["application"].GetString() << " as instance "
			  << resultJSON["metadata"]["name"].GetString()
			  << " with ID " << resultJSON["metadata"]["id"].GetString() << std::endl;
	}
	else{
		std::cout << "Failed to install application " << opt.appName;
		showError(response.body);
	}
}

void Client::listInstances(const InstanceListOptions& opt){
	std::string url=makeURL("instances");
	if(!opt.vo.empty())
		url+="&vo="+opt.vo;
	if(!opt.cluster.empty())
		url+="&cluster="+opt.cluster;
	auto response=httpRequests::httpGet(url);
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << jsonListToTable(json["items"],
		                             {{"Name","/metadata/name"},
		                              {"Started","/metadata/created",true},
		                              {"VO","/metadata/vo"},
		                              {"Cluster","/metadata/cluster"},
		                              {"ID","/metadata/id",true}});
	}
	else{
		std::cout << "Failed to list clusters";
		showError(response.body);
	}
}

void Client::getInstanceInfo(const InstanceOptions& opt){
	std::string url=makeURL("instances/"+opt.instanceID);
	auto response=httpRequests::httpGet(url);
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document body;
		body.Parse(response.body.c_str());
		std::cout << jsonListToTable(body,
		                             {{"Name","/metadata/name"},
		                              {"Started","/metadata/created",true},
		                              {"VO","/metadata/vo"},
		                              {"Cluster","/metadata/cluster"},
		                              {"ID","/metadata/id",true}});
		std::cout << '\n' << bold("Services:");
		if(body["services"].Size()==0)
			std::cout << " (none)" << std::endl;
		else{
			std::cout << '\n' << jsonListToTable(body["services"],
			                                     {{"Name","/name"},
			                                      {"Cluster IP","/clusterIP"},
			                                      {"External IP","/externalIP"},
			                                      {"ports","/ports"}});
		}
		std::cout << '\n' << bold("Configuration:");

		if(body["metadata"]["configuration"].IsNull()
		   || (body["metadata"]["configuration"].IsString() && 
		       std::string(body["metadata"]["configuration"].GetString())
		       .find_first_not_of(" \t\n\r\v") == std::string::npos))
			std::cout << " (default)" << std::endl;
		else
			std::cout << "\n" << body["metadata"]["configuration"].GetString() << std::endl;
	}
	else{
		std::cout << "Failed to list clusters";
		showError(response.body);
	}
}

void Client::deleteInstance(const InstanceOptions& opt){
	auto response=httpRequests::httpDelete(makeURL("instances/"+opt.instanceID));
	//TODO: other output formats
	if(response.status==200)
		std::cout << "Successfully deleted instance " << opt.instanceID << std::endl;
	else{
		std::cout << "Failed to delete instance " << opt.instanceID;
		showError(response.body);
	}
}

std::string Client::getDefaultEndpointFilePath(){
	std::string path=getHomeDirectory();
	path+=".slate/endpoint";
	return path;
}

std::string Client::getDefaultCredFilePath(){
	std::string path=getHomeDirectory();
	path+=".slate/token";
	return path;
}

std::string Client::fetchStoredCredentials(){
	PermState perms=checkPermissions(credentialPath);
	if(perms==PermState::INVALID)
		throw std::runtime_error("Credentials file "+credentialPath+
		                         " has wrong permissions; should be 0600 and owned by the current user");
	std::string token;
	if(perms==PermState::DOES_NOT_EXIST)
		throw std::runtime_error("Credentials file "+credentialPath+" does not exist");
	
	std::ifstream credFile(credentialPath);
	if(!credFile) //this mostly shouldn't happen since we already checked the permissions
		throw std::runtime_error("Failed to open credentials file "+credentialPath+" for reading");
	
	credFile >> token;
	if(credFile.fail())
		throw std::runtime_error("Failed to read credentials file "+credentialPath+"");
	
	return token;
}

std::string Client::getToken(){
	if(token.empty()){ //need to read in
		if(credentialPath.empty())
			credentialPath=getDefaultCredFilePath();
		token=fetchStoredCredentials();
	}
	return token;
}

std::string Client::getEndpoint(){
	if(apiEndpoint.empty()){ //need to read in
		if(endpointPath.empty())
			endpointPath=getDefaultEndpointFilePath();
		PermState perms=checkPermissions(endpointPath);
		if(perms!=PermState::DOES_NOT_EXIST){
			//don't actually care about permissions, be we should only try to
			//read if the file exists
			std::ifstream endpointFile(endpointPath);
			if(!endpointFile) //this mostly shouldn't happen since we already checked the permissions
				throw std::runtime_error("Failed to open endpoint file "+endpointPath+" for reading");
			
			endpointFile >> apiEndpoint;
			if(endpointFile.fail())
				throw std::runtime_error("Failed to read endpoint file "+endpointPath+"");
		}
		else{ //use default value
			apiEndpoint="http://localhost:18080";
		}
	}
	auto schemeSepPos=apiEndpoint.find("://");
	//there should be a scheme separator
	if(schemeSepPos==std::string::npos)
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look like a valid URL");
	//there should be a scheme before the separator
	if(schemeSepPos==0)
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look like it has a valid URL scheme");
	//the scheme should contain only letters, digits, +, ., and -
	if(apiEndpoint.find_first_not_of("abcdefghijklmnopqrstuvwxzy0123456789+.-")<schemeSepPos)
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look like it has a valid URL scheme");
	//have something after the scheme
	if(schemeSepPos+3>=apiEndpoint.size())
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look like a valid URL");
	//no query string is permitted
	if(apiEndpoint.find('?')!=std::string::npos)
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look valid; "
		                         "no query is permitted");
	//no fragment is permitted
	if(apiEndpoint.find('#')!=std::string::npos)
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look valid; "
		                         "no fragment is permitted");
	//try to figure out where the hostname starts
	auto hostPos=schemeSepPos+3;
	if(apiEndpoint.find('@',hostPos)!=std::string::npos)
		hostPos=apiEndpoint.find('@',hostPos)+1;
	//have a hostname
	if(hostPos>=apiEndpoint.size())
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look like a valid URL");
	auto portPos=apiEndpoint.find(':',hostPos);
	//no slashes are permitted before the port
	if(apiEndpoint.find('/',hostPos)<portPos)
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look valid; "
		                         "no path (including a trailing slash) is permitted");
	if(portPos!=std::string::npos){
		portPos++;
		if(portPos>=apiEndpoint.size())
			throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look like a valid URL");
		//after the start of the port, there may be only digits
		if(apiEndpoint.find_first_not_of("0123456789",portPos)!=std::string::npos)
			throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look valid; "
			                         "port number may contain only digits and no path "
			                         "(including a trailing slash) is permitted");
	}
	if(apiEndpoint[apiEndpoint.size()-1]=='/')
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look valid; "
		                         "no path (including a trailing slash) is permitted");
	
	return apiEndpoint;
}
