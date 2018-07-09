#include "KubeInterface.h"

#include <array>
#include <cstdio> //popen, pclose, fgets, feof
#include <memory>
#include <string>

#include "Logging.h"
#include "Utilities.h"

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

std::string kubectl(const std::string& configPath, const std::string& context, 
					const std::string& command){
	std::string fullCommand="kubectl ";
	//kubectl seems to default to a very liberal 2 minute timeout
	//Reduce that to something more managable
	fullCommand+="--request-timeout=10s ";
	fullCommand+="--kubeconfig='"+configPath+"' ";
	if(!context.empty())
		fullCommand+="--context='"+context+"' ";
	fullCommand+=command;
	return removeShellEscapeSequences(runCommand(fullCommand));
}

void kubectl_create_namespace(const std::string& clusterConfig, const VO& vo) {
  runCommand("kubectl --kubeconfig " + clusterConfig + " create namespace " + vo.namespaceName());	
}

void kubectl_delete_namespace(const std::string& clusterConfig, const VO& vo) {
  runCommand("kubectl --kubeconfig " + clusterConfig + " delete namespace " + vo.namespaceName());	
}

}
