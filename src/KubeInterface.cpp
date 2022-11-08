#include "KubeInterface.h"

#include <memory>
#include <string>
#include <iostream>

#include "Utilities.h"
#include "FileHandle.h"
#include "Telemetry.h"

namespace kubernetes{
	
commandResult kubectl(const std::string& configPath,
                      const std::vector<std::string>& arguments){

	auto tracer = getTracer();
	auto span = tracer->StartSpan("kubectl");
	auto scope = tracer->WithActiveSpan(span);

	std::vector<std::string> fullArgs;
	fullArgs.push_back("--request-timeout=10s");
	if(!configPath.empty())
		fullArgs.push_back("--kubeconfig="+configPath);
	std::copy(arguments.begin(),arguments.end(),std::back_inserter(fullArgs));
	std::ostringstream cmd;
	cmd << "kubectl";
	for (std::vector<std::string>::const_iterator i = fullArgs.begin(); i != fullArgs.end(); ++i ) {
		cmd << " " << *i;
	}
	span->SetAttribute("log.message", cmd.str());
	auto result=runCommand("kubectl",fullArgs);
	span->End();
	return commandResult{removeShellEscapeSequences(result.output),
	                     removeShellEscapeSequences(result.error),result.status};
}

int getControllerVersion(const std::string& clusterConfig) {
	auto tracer = getTracer();
	auto span = tracer->StartSpan("getControllerVersion");
	auto scope = tracer->WithActiveSpan(span);

    auto result=runCommand("kubectl",{"--kubeconfig",clusterConfig,"get", "crd", "clusternss.slateci.io"});

    if (result.output.find("CREATED AT") != std::string::npos) {
        std::cerr << "Cluster using federation controller" << std::endl;
        // if clusternss is found, we're talking to a cluster with the new version of the controller
        return 2;
    }
    std::cerr << "Cluster using nrp controller" << std::endl;
	span->End();
    return 1;
}

#ifdef SLATE_SERVER
void kubectl_create_namespace(const std::string& clusterConfig, const Group& group) {

    std::string input = "";
    if (getControllerVersion(clusterConfig) == 1) {
        std::cerr << "Using old controller defs" << std::endl;
        input = R"(apiVersion: nrp-nautilus.io/v1alpha1
kind: ClusterNamespace
metadata:
  name: )"+group.namespaceName()+"\n";
    } else {
        std::cerr << "Using news controller defs" << std::endl;
        input = R"(apiVersion: slateci.io/v1alpha2
kind: ClusterNS
metadata:
  name: )"+group.namespaceName()+"\n";

    }

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
    commandResult result;
    if (getControllerVersion(clusterConfig) == 1) {
        result = runCommand("kubectl", {"--kubeconfig", clusterConfig,
                                             "delete", "clusternamespace", group.namespaceName()});
    } else {
        result = runCommand("kubectl", {"--kubeconfig", clusterConfig,
                                             "delete", "clusterNS", group.namespaceName()});

    }
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

	auto tracer = getTracer();
	auto span = tracer->StartSpan("helm");
	auto scope = tracer->WithActiveSpan(span);

	std::vector<std::string> fullArgs;
	if(getHelmMajorVersion()==2){
		fullArgs.push_back("--tiller-namespace="+tillerNamespace);
		fullArgs.push_back("--tiller-connection-timeout=10");
	}
	std::copy(arguments.begin(),arguments.end(),std::back_inserter(fullArgs));

	std::ostringstream cmd;
	cmd << "kubectl";
	for (std::vector<std::string>::const_iterator i = fullArgs.begin(); i != fullArgs.end(); ++i ) {
		cmd << " " << *i;
	}
	span->SetAttribute("log.message", cmd.str());

	auto result = runCommand("helm",fullArgs,{{"KUBECONFIG",configPath}});
	span->End();
	return result;
}

unsigned int getHelmMajorVersion(){
	auto tracer = getTracer();
	auto span = tracer->StartSpan("getHelmMajorVersion");
	auto scope = tracer->WithActiveSpan(span);
	span->SetAttribute("log.message", "helm version");
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
			const std::string& err = "Unable to extract helm version";
			setSpanError(span, err);
			throw std::runtime_error(err);
		}
	}
	if(!helmMajorVersion) {
		const std::string& err = "Unable to extract helm version";
		setSpanError(span, err);
		throw std::runtime_error(err);
	}
	span->End();
	return helmMajorVersion;
}

std::multimap<std::string,std::string> findAll(const std::string& clusterConfig, const std::string& selector,
											   const std::string& nspace, const std::string& verbs){

	auto tracer = getTracer();
	auto span = tracer->StartSpan("findAll");
	auto scope = tracer->WithActiveSpan(span);

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
	span->End();
	return objects;
}

}
