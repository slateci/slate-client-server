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

#if ! ( __APPLE__ && __MACH__ )
	//Whether to use CURLOPT_CAINFO to specifiy a CA bundle path.
	//According to https://curl.haxx.se/libcurl/c/CURLOPT_CAINFO.html
	//this should not be used on Mac OS
	#define USE_CURLOPT_CAINFO
#endif

struct VOListOptions{
	bool user;
};

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

struct VOClusterAccessOptions{
	std::string clusterName;
	std::string voName;
};

struct ClusterAccessListOptions{
	std::string clusterName;
};

struct ApplicationOptions{
	bool devRepo;
	bool testRepo;
};

struct ApplicationConfOptions : public ApplicationOptions{
	std::string appName;
	std::string outputFile;
};

struct ApplicationInstallOptions : public ApplicationOptions{
	std::string appName;
	std::string cluster;
	std::string vo;
	std::string configPath;
};

struct InstanceListOptions{
	std::string vo;
	std::string cluster;
};

struct InstanceOptions{
	std::string instanceID;
};

struct SecretListOptions{
	std::string vo;
	std::string cluster;
};

struct SecretOptions{
	std::string secretID;
};

struct SecretCreateOptions{
	std::string name;
	std::string vo;
	std::string cluster;
	std::vector<std::string> data;
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
	
	void listVOs(const VOListOptions& opt);
	
	void createCluster(const ClusterCreateOptions& opt);
	
	void deleteCluster(const ClusterDeleteOptions& opt);
	
	void listClusters();
	
	void grantVOClusterAccess(const VOClusterAccessOptions& opt);
	
	void revokeVOClusterAccess(const VOClusterAccessOptions& opt);
	
	void listVOWithAccessToCluster(const ClusterAccessListOptions& opt);
	
	void listApplications(const ApplicationOptions& opt);
	
	void getApplicationConf(const ApplicationConfOptions& opt);
	
	void installApplication(const ApplicationInstallOptions& opt);
	
	void listInstances(const InstanceListOptions& opt);
	
	void getInstanceInfo(const InstanceOptions& opt);
	
	void deleteInstance(const InstanceOptions& opt);

	void listSecrets(const SecretListOptions& opt);

	void getSecretInfo(const SecretOptions& opt);
	
	void createSecret(const SecretCreateOptions& opt);

	void deleteSecret(const SecretOptions& opt);
	
private:
	///Get the default path to the user's API endpoint file
	std::string getDefaultEndpointFilePath();
	///Get the default path to the user's credential file
	std::string getDefaultCredFilePath();
	
	std::string fetchStoredCredentials();
	
	std::string getToken();
	
	std::string getEndpoint();
	
	std::string makeURL(const std::string& path){
		return(getEndpoint()+"/"+apiVersion+"/"+path+"?token="+getToken());
	}
	
	httpRequests::Options defaultOptions();
	
#ifdef USE_CURLOPT_CAINFO
	void detectCABundlePath();
#endif
	
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
	                        const std::vector<columnSpec>& columns,
				const bool headers) const;
	
	std::string jsonListToTable(const rapidjson::Value& jdata,
	                            const std::vector<columnSpec>& columns,
				    const bool headers) const;

	std::string displayContents(const rapidjson::Value& jdata,
				    const std::vector<columnSpec>& columns,
				    const bool headers) const;
	
	std::string formatOutput(const rapidjson::Value& jdata, const rapidjson::Value& original,
				 const std::vector<columnSpec>& columns) const;
	
	std::string endpointPath;
	std::string apiEndpoint;
	std::string apiVersion;
	std::string credentialPath;
	std::string token;
	bool useANSICodes;
	std::size_t outputWidth;
	std::string outputFormat;
#ifdef USE_CURLOPT_CAINFO
	std::string caBundlePath;
#endif
	
	friend void registerCommonOptions(CLI::App&, Client&);
};

#endif
