#include "Client.h"

#include <array>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <sys/ioctl.h>

#include "client_version.h"
#include "Utilities.h"
#include "Process.h"

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
                                const std::vector<columnSpec>& columns,
				const bool headers) const{
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
				os << std::setw(minColumnWidths[j]+(useANSICodes && !i && headers?9:0)) 
				   << ((useANSICodes && i) || !headers?items[i][j]:underline(items[i][j]));
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
					   << ((useANSICodes && i) || !headers?to_print:underline(to_print));
					
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
                                    const std::vector<columnSpec>& columns,
				    const bool headers = true) const{
	std::vector<std::vector<std::string>> data;
	if (headers) {
		data.emplace_back();
		auto& row=data.back();
		for(const auto& col : columns)
			row.push_back(col.label);
	}
	
	if(jdata.IsArray()){
		for(auto& jrow : jdata.GetArray()){
			data.emplace_back();
			auto& row=data.back();
			for(const auto& col : columns) {
				auto attribute = rapidjson::Pointer(col.attribute.c_str()).Get(jrow);
				if (!attribute)
					throw std::runtime_error("Given attribute does not exist");
				row.push_back(attribute->GetString());
			}
		}
	}
	else if(jdata.IsObject()){
		data.emplace_back();
		auto& row=data.back();
		for(const auto& col : columns) {
			auto attribute = rapidjson::Pointer(col.attribute.c_str()).Get(jdata);
			if (!attribute)
				throw std::runtime_error("Given attribute does not exist");
			row.push_back(attribute->GetString());
		}
	}
	
	return formatTable(data, columns, headers);
}

std::string Client::displayContents(const rapidjson::Value& jdata,
				    const std::vector<columnSpec>& columns,
				    const bool headers = true) const{
	//TODO: have this jsonListToTable but with iterating over a dictionary instead of how it's
	//currently done in jsonListToTable (for secrets in particular)

  	std::vector<std::vector<std::string>> data;
	if (headers) {
		data.emplace_back();
		auto& row=data.back();
		for (auto& col : columns)
			row.push_back(col.label);
	}

	for (auto itr = jdata.MemberBegin(); itr != jdata.MemberEnd(); itr++) {
		data.emplace_back();
		auto& row=data.back();
		auto key = itr->name.GetString();
		if (!key)
			throw std::runtime_error("Key does not exist");
		row.push_back(key);
		auto val = itr->value.GetString();
		if (!val)
			throw std::runtime_error("Value does not exist");
		row.emplace_back(val,itr->value.GetStringLength());
	}
	return formatTable(data, columns, headers);
}

std::string readJsonPointer(const rapidjson::Value& jdata,
			    std::string pointer) {
	auto ptr = rapidjson::Pointer(pointer).Get(jdata);
	if (ptr == NULL)
		throw std::runtime_error("The pointer provided to format output is not valid");
	std::string result = ptr->GetString();
	return result + "\n";
}

std::string Client::formatOutput(const rapidjson::Value& jdata, const rapidjson::Value& original,
				 const std::vector<columnSpec>& columns) const{
	//output in json format
	if (outputFormat == "json") {
		rapidjson::StringBuffer buf;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
		jdata.Accept(writer);
		std::string str = buf.GetString();
		str += "\n";
		return str;
	}

	//output in table format with custom columns given in a file
	if (outputFormat.find("custom-columns-file") != std::string::npos) {
		if (outputFormat.find("=") == std::string::npos)
			throw std::runtime_error("No file was specified to format output with custom columns");
	  
		std::string file = outputFormat.substr(outputFormat.find("=") + 1);
		if (file.empty())
			throw std::runtime_error("No file was specified to format output with custom columns");

		//read from given file to get columns
		std::ifstream columnFormat(file);
		if (!columnFormat.is_open())
			throw std::runtime_error("The specified file for custom columns was not able to be opened");
		
		std::string line;
		std::vector<columnSpec> customColumns;
		std::vector<std::string> labels;
		std::vector<std::string> data;
		//get labels from first line
		while (getline(columnFormat, line)) {
			//split words by tabs and/or spaces in each line
			std::stringstream ss(line);
			std::vector<std::string> tokens;
			std::string item;
			while (std::getline(ss, item, '\t')) {
				std::stringstream itemss(item);
				std::string separated;
				while (std::getline(itemss, separated, ' ')) { 
					if(!separated.empty())
						tokens.push_back(separated);
				}
			}

			//separate labels from the attribute for each label
			if (labels.size() == 0)
				labels = tokens;
			else if (data.size() == 0)
				data = tokens;
			else
				throw std::runtime_error("The custom columns file should only include labels and a single attribute for each label"); 
		}
		columnFormat.close();

		//create the custom columns from gathered labels & attributes
		for (auto i=0; i < labels.size(); i++) {
			columnSpec col(labels[i],data[i]);
			customColumns.push_back(col);
		}

		return jsonListToTable(jdata,customColumns);
	}
	
	//output in table format with custom columns given inline
	if (outputFormat.find("custom-columns") != std::string::npos) {
		if (outputFormat.find("=") == std::string::npos)
			throw std::runtime_error("No custom columns were specified to format output with");
	  
		std::string cols = outputFormat.substr(outputFormat.find("=") + 1);
		if (cols.empty())
			throw std::runtime_error("No custom columns were specified to format output with");

		//get columns from inline specification
		std::vector<columnSpec> customColumns;
		while (!cols.empty()) {
			if (cols.find(":") == std::string::npos)
				throw std::runtime_error("Every label for the table must have an attribute specified with it");

			std::string label = cols.substr(0, cols.find(":"));
			cols = cols.substr(cols.find(":") + 1);
			if (cols.empty())
				throw std::runtime_error("Every label for the table must have an attribute specified with it");
			
			std::string data;
			if (cols.find(",") != std::string::npos) {
				data = cols.substr(0, cols.find(","));
				cols = cols.substr(cols.find(",") + 1);
			} else {
				data = cols.substr(0);
				cols = "";
			}
			columnSpec col(label,data);
			customColumns.push_back(col);
		        
		}
		return jsonListToTable(jdata,customColumns);
	}

	//output in default table format, with headers suppressed
	if (outputFormat == "no-headers")
		return jsonListToTable(jdata,columns,false);

	//output in format of a json pointer specified in the given file
	if (outputFormat.find("jsonpointer-file") != std::string::npos) {
		if (outputFormat.find("=") == std::string::npos)
			throw std::runtime_error("No json pointer file was specified to be used to format the output");

		std::string file = outputFormat.substr(outputFormat.find("=") + 1);
		if (file.empty())
			throw std::runtime_error("No file was specified to format output with");

		std::ifstream jsonpointer(file);
		if (!jsonpointer.is_open())
			throw std::runtime_error("The file specified to format output was unable to be opened");

		//get pointer specification from file
		std::string pointer;
		std::string part;
		while (getline(jsonpointer, part))
			pointer += part;
		std::string response = readJsonPointer(original, pointer);		
		jsonpointer.close();

		return response;
	}

	//output in format of json pointer specified inline
	if (outputFormat.find("jsonpointer") != std::string::npos) {
		if (outputFormat.find("=") == std::string::npos)
			throw std::runtime_error("No json pointer format was included to use to format the output");


		std::string jsonpointer = outputFormat.substr(outputFormat.find("=") + 1);
		if (jsonpointer.empty())
			throw std::runtime_error("No json pointer was given to format output");
		return readJsonPointer(original, jsonpointer);
	}

	//display secrets separately from normal json objects
	for (auto& col : columns) {
		if (col.attribute == "/contents")
			return displayContents(jdata["contents"], columns);
	}
	
	//output in table format with default columns
	if (outputFormat.empty())
		return jsonListToTable(jdata,columns);

	throw std::runtime_error("Specified output format is not supported");
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

void Client::printVersion(){
	rapidjson::Document json(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = json.GetAllocator();
	rapidjson::Value client(rapidjson::kObjectType);
	client.AddMember("version", rapidjson::StringRef(clientVersionString), alloc);
	json.AddMember("client", client, alloc);
	
	std::cout << formatOutput(json, json, {{"Client Version", "/client/version"}});
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
  
	auto response=httpRequests::httpPost(makeURL("vos"),buffer.GetString(),defaultOptions());
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
	auto response=httpRequests::httpDelete(makeURL("vos/"+opt.voName),defaultOptions());
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
	auto response=httpRequests::httpGet(url,defaultOptions());
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << formatOutput(json["items"], json, {{"Name", "/metadata/name"},{"ID", "/metadata/id", true}});
	}
	else{
		std::cout << "Failed to list VOs";
		showError(response.body);
	}
}

void Client::createCluster(const ClusterCreateOptions& opt){
	const static std::string controllerRepo="https://gitlab.com/ucsd-prp/nrp-controller";
	const static std::string controllerDeploymentURL="https://gitlab.com/ucsd-prp/nrp-controller/raw/master/deploy.yaml";
	const static std::string federationRoleURL="https://gitlab.com/ucsd-prp/nrp-controller/raw/master/federation-role.yaml";
	
	//find the config information
	std::string configPath;
	if(!opt.kubeconfig.empty())
		configPath=opt.kubeconfig;
	if(configPath.empty()) //try environment
		fetchFromEnvironment("KUBECONFIG",configPath);
	if(configPath.empty()) //try stardard default path
		configPath=getHomeDirectory()+".kube/config";
	//read the config information
	std::string config;
	
	if(checkPermissions(configPath)==PermState::DOES_NOT_EXIST)
		throw std::runtime_error("Config file '"+configPath+"' does not exist");
	
	std::cout << "Extracting kubeconfig from " << configPath << "..." << std::endl;
	auto result = runCommand("kubectl",{"config","view","--minify","--flatten","--kubeconfig",configPath});
	if(result.status)
		throw std::runtime_error("Unable to extract kubeconfig: "+result.error);
	config=result.output;
	
	//Try to figure out whether we are inside of a federation cluster, or 
	//otherwise whether the federation controller is deployed
	std::cout << "Checking for privilege level/deployment controller status..." << std::endl;
	result=runCommand("kubectl",{"get","deployments","-n","kube-system","--kubeconfig",configPath});
	if(result.status==0){
		//We can list objects in kube-system, so permissions are too broad. 
		//Check whether the controller is running:
		if(result.output.find("nrp-controller")==std::string::npos){
			//controller is not deployed, 
			//check whether the user wants us to install it
			std::cout << "It appears that the nrp-controller is not deployed on this cluster.\n\n"
			<< "The nrp-controller is a utility which allows SLATE to operate with\n"
			<< "reduced privileges in your Kubernetes cluster. It grants SLATE access to a\n"
			<< "single initial namespace of your choosing and a mechanism to create additional\n"
			<< "namespaces, without granting it any access to namespaces it has not created.\n"
			<< "This means that you can be certain that SLATE will not interfere with other\n"
			<< "uses of your cluster.\n"
			<< "See " << controllerRepo << " for more information on the\n"
			<< "controller software and \n"
			<< controllerDeploymentURL << " for the\n"
			<< "deployment definition used to install it.\n\n"
			<< "This component is needed for SLATE to use this cluster.\n"
			<< "Do you want to install it now? [y]/n: ";
			std::cout.flush();
			
			if(!opt.assumeYes){
				std::string answer;
				std::getline(std::cin,answer);
				if(answer!="" && answer!="y" && answer!="Y")
					throw std::runtime_error("Cluster registration aborted");
			}
			else
				std::cout << "assuming yes" << std::endl;
			
			std::cout << "Applying " << controllerDeploymentURL << std::endl;
			result=runCommand("kubectl",{"apply","-f",controllerDeploymentURL,"--kubeconfig",configPath});
			if(result.status)
				throw std::runtime_error("Failed to deploy federation controller: "+result.error);
		}
		else
			std::cout << " Controller is deployed" << std::endl;
		std::cout << "Checking for federation ClusterRole..." << std::endl;
		result=runCommand("kubectl",{"get","clusterrole","federation-cluster","--kubeconfig",configPath});
		if(result.status){
			std::cout << "It appears that the federation-cluster ClusterRole is not deployed on this cluster.\n\n"
			<< "This is a ClusterRole used by the nrp-controller to grant SLATE access\n"
			<< "to only its own namespaces. You can view its definition at\n"
			<< federationRoleURL << ".\n\n"
			<< "This component is needed for SLATE to use this cluster.\n"
			<< "Do you want to install it now? [y]/n: ";
			std::cout.flush();
			
			if(!opt.assumeYes){
				std::string answer;
				std::getline(std::cin,answer);
				if(answer!="" && answer!="y" && answer!="Y")
					throw std::runtime_error("Cluster registration aborted");
			}
			else
				std::cout << "assuming yes" << std::endl;
			
			std::cout << "Applying " << federationRoleURL << std::endl;
			result=runCommand("kubectl",{"apply","-f",federationRoleURL,"--kubeconfig",configPath});
			if(result.status)
				throw std::runtime_error("Failed to deploy federation clusterrole: "+result.error);
		}
		else
			std::cout << " ClusterRole is defined" << std::endl;
		
		//At this pont we have ensured that we have the right tools, but the 
		//priveleges are still too high. 
		std::cout << "This kubeconfig gives greater privileges than SLATE should use.\n\n"
		<< "SLATE should be granted access using a ServiceAccount created with a Cluster\n"
		<< "object by the nrp-controller. Do you want to create such a ServiceAccount\n"
		<< "automatically now? [y]/n: ";
		std::cout.flush();
		if(!opt.assumeYes){
			std::string answer;
			std::getline(std::cin,answer);
			if(answer!="" && answer!="y" && answer!="Y")
				throw std::runtime_error("Cluster registration aborted");
		}
		else
			std::cout << "assuming yes" << std::endl;
		
		std::string namespaceName;
		std::cout << "Please enter the name you would like to give the ServiceAccount and core\n"
		<< "SLATE namespace. The default is 'slate-system': ";
		if(!opt.assumeYes){
			std::cout.flush();
			std::getline(std::cin,namespaceName);
		}
		else
			std::cout << "assuming slate-system" << std::endl;
		if(namespaceName.empty())
			namespaceName="slate-system";
		std::cout << "Creating Cluster '" << namespaceName << "'..." << std::endl;
		
		result=runCommandWithInput("kubectl",
R"(apiVersion: nrp-nautilus.io/v1alpha1
kind: Cluster
metadata: 
  name: )"+namespaceName,
								   {"create","-f","-"});
		if(result.status)
			throw std::runtime_error("Cluster creation failed: "+result.error);
		
		//wait for the corresponding namespace to be ready
		while(true){
			result=runCommand("kubectl",{"get","namespace",namespaceName,"-o","jsonpath={.status.phase}"});
			if(result.status==0 && result.output=="Active")
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		
		std::cout << "Locating ServiceAccount credentials..." << std::endl;
		result=runCommand("kubectl",{"get","serviceaccount",namespaceName,"-n",namespaceName,"-o","jsonpath='{.secrets[].name}'"});
		if(result.status)
			throw std::runtime_error("Unable to locate ServiceAccount credential secret: "+result.error);
		std::string credName=result.output;
		{ //kubectl leaves quotes around the name. Get rid of them.
			std::size_t pos;
			while((pos=credName.find('\''))!=std::string::npos)
				credName.erase(pos,1);
		}
		
		std::cout << "Extracting CA data..." << std::endl;
		result=runCommand("kubectl",{"get","secret",credName,"-n",namespaceName,"-o","jsonpath='{.data.ca\\.crt}'"});
		if(result.status)
			throw std::runtime_error("Unable to extract ServiceAccount CA data from secret"+result.error);
		std::string caData=result.output;
		
		std::cout << "Determining server address..." << std::endl;
		result=runCommand("kubectl",{"cluster-info"});
		if(result.status)
			throw std::runtime_error("Unable to get Kubernetes cluster-info: "+result.error);
		std::string serverAddress;
		{
			auto startPos=result.output.find("http");
			if(startPos==std::string::npos)
				throw std::runtime_error("Unable to parse Kubernetes cluster-info");
			auto endPos=result.output.find((char)0x1B,startPos);
			if(endPos==std::string::npos)
				throw std::runtime_error("Unable to parse Kubernetes cluster-info");
			serverAddress=result.output.substr(startPos,endPos-startPos);
		}
		
		std::cout << "Extracting ServiceAccount token..." << std::endl;
		result=runCommand("kubectl",{"get","secret","-n",namespaceName,credName,"-o","jsonpath={.data.token}"});
		if(result.status)
			throw std::runtime_error("Unable to extract ServiceAccount token data from secret: "+result.error);
		std::string encodedToken=result.output;
		
		result=runCommandWithInput("base64",encodedToken,{"--decode"});
		if(result.status)
			throw std::runtime_error("Unable to decode token data with base64: "+result.error);
		std::string token=result.output;
		
		{
			std::ostringstream os;
			os << R"(apiVersion: v1
clusters:
- cluster:
    certificate-authority-data: )"
			<< caData << '\n'
			<< "    server: " << serverAddress << '\n'
			<< R"(  name: )" << namespaceName << R"(
contexts:
- context:
    cluster: )" << namespaceName << R"(
    namespace: )" << namespaceName << '\n'
			<< "    user: " << namespaceName << '\n'
			<< R"(  name: )" << namespaceName << R"(
current-context: )" << namespaceName << R"(
kind: Config
preferences: {}
users:
- name: )" << namespaceName << '\n'
			<< R"(  user:
    token: )" << token << '\n';
			config=os.str();
			std::cout << " Done generating config with limited privileges" << std::endl;
		}
	}
	else{
		throw std::runtime_error("Unable to list deployments in the kube-system namespace; "
		                         "this command needs to be run with kubernetes administrator "
		                         "privileges in order to create the correct environment (with "
		                         "limited privileges) for SLATE to use.\n"
		                         "Kubernetes error: "+result.error);
	}
	
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

	std::cout << "Sending config to SLATE server..." << std::endl;
	auto response=httpRequests::httpPost(makeURL("clusters"),buffer.GetString(),defaultOptions());
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
	auto response=httpRequests::httpDelete(makeURL("clusters/"+opt.clusterName),defaultOptions());
	//TODO: other output formats
	if(response.status==200)
		std::cout << "Successfully deleted cluster " << opt.clusterName << std::endl;
	else{
		std::cout << "Failed to delete cluster " << opt.clusterName;
		showError(response.body);
	}

}

void Client::listClusters(){
	auto response=httpRequests::httpGet(makeURL("clusters"),defaultOptions());
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << formatOutput(json["items"], json,
		                             {{"Name","/metadata/name"},
		                              {"ID","/metadata/id",true},
		                              {"Owned By","/metadata/owningVO"}});
	}
	else{
		std::cout << "Failed to list clusters";
		showError(response.body);
	}
}

void Client::grantVOClusterAccess(const VOClusterAccessOptions& opt){
	auto response=httpRequests::httpPut(makeURL("clusters/"
	                                            +opt.clusterName
	                                            +"/allowed_vos/"
	                                            +opt.voName),"",defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		std::cout << "Successfully granted VO " << opt.voName 
		          << " access to cluster " << opt.clusterName << std::endl;
	}
	else{
		std::cout << "Failed to grant VO " << opt.voName << " access to cluster " 
		          << opt.clusterName;
		showError(response.body);
	}
}

void Client::revokeVOClusterAccess(const VOClusterAccessOptions& opt){
	auto response=httpRequests::httpDelete(makeURL("clusters/"
	                                               +opt.clusterName
	                                               +"/allowed_vos/"
	                                               +opt.voName),
	                                       defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		std::cout << "Successfully revoked VO " << opt.voName 
		          << " access to cluster " << opt.clusterName << std::endl;
	}
	else{
		std::cout << "Failed to revoke VO " << opt.voName << " access to cluster " 
		          << opt.clusterName;
		showError(response.body);
	}
}

void Client::listVOWithAccessToCluster(const ClusterAccessListOptions& opt){
	auto response=httpRequests::httpGet(makeURL("clusters/"
	                                            +opt.clusterName
	                                            +"/allowed_vos"),
	                                    defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << formatOutput(json["items"], json, {{"Name", "/metadata/name"},
		      {"ID", "/metadata/id", true}});
	}
	else{
		std::cout << "Failed to retrieve VOs with access to cluster " << opt.clusterName;
		showError(response.body);
	}
}

void Client::listApplications(const ApplicationOptions& opt){
	std::string url=makeURL("apps");
	if(opt.devRepo)
		url+="&dev";
	if(opt.testRepo)
		url+="&test";
	auto response=httpRequests::httpGet(url,defaultOptions());
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << formatOutput(json["items"], json,
		                             {{"Name","/metadata/name"},
		                              {"App Version","/metadata/app_version"},
		                              {"Chart Version","/metadata/chart_version"},
		                              {"Description","/metadata/description",true}});
	}
	else{
		std::cout << "Failed to list applications";
		showError(response.body);
	}
}
	
void Client::getApplicationConf(const ApplicationConfOptions& opt){
	std::string url=makeURL("apps/"+opt.appName);
	if(opt.devRepo)
		url+="&dev";
	if(opt.testRepo)
		url+="&test";
	auto response=httpRequests::httpGet(url,defaultOptions());
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
	request.AddMember("configuration", rapidjson::StringRef(configuration.c_str()), alloc);
	
	std::string url=makeURL("apps/"+opt.appName);
	if(opt.devRepo)
		url+="&dev";
	if(opt.testRepo)
		url+="&test";

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	request.Accept(writer);

	auto response=httpRequests::httpPost(url,buffer.GetString(),defaultOptions());
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
	auto response=httpRequests::httpGet(url,defaultOptions());
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		filterInstanceNames(json, "/items");
		std::cout << formatOutput(json["items"], json,
		                             {{"Name","/metadata/name"},
		                              {"Started","/metadata/created",true},
		                              {"VO","/metadata/vo"},
		                              {"Cluster","/metadata/cluster"},
		                              {"ID","/metadata/id",true}});
	}
	else{
		std::cout << "Failed to list application instances";
		showError(response.body);
	}
}

void Client::getInstanceInfo(const InstanceOptions& opt){
	if(!verifyInstanceID(opt.instanceID))
		throw std::runtime_error("The instance info command requires an instance ID, not a name");
	
	std::string url=makeURL("instances/"+opt.instanceID);
	auto response=httpRequests::httpGet(url,defaultOptions());
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document body;
		body.Parse(response.body.c_str());
		filterInstanceNames(body,"");
		std::cout << formatOutput(body, body,
		                             {{"Name","/metadata/name"},
		                              {"Started","/metadata/created",true},
		                              {"VO","/metadata/vo"},
		                              {"Cluster","/metadata/cluster"},
		                              {"ID","/metadata/id",true}});
		std::cout << '\n' << bold("Services:");
		if(body["services"].Size()==0)
			std::cout << " (none)" << std::endl;
		else{
			std::cout << '\n' << formatOutput(body["services"], body,
			                                     {{"Name","/name"},
			                                      {"Cluster IP","/clusterIP"},
			                                      {"External IP","/externalIP"},
			                                      {"Ports","/ports"}});
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
		std::cout << "Failed to get application instance info";
		showError(response.body);
	}
}

void Client::deleteInstance(const InstanceDeleteOptions& opt){
	if(!verifyInstanceID(opt.instanceID))
		throw std::runtime_error("The instance delete command requires an instance ID, not a name");
	
	auto url=makeURL("instances/"+opt.instanceID);
	if(opt.force)
		url+="&force";
	auto response=httpRequests::httpDelete(url,defaultOptions());
	//TODO: other output formats
	if(response.status==200)
		std::cout << "Successfully deleted instance " << opt.instanceID << std::endl;
	else{
		std::cout << "Failed to delete instance " << opt.instanceID;
		showError(response.body);
	}
}

void Client::fetchInstanceLogs(const InstanceLogOptions& opt){
	if(!verifyInstanceID(opt.instanceID))
		throw std::runtime_error("The instance logs command requires an instance ID, not a name");
	
	std::string url=makeURL("instances/"+opt.instanceID+"/logs");
	if(opt.maxLines)
		url+="&max_lines="+std::to_string(opt.maxLines);
	if(!opt.container.empty())
		#warning TODO: container name should be URL encoded
		url+="&container="+opt.container;
	auto response=httpRequests::httpGet(url,defaultOptions());
	if(response.status==200){
		rapidjson::Document body;
		body.Parse(response.body.c_str());
		auto ptr=rapidjson::Pointer("/logs").Get(body);
		if(ptr==NULL)
			throw std::runtime_error("Failed to extract log data from server response");
		std::cout << ptr->GetString();
	}
	else{
		std::cout << "Failed to get application instance logs";
		showError(response.body);
	}
}

void Client::listSecrets(const SecretListOptions& opt){
	std::string url=makeURL("secrets") + "&vo="+opt.vo;
	if(!opt.cluster.empty())
		url+="&cluster="+opt.cluster;
	auto response=httpRequests::httpGet(url,defaultOptions());
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << formatOutput(json["items"], json,
		                             {{"Name","/metadata/name"},
		                              {"Created","/metadata/created",true},
		                              {"VO","/metadata/vo"},
		                              {"Cluster","/metadata/cluster"},
		                              {"ID","/metadata/id",true}});
	}
	else{
		std::cout << "Failed to list secrets";
		showError(response.body);
	}
}

void Client::getSecretInfo(const SecretOptions& opt){
	if(!verifySecretID(opt.secretID))
		throw std::runtime_error("The secret info command requires a secret ID, not a name");
	
	std::string url=makeURL("secrets/"+opt.secretID);
	auto response=httpRequests::httpGet(url,defaultOptions());
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document body;
		body.Parse(response.body.c_str());
		std::cout << formatOutput(body, body,
		                             {{"Name","/metadata/name"},
		                              {"Created","/metadata/created",true},
		                              {"VO","/metadata/vo"},
		                              {"Cluster","/metadata/cluster"},
					      {"ID","/metadata/id",true}});
		std::cout << '\n' << bold("Contents:") << "\n";

		std::cout << formatOutput(body, body,
					  {{"Key", "/contents"},
					   {"Value", "/contents", true}});
	}
	else{
		std::cout << "Failed to get secret info";
		showError(response.body);
	}
}

void Client::createSecret(const SecretCreateOptions& opt){
	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();

	request.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", opt.name, alloc);
	metadata.AddMember("vo", opt.vo, alloc);
	metadata.AddMember("cluster", opt.cluster, alloc);
	request.AddMember("metadata", metadata, alloc);
	rapidjson::Value contents(rapidjson::kObjectType);
	for (auto item : opt.data) {
		if (item.find("=") != std::string::npos) {
			auto keystr = item.substr(0, item.find("="));
			if (keystr.empty()) {
				std::cout << "Failed to create secret: No key given with value " << item << std::endl;
				return;
			}
			auto val = item.substr(item.find("=") + 1);
			if (val.empty()) {
				std::cout << "Failed to create secret: No value given with key " << keystr << std::endl;
			        return;
			}
			
			rapidjson::Value key;
			key.SetString(keystr.c_str(), keystr.length(), alloc);
			contents.AddMember(key, val, alloc);
		} else {
			std::cout << "Failed to create secret: The key, value pair " << item << " is not in the required form key=val" << std::endl;
			return;
		}
	}

	request.AddMember("contents", contents, alloc);
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	request.Accept(writer);

	auto response=httpRequests::httpPost(makeURL("secrets"),buffer.GetString(),defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
	  	std::cout << "Successfully created secret " 
			  << resultJSON["metadata"]["name"].GetString()
			  << " with ID " << resultJSON["metadata"]["id"].GetString() << std::endl;
	}
	else{
		std::cout << "Failed to create secret " << opt.name;
		showError(response.body);
	}
}

void Client::deleteSecret(const SecretDeleteOptions& opt){
	if(!verifySecretID(opt.secretID))
		throw std::runtime_error("The secret delete command requires a secret ID, not a name");
	
	auto url=makeURL("secrets/"+opt.secretID);
	if(opt.force)
		url+="&force";
	auto response=httpRequests::httpDelete(url,defaultOptions());
	//TODO: other output formats
	if(response.status==200)
		std::cout << "Successfully deleted secret " << opt.secretID << std::endl;
	else{
		std::cout << "Failed to delete secret " << opt.secretID;
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
		if(credentialPath.empty()) //use default if not specified
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

httpRequests::Options Client::defaultOptions(){
	httpRequests::Options opts;
#ifdef USE_CURLOPT_CAINFO
	detectCABundlePath();
	opts.caBundlePath=caBundlePath;
#endif
	return opts;
}

#ifdef USE_CURLOPT_CAINFO
void Client::detectCABundlePath(){
	if(caBundlePath.empty()){
		//collection of known paths, copied from curl's acinclude.m4
		const static auto possiblePaths={
			"/etc/ssl/certs/ca-certificates.crt",     //Debian systems
			"/etc/pki/tls/certs/ca-bundle.crt",       //Redhat and Mandriva
			"/usr/share/ssl/certs/ca-bundle.crt",     //old(er) Redhat
			"/usr/local/share/certs/ca-root-nss.crt", //FreeBSD
			"/etc/ssl/cert.pem",                      //OpenBSD, FreeBSD (symlink)
			"/etc/ssl/certs/",                        //SUSE
		};
		for(const auto path : possiblePaths){
			if(checkPermissions(path)!=PermState::DOES_NOT_EXIST){
				caBundlePath=path;
				return;
			}
		}
	}
}
#endif

bool Client::verifyInstanceID(const std::string& id){
	if(id.size()!=45)
		return false;
	if(id.find("Instance_")!=0)
		return false;
	if(id.find_first_not_of("0123456789abcdef-",9)!=std::string::npos)
		return false;
	return true;
}

bool Client::verifySecretID(const std::string& id){
	if(id.size()!=43)
		return false;
	if(id.find("Secret_")!=0)
		return false;
	if(id.find_first_not_of("0123456789abcdef-",7)!=std::string::npos)
		return false;
	return true;
}

void Client::filterInstanceNames(rapidjson::Document& json, std::string pointer){
	auto filterName=[&json](rapidjson::Value& item){
		std::string VO=rapidjson::Pointer("/metadata/vo").Get(item)->GetString();
		VO+='-';
		rapidjson::Value* nameValue=rapidjson::Pointer("/metadata/name").Get(item);
		std::string name=nameValue->GetString();
		if(name.find(VO)==0)
			name.erase(0,VO.size());
		nameValue->SetString(name.c_str(),name.size(),json.GetAllocator());
	};
	rapidjson::Value* item=rapidjson::Pointer(pointer.c_str()).Get(json);
	if(item->IsArray()){
		for(auto& item_ : item->GetArray())
			filterName(item_);
	}
	else
		filterName(*item);
}
