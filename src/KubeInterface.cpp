#include "KubeInterface.h"

#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "Utilities.h"
#include "FileHandle.h"

namespace kubernetes{
	
commandResult kubectl(const std::string& configPath,
                      const std::vector<std::string>& arguments){
	std::vector<std::string> fullArgs;
	fullArgs.push_back("--request-timeout=10s");
	if(!configPath.empty())
		fullArgs.push_back("--kubeconfig="+configPath);
	std::copy(arguments.begin(),arguments.end(),std::back_inserter(fullArgs));
	auto result=runCommand("kubectl",fullArgs);
	return commandResult{removeShellEscapeSequences(result.output),
	                     removeShellEscapeSequences(result.error),result.status};
}

#ifdef SLATE_SERVER
void kubectl_create_namespace(const std::string& clusterConfig, const Group& group) {
	std::string input=
R"(apiVersion: nrp-nautilus.io/v1alpha1
kind: ClusterNamespace
metadata:
  name: )"+group.namespaceName()+"\n";
	
	auto tmpFile=makeTemporaryFile("namespace_yaml_");
	std::ofstream tmpfile(tmpFile);
	tmpfile << input;
	tmpfile.close();
	
	auto result=runCommand("kubectl",{"--kubeconfig",clusterConfig,"create","-f",tmpFile});
	if(result.status){
		//if the namespace already existed we do not have a problem, otherwise we do
		if(result.error.find("AlreadyExists")==std::string::npos)
			throw std::runtime_error("Namespace creation failed: "+result.error);
	}
}

void kubectl_delete_namespace(const std::string& clusterConfig, const Group& group) {
	auto result=runCommand("kubectl",{"--kubeconfig",clusterConfig,
		"delete","clusternamespace",group.namespaceName()});
	if(result.status){
		//if the namespace did not exist we do not have a problem, otherwise we do
		if(result.error.find("NotFound")==std::string::npos)
			throw std::runtime_error("Namespace deletion failed: "+result.error);
	}
}
#endif //SLATE_SERVER
	
commandResult helm(const std::string& configPath,
                   const std::string& tillerNamespace,
                   const std::vector<std::string>& arguments){
	std::vector<std::string> fullArgs;
	if(getHelmMajorVersion()==2){
		fullArgs.push_back("--tiller-namespace="+tillerNamespace);
		fullArgs.push_back("--tiller-connection-timeout=10");
	}
	std::copy(arguments.begin(),arguments.end(),std::back_inserter(fullArgs));
	return runCommand("helm",fullArgs,{{"KUBECONFIG",configPath}});
}

unsigned int getHelmMajorVersion(){
	auto commandResult = runCommand("helm",{"version"});
	unsigned int helmMajorVersion=0;
	std::string line;
	std::istringstream ss(commandResult.output);
	while(std::getline(ss,line)){
		if(line.find("Server: ")==0) //ignore tiller version
			continue;
		std::string marker="SemVer:\"v";
		auto startPos=line.find(marker);
		if(startPos==std::string::npos){
			marker="Version:\"v";
			startPos=line.find(marker);
			if(startPos==std::string::npos)
				continue; //give up :(
		}
		startPos+=marker.size();
		if(startPos>=line.size()-1) //also weird
			continue;
		auto endPos=line.find('.',startPos+1);
		try{
			helmMajorVersion=std::stoul(line.substr(startPos,endPos-startPos));
		}catch(std::exception& ex){
			throw std::runtime_error("Unable to extract helm version");
		}
	}
	if(!helmMajorVersion)
		throw std::runtime_error("Unable to extract helm version");
	return helmMajorVersion;
}

	std::multimap<std::string,std::string> findAll(const std::string& clusterConfig, const std::string& selector, const std::string& nspace, const std::string& verbs){
	std::multimap<std::string,std::string> objects;

	//first determine all possible API resource types
	auto result=kubectl(clusterConfig, {"api-resources","-o=name","--verbs="+verbs});
	if(result.status!=0)
		throw std::runtime_error("Failed to determine list of Kubernetes resource types");
	std::vector<std::string> resourceTypes;
	std::istringstream ss(result.output);
	std::string item;
	while(std::getline(ss,item))
		resourceTypes.push_back(item);
	
	//for every type try to find every object matching the selector
	std::vector<std::string> baseArgs={"get","-o=jsonpath={.items[*].metadata.name}","-l="+selector};
	if(!nspace.empty())
		baseArgs.push_back("-n="+nspace);
	for(const auto& type : resourceTypes){
		auto args=baseArgs;
		args.insert(args.begin()+1,type);
		result=kubectl(clusterConfig, args);
		if(result.status!=0)
			throw std::runtime_error("Failed to list resources of type "+type);
		ss.str(result.output);
		ss.clear();
		while(ss >> item)
			objects.emplace(type,item);
	}
	return objects;
}

}
