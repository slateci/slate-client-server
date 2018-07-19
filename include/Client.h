#ifndef SLATE_CLIENT_H
#define SLATE_CLIENT_H

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

#include "rapidjson/document.h"
#include "rapidjson/pointer.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "HTTPRequests.h"

struct VOCreateOptions{
	std::string voName;
};

struct VODeleteOptions{
	std::string voName;
};

struct ClusterCreateOptions{
	std::string clusterName;
	std::string voName;
	std::string kubeconfig;
};

struct ClusterDeleteOptions{
	std::string clusterName;
};

struct ApplicationOptions{
	bool devRepo;
};

struct ApplicationConfOptions : public ApplicationOptions{
	std::string appName;
	std::string outputFile;
};

struct ApplicationInstallOptions : public ApplicationOptions{
	std::string appName;
	std::string cluster;
	std::string vo;
	std::string tag;
	std::string configPath;
};

struct InstanceListOptions{
	std::string vo;
	std::string cluster;
};

struct InstanceOptions{
	std::string instanceID;
};

///Try to get the value of an enviroment variable and store it to a string object.
///If the variable was not set \p target will not be modified. 
///\param name the name of the environment variable to get
///\param target the variable into which the environment variable should be 
///              copied, if set
///\return whether the environment variable was set
bool fetchFromEnvironment(const std::string& name, std::string& target);

namespace CLI{
	class App; //fwd decl
}

class Client{
public:
	///\param useANSICodes if true and stdout is a TTY, use ANSI formatting
	///                    for underlines, bold, colrs, etc.
	///\param outputWidth maximum number of columns to use for output. If zero, 
	///                   choose automatically, using the terminal width if 
	///                   stdout is a TTY or unlimited if it is not. 
	explicit Client(bool useANSICodes=true, std::size_t outputWidth=0);
	
	void setOutputWidth(std::size_t width);
	
	void setUseANSICodes(bool use);
	
	void createVO(const VOCreateOptions& opt);
	
	void deleteVO(const VODeleteOptions& opt);
	
	void listVOs();
	
	void createCluster(const ClusterCreateOptions& opt);
	
	void deleteCluster(const ClusterDeleteOptions& opt);
	
	void listClusters();
	
	void listApplications(const ApplicationOptions& opt);
	
	void getApplicationConf(const ApplicationConfOptions& opt);
	
	void installApplication(const ApplicationInstallOptions& opt);
	
	void listInstances(const InstanceListOptions& opt);
	
	void getInstanceInfo(const InstanceOptions& opt);
	
	void deleteInstance(const InstanceOptions& opt);
	
private:
	///Get the default path to the user's credential file
	std::string getDefaultCredFilePath();
	
	std::string fetchStoredCredentials();
	
	std::string getToken();
	
	std::string makeURL(const std::string& path){
		return(apiEndpoint+"/"+apiVersion+"/"+path+"?token="+getToken());
	}
	
	std::string underline(std::string s) const;
	std::string bold(std::string s) const;
	
	struct columnSpec{
		columnSpec(std::string lab, std::string attr, bool canWrap=false):
		label(lab),attribute(attr),allowWrap(canWrap){}
		
		std::string label;
		std::string attribute;
		bool allowWrap;
	};
	
	std::string formatTable(const std::vector<std::vector<std::string>>& items,
	                        const std::vector<columnSpec>& columns) const;
	
	std::string jsonListToTable(const rapidjson::Value& jdata,
	                            const std::vector<columnSpec>& columns) const;
	
	std::string apiEndpoint;
	std::string apiVersion;
	std::string credentialPath;
	std::string token;
	bool useANSICodes;
	std::size_t outputWidth;
	
	friend void registerCommonOptions(CLI::App&, Client&);
};

#endif
