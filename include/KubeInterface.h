#ifndef SLATE_KUBE_INTERFACE_H
#define SLATE_KUBE_INTERFACE_H

#include <string>

namespace kubernetes{
	std::string kubectl(const std::string& configPath, 
						const std::string& context, 
						const std::string& command);
}

#endif //SLATE_KUBE_INTERFACE_H