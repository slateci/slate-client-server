#ifndef SLATE_KUBE_INTERFACE_H
#define SLATE_KUBE_INTERFACE_H

#include <string>
#include "Entities.h"

namespace kubernetes{
	std::string kubectl(const std::string& configPath, 
						const std::string& context, 
						const std::string& command);

	void kubectl_create_namespace(const std::string& clusterConfig, const VO& vo);

	void kubectl_delete_namespace(const std::string& clusterConfig, const VO& vo);
}

#endif //SLATE_KUBE_INTERFACE_H
