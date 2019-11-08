#ifndef SLATE_KUBE_INTERFACE_H
#define SLATE_KUBE_INTERFACE_H

#include <string>
#include "Entities.h"
#include "Process.h"

namespace kubernetes{
	commandResult kubectl(const std::string& configPath,
	                      const std::vector<std::string>& arguments);
	
	commandResult helm(const std::string& configPath,
	                   const std::string& tillerNamespace,
	                   const std::vector<std::string>& arguments);

	///\param clusterConfig path to the kubernetes config file corresponding to 
	///                     the target cluster
	///\param group the Group whose namespace should be created
	void kubectl_create_namespace(const std::string& clusterConfig, const Group& group);

	///\param clusterConfig path to the kubernetes config file corresponding to 
	///                     the target cluster
	///\param group the Group whose namespace should be removed
	void kubectl_delete_namespace(const std::string& clusterConfig, const Group& group);
	
	///\return the major component of the installed Helm's current version number
	unsigned int getHelmMajorVersion();
}

#endif //SLATE_KUBE_INTERFACE_H
