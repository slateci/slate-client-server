#ifndef SLATE_KUBE_INTERFACE_H
#define SLATE_KUBE_INTERFACE_H

#include <map>
#include <string>
#include "Entities.h"
#include "Process.h"

namespace kubernetes{
	commandResult kubectl(const std::string& configPath,
	                      const std::vector<std::string>& arguments);
	
	commandResult helm(const std::string& configPath,
	                   const std::string& tillerNamespace,
	                   const std::vector<std::string>& arguments);

	void kubectl_create_namespace(const std::string& clusterConfig, const Group& group);

	void kubectl_delete_namespace(const std::string& clusterConfig, const Group& group);

	///Collect the types and names of all objects matching a selector
	///
	///This function is very inefficient and should be avoided whenever possible, 
	///as it must issue a separate kubectl command for every resource type known
	///on the cluster, whether or not there turn out to be any relevant resources 
	///of that type. 
	///\param clusterConfig path to the kubeconfig file
	///\param selector the selector expression to use for filtering
	///\param nspace the namespace in which to search. Non-namespaced resources 
	///              are always found regardless of this parameter's value.
	std::multimap<std::string,std::string> findAll(const std::string& clusterConfig, 
	                                               const std::string& selector, 
	                                               const std::string nspace);
}

#endif //SLATE_KUBE_INTERFACE_H
