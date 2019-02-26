#include "KubeInterface.h"

#include <fstream>
#include <memory>
#include <string>

#include "Logging.h"
#include "Utilities.h"
#include "FileHandle.h"

///Remove ANSI escape sequences from a string. 
///This is hard to do generally, for now only CSI SGR sequences are identified
///and removed. These are by far the most common sequences in command output, 
///and _appear_ to be the only ones produced by kubectl. 
std::string removeShellEscapeSequences(std::string s){
	std::size_t pos=0;
	while((pos=s.find("\x1B[",pos))!=std::string::npos){
		std::size_t end=s.find('m',pos);
		if(end==std::string::npos)
			s.erase(pos);
		else
			s.erase(pos,end-pos+1);
	}
	return s;
}

namespace kubernetes{
	
commandResult kubectl(const std::string& configPath,
                      const std::vector<std::string>& arguments){
	std::vector<std::string> fullArgs;
	fullArgs.push_back("--request-timeout=10s");
	fullArgs.push_back("--kubeconfig="+configPath);
	std::copy(arguments.begin(),arguments.end(),std::back_inserter(fullArgs));
	auto result=runCommand("kubectl",fullArgs);
	return commandResult{removeShellEscapeSequences(result.output),
	                     removeShellEscapeSequences(result.error),result.status};
}
	
commandResult helm(const std::string& configPath,
                   const std::string& tillerNamespace,
                   const std::vector<std::string>& arguments){
	std::vector<std::string> fullArgs;
	fullArgs.push_back("--tiller-namespace="+tillerNamespace);
	fullArgs.push_back("--tiller-connection-timeout=10");
	std::copy(arguments.begin(),arguments.end(),std::back_inserter(fullArgs));
	return runCommand("helm",fullArgs,{{"KUBECONFIG",configPath}});
}

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

}
