#include "KubeInterface.h"

#include <memory>
#include <string>
#include <iostream>
#include <sstream>

#include "Utilities.h"
#include "FileHandle.h"
#ifdef SLATE_SERVER
#include "Telemetry.h"
#endif

namespace kubernetes{
	
commandResult kubectl(const std::string& configPath,
                      const std::vector<std::string>& arguments){
#ifdef SLATE_SERVER
	auto tracer = getTracer();
	auto span = tracer->StartSpan("kubectl");
	auto scope = tracer->WithActiveSpan(span);
#endif
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
#ifdef SLATE_SERVER
	span->SetAttribute("log.message", cmd.str());
#endif
	auto result=runCommand("kubectl",fullArgs);
#ifdef SLATE_SERVER
	span->End();
#endif
	return commandResult{removeShellEscapeSequences(result.output),
	                     removeShellEscapeSequences(result.error),result.status};
}

int getControllerVersion(const std::string& clusterConfig) {
#ifdef SLATE_SERVER
	auto tracer = getTracer();
	auto span = tracer->StartSpan("getControllerVersion");
	auto scope = tracer->WithActiveSpan(span);
#endif
    auto result=runCommand("kubectl",{"--kubeconfig",clusterConfig,"get", "crd", "clusternss.slateci.io"});

    if (result.output.find("CREATED AT") != std::string::npos) {
        std::cerr << "Cluster using federation controller" << std::endl;
        // if clusternss is found, we're talking to a cluster with the new version of the controller
#ifdef SLATE_SERVER
		span->End();
#endif
        return 2;
    }
    std::cerr << "Cluster using nrp controller" << std::endl;
#ifdef SLATE_SERVER
	span->End();
#endif
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

#ifdef SLATE_SERVER
	auto tracer = getTracer();
	auto span = tracer->StartSpan("helm");
	auto scope = tracer->WithActiveSpan(span);
#endif

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
#ifdef SLATE_SERVER
	span->SetAttribute("log.message", cmd.str());
#endif

	auto result = runCommand("helm",fullArgs,{{"KUBECONFIG",configPath}});
#ifdef SLATE_SERVER
	span->End();
#endif
	return result;
}

unsigned int getHelmMajorVersion(){
#ifdef SLATE_SERVER
	auto tracer = getTracer();
	auto span = tracer->StartSpan("getHelmMajorVersion");
	auto scope = tracer->WithActiveSpan(span);
	span->SetAttribute("log.message", "helm version");
#endif
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
#ifdef SLATE_SERVER
			setSpanError(span, err);
#endif
			throw std::runtime_error(err);
		}
	}
	if(!helmMajorVersion) {
		const std::string& err = "Unable to extract helm version";
#ifdef SLATE_SERVER
		setSpanError(span, err);
#endif
		throw std::runtime_error(err);
	}
#ifdef SLATE_SERVER
	span->End();
#endif
	return helmMajorVersion;
}

std::multimap<std::string,std::string> findAll(const std::string& clusterConfig, const std::string& selector,
											   const std::string& nspace, const std::string& verbs){
#ifdef SLATE_SERVER
	auto tracer = getTracer();
	auto span = tracer->StartSpan("findAll");
	auto scope = tracer->WithActiveSpan(span);
#endif

	std::multimap<std::string,std::string> objects;

	//first determine all possible API resource types
	auto result=kubectl(clusterConfig, {"api-resources","-o=name","--verbs="+verbs});
	if(result.status!=0) {
#ifdef SLATE_SERVER
		setSpanError(span, "Failed to determine list of Kubernetes resource types");
		span->End();
#endif
		throw std::runtime_error("Failed to determine list of Kubernetes resource types");
	}
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
		if(result.status!=0) {
#ifdef SLATE_SERVER
			setSpanError(span, "Failed to list resources of type " + type);
			span->End();
#endif
			throw std::runtime_error("Failed to list resources of type " + type);
		}
		ss.str(result.output);
		ss.clear();
		while(ss >> item)
			objects.emplace(type,item);
	}

#ifdef SLATE_SERVER
	span->End();
#endif
	return objects;
}

}
