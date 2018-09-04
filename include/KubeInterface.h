#ifndef SLATE_KUBE_INTERFACE_H
#define SLATE_KUBE_INTERFACE_H

#include <string>
#include "Entities.h"
#include "Utilities.h"

namespace kubernetes{
	commandResult kubectl(const std::string& configPath,
	                      const std::vector<std::string>& arguments);

	void kubectl_create_namespace(const std::string& clusterConfig, const VO& vo);

	void kubectl_delete_namespace(const std::string& clusterConfig, const VO& vo);
}

#endif //SLATE_KUBE_INTERFACE_H
