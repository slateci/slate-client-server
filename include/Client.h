#ifndef SLATE_CLIENT_H
#define SLATE_CLIENT_H

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <iostream>
#include <functional>

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

struct upgradeOptions{
	bool assumeYes;
};

struct VOListOptions{
	bool user;
	
	VOListOptions():user(false){}
};

struct VOCreateOptions{
	std::string voName;
};

struct VODeleteOptions{
	std::string voName;
};

struct ClusterListOptions{
	std::string vo;
};

struct ClusterCreateOptions{
	std::string clusterName;
	std::string voName;
	std::string kubeconfig;
	bool assumeYes;
	
	ClusterCreateOptions():assumeYes(false){}
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

struct VOClusterAppUseListOptions{
	std::string clusterName;
	std::string voName;
};

struct VOClusterAppUseOptions{
	std::string clusterName;
	std::string voName;
	std::string appName;
};

struct ApplicationOptions{
	bool devRepo;
	bool testRepo;
	
	ApplicationOptions():devRepo(false),testRepo(false){}
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

struct InstanceDeleteOptions : public InstanceOptions{
	bool force;
	
	InstanceDeleteOptions():force(false){}
};

struct InstanceLogOptions : public InstanceOptions{
	unsigned long maxLines;
	std::string container;
	bool previousLogs;
	
	InstanceLogOptions():maxLines(20),previousLogs(false){}
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

struct SecretCopyOptions{
	std::string name;
	std::string vo;
	std::string cluster;
	std::string sourceID;
};

struct SecretDeleteOptions : public SecretOptions{
	bool force;
	
	SecretDeleteOptions():force(false){}
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
	///                    for underlines, bold, colors, etc.
	///\param outputWidth maximum number of columns to use for output. If zero, 
	///                   choose automatically, using the terminal width if 
	///                   stdout is a TTY or unlimited if it is not. 
	explicit Client(bool useANSICodes=true, std::size_t outputWidth=0);
	
	void setOutputWidth(std::size_t width);
	
	void setUseANSICodes(bool use);
	
	void printVersion();
	
	void upgrade(const upgradeOptions&);
	
	void createVO(const VOCreateOptions& opt);
	
	void deleteVO(const VODeleteOptions& opt);
	
	void listVOs(const VOListOptions& opt);
	
	void createCluster(const ClusterCreateOptions& opt);
	
	void deleteCluster(const ClusterDeleteOptions& opt);
	
	void listClusters(const ClusterListOptions& opt);
	
	void grantVOClusterAccess(const VOClusterAccessOptions& opt);
	
	void revokeVOClusterAccess(const VOClusterAccessOptions& opt);
	
	void listVOWithAccessToCluster(const ClusterAccessListOptions& opt);
	
	void listAllowedApplications(const VOClusterAppUseListOptions& opt);
	
	void allowVOUseOfApplication(const VOClusterAppUseOptions& opt);
	
	void denyVOUseOfApplication(const VOClusterAppUseOptions& opt);
	
	void listApplications(const ApplicationOptions& opt);
	
	void getApplicationConf(const ApplicationConfOptions& opt);
	
	void installApplication(const ApplicationInstallOptions& opt);
	
	void listInstances(const InstanceListOptions& opt);
	
	void getInstanceInfo(const InstanceOptions& opt);
	
	void deleteInstance(const InstanceDeleteOptions& opt);
	
	void fetchInstanceLogs(const InstanceLogOptions& opt);

	void listSecrets(const SecretListOptions& opt);

	void getSecretInfo(const SecretOptions& opt);
	
	void createSecret(const SecretCreateOptions& opt);
	
	void copySecret(const SecretCopyOptions& opt);

	void deleteSecret(const SecretDeleteOptions& opt);

	std::string orderBy = "Owner";
	
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

	struct ProgressManager{
	private:
	  bool stop_;
	  std::atomic<bool> showingProgress_;
	  std::atomic<bool> actuallyShowingProgress_;
	  unsigned int nestingLevel;
	  float progress_;
	  std::mutex mut_;
	  std::condition_variable cond_;
	  std::thread thread_;
	  std::chrono::system_clock::time_point progressStart_;
	  struct WorkItem{
	    std::chrono::system_clock::time_point time_;
	    std::function<void()> work_;
	    WorkItem(){}
	    WorkItem(std::chrono::system_clock::time_point t, std::function<void()> w);
	    bool operator<(const WorkItem&) const;
	  };
	  std::priority_queue<WorkItem> work_;
	  bool repeatWork_;

	  void start_scan_progress(std::string msg);
	  void scan_progress(int progress);
	  void show_progress();
	public:
	  bool verbose_;
	  
	  explicit ProgressManager();
	  ~ProgressManager();
    
	  void MaybeStartShowingProgress(std::string message);
	  ///\param value a fraction in [0,1]
	  void SetProgress(float value);
	  void ShowSomeProgress();
	  void StopShowingProgress();
	};
	
	///The progress bar manager
	ProgressManager pman_;

	void showError(const std::string& maybeJSON);
	
	std::string formatTable(const std::vector<std::vector<std::string>>& items,
	                        const std::vector<columnSpec>& columns,
				const bool headers) const;
	
	std::string jsonListToTable(const rapidjson::Value& jdata,
	                            const std::vector<columnSpec>& columns,
				    const std::vector<std::string>& labels,
				    const bool headers) const;

	std::string displayContents(const rapidjson::Value& jdata,
				    const std::vector<columnSpec>& columns,
				    const bool headers) const;
	
	std::string formatOutput(const rapidjson::Value& jdata, const rapidjson::Value& original,
				 const std::vector<columnSpec>& columns) const;
	
	///return true if the argument mtaches the correct format for an instance ID
	static bool verifyInstanceID(const std::string& id);
	///return true if the argument mtaches the correct format for a secret ID
	static bool verifySecretID(const std::string& id);
	
	static void filterInstanceNames(rapidjson::Document& json, std::string pointer);
	
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
