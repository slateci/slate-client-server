#ifndef SLATE_CLIENT_H
#define SLATE_CLIENT_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

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

struct GroupListOptions{
	bool user;
	
	GroupListOptions():user(false){}
};

struct GroupListAllowedOptions{
	std::string groupName;
};

struct GroupInfoOptions{
	std::string groupName;
};

struct GroupCreateOptions{
	std::string groupName;
	std::string scienceField;
};

struct GroupUpdateOptions{
	std::string groupName;
	std::string email;
	std::string phone;
	std::string scienceField;
	std::string description;
};

struct GroupDeleteOptions{
	std::string groupName;
	bool assumeYes;
	
	GroupDeleteOptions():assumeYes(false){}
};

struct ClusterListOptions{
	std::string group;
};

struct ClusterInfoOptions{
	std::string clusterName;
	bool all_nodes;
};

struct ClusterCreateOptions{
	std::string clusterName;
	std::string groupName;
	std::string orgName;
	std::string kubeconfig;
	bool assumeYes;
	bool assumeLoadBalancer;
	bool noIngress;
	
	ClusterCreateOptions():assumeYes(false),noIngress(false),
	assumeLoadBalancer(false){}
};

///A physical location on the Earth
struct GeoLocation{
	double lat, lon;
};

std::ostream& operator<<(std::ostream& os, const GeoLocation& gl);
std::istream& operator>>(std::istream& is, GeoLocation& gl);

struct ClusterOptions{
	std::string clusterName;
};

struct ClusterUpdateOptions : public ClusterOptions{
	std::string orgName;
	bool reconfigure;
	std::string kubeconfig;
	std::vector<GeoLocation> locations;
	bool assumeYes;
	
	ClusterUpdateOptions():reconfigure(false),assumeYes(false){}
};

struct ClusterDeleteOptions : public ClusterOptions{
	bool assumeYes;
	bool force;
	
	ClusterDeleteOptions():assumeYes(false),force(false){}
};

struct GroupClusterAccessOptions : public ClusterOptions{
	std::string groupName;
};

struct ClusterAccessListOptions : public ClusterOptions{
};

struct GroupClusterAppUseListOptions : public ClusterOptions{
	std::string groupName;
};

struct GroupClusterAppUseOptions : public ClusterOptions{
	std::string groupName;
	std::string appName;
};

struct ClusterPingOptions : public ClusterOptions{
};

struct ClusterComponentOptions{
	std::string componentName;
	std::string kubeconfig;
	std::string systemNamespace;
	
	ClusterComponentOptions();
};

struct ClusterComponentListOptions : public ClusterComponentOptions{
	bool verbose;
	
	ClusterComponentListOptions():verbose(false){}
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
	ApplicationInstallOptions():fromLocalChart(false){}

	std::string appName;
	std::string cluster;
	std::string group;
	std::string configPath;
	bool fromLocalChart;
};

struct InstanceListOptions{
	std::string group;
	std::string cluster;
};

struct InstanceOptions{
	std::string instanceID;
};

struct InstanceDeleteOptions : public InstanceOptions{
	bool force;
	bool assumeYes;
	
	InstanceDeleteOptions():force(false),assumeYes(false){}
};

struct InstanceLogOptions : public InstanceOptions{
	unsigned long maxLines;
	std::string container;
	bool previousLogs;
	
	InstanceLogOptions():maxLines(20),previousLogs(false){}
};

struct InstanceScaleOptions : public InstanceOptions{
	unsigned long instanceReplicas;
	std::string deployment;
	
	constexpr static unsigned long replicasNotSet=-1;
	
	InstanceScaleOptions():instanceReplicas(replicasNotSet){}
};

struct SecretListOptions{
	std::string group;
	std::string cluster;
};

struct SecretOptions{
	std::string secretID;
};

struct SecretCreateOptions{
	std::string name;
	std::string group;
	std::string cluster;
	std::vector<std::string> data;
};

struct SecretCopyOptions{
	std::string name;
	std::string group;
	std::string cluster;
	std::string sourceID;
};

struct UserOptions{
	std::string id;
	std::string group;
};

struct CreateUserOptions{
	std::string globID;
	std::string name;
	std::string email;
	std::string phone;
	std::string institution;
	bool admin;
};

struct UpdateUserOptions: public CreateUserOptions{
	std::string id;
};

struct SecretDeleteOptions : public SecretOptions{
	bool force;
	bool assumeYes;
	
	SecretDeleteOptions():force(false),assumeYes(false){}
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

///Represents that an operation has failed and any available information has 
///already been displayed to the user
struct OperationFailed : public std::exception{
	const char* what(){ return ""; }
};

struct InstallAborted : public std::runtime_error{
	explicit InstallAborted(std::string&& what):std::runtime_error(std::move(what)){}
};

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
	
	void createGroup(const GroupCreateOptions& opt);
	
	void updateGroup(const GroupUpdateOptions& opt);
	
	void deleteGroup(const GroupDeleteOptions& opt);
	
	void getGroupInfo(const GroupInfoOptions& opt);
	
	void listGroups(const GroupListOptions& opt);
	
	void createCluster(const ClusterCreateOptions& opt);
	
	void updateCluster(const ClusterUpdateOptions& opt);
	
	void deleteCluster(const ClusterDeleteOptions& opt);
	
	void listClusters(const ClusterListOptions& opt);

	void listClustersAccessibleToGroup(const GroupListAllowedOptions& opt);
	
	void getClusterInfo(const ClusterInfoOptions& opt);
	
	void grantGroupClusterAccess(const GroupClusterAccessOptions& opt);
	
	void revokeGroupClusterAccess(const GroupClusterAccessOptions& opt);
	
	void listGroupWithAccessToCluster(const ClusterAccessListOptions& opt);
	
	void listAllowedApplications(const GroupClusterAppUseListOptions& opt);
	
	void allowGroupUseOfApplication(const GroupClusterAppUseOptions& opt);
	
	void denyGroupUseOfApplication(const GroupClusterAppUseOptions& opt);
	
	void pingCluster(const ClusterPingOptions& opt);
	
	void listClusterComponents() const;
	
	void listInstalledClusterComponents(const ClusterComponentListOptions& opt) const;
	
	void checkClusterComponent(const ClusterComponentOptions& opt) const;
	
	void addClusterComponent(const ClusterComponentOptions& opt) const;
	
	void removeClusterComponent(const ClusterComponentOptions& opt) const;
	
	void upgradeClusterComponent(const ClusterComponentOptions& opt) const;
	
	void listApplications(const ApplicationOptions& opt);
	
	void getApplicationConf(const ApplicationConfOptions& opt);
	
	void getApplicationDocs(const ApplicationConfOptions& opt);
	
	void installApplication(const ApplicationInstallOptions& opt);
	
	void listInstances(const InstanceListOptions& opt);
	
	void getInstanceInfo(const InstanceOptions& opt);
	
	void restartInstance(const InstanceOptions& opt);
	
	void deleteInstance(const InstanceDeleteOptions& opt);
	
	void fetchInstanceLogs(const InstanceLogOptions& opt);

	void scaleInstance(const InstanceScaleOptions& opt);

	void fetchCurrentProfile();

	void getUserInfo(const UserOptions& opt);

	void listUsers(const UserOptions& opt);

	void listUserGroups(const UserOptions& opt);

	void createUser(const CreateUserOptions& opt);

	void removeUser(const UserOptions& opt);

	void updateUser(const UpdateUserOptions& opt);

	void addUserToGroup(const UserOptions& opt);

	void removeUserFromGroup(const UserOptions& opt);

	void updateUserToken(const UserOptions& opt);

	void listSecrets(const SecretListOptions& opt);

	void getSecretInfo(const SecretOptions& opt);
	
	void createSecret(const SecretCreateOptions& opt);
	
	void copySecret(const SecretCopyOptions& opt);

	void deleteSecret(const SecretDeleteOptions& opt);

	bool clientShouldPrintOnlyJson() const;
	
private:
	struct ClusterConfig{
		///The name of slate's system namespace, selected by the user
		std::string namespaceName;
		///The kubeconfig data necessary to use the slate service account
		std::string serviceAccountCredentials;
	};

	///\param configPath the filesystem path to the user's selected kubeconfig. If
	///                  empty, attempt autodetection. 
	///\param assumeYes assume yes/default for questions which would be asked 
	///                 interactively of the user
	///\return the user-selected and automatically generated configuration data
	ClusterConfig extractClusterConfig(std::string configPath, bool assumeYes);
	
	void ensureNRPController(const std::string& configPath, bool assumeYes);
	
	void ensureRBAC(const std::string& configPath, bool assumeYes);
	bool checkLoadBalancer(const std::string& configPath, bool assumeYes);

	template<typename OptionsType>
	void retryInstanceCommandWithFixup(void (Client::* command)(const OptionsType&), OptionsType opt);
	
	///Figure out the correct kubeconfig path to use, starting from a possible 
	///value provided by the user, and falling back appropriately to the 
	///environment ($KUBECONFIG and ~/.kube/config) if that is not set.
	///\return the first path found in the fallback sequence
	///\throws std::runtime_error if the specified path does not refer to a 
	///        readable file
	std::string getKubeconfigPath(std::string configPath) const;

	///Get the default path to the user's API endpoint file
	std::string getDefaultEndpointFilePath() const;
	///Get the default path to the user's credential file
	std::string getDefaultCredFilePath() const;
	
	std::string fetchStoredCredentials() const;

	void updateStoredCredentials(std::string token);
	
	std::string getToken() const;
	
	std::string getEndpoint() const;
	
	std::string makeURL(const std::string& path) const{
		return(getEndpoint()+"/"+apiVersion+"/"+path+"?token="+getToken());
	}
	
	httpRequests::Options defaultOptions() const;
	rapidjson::Document getClusterList(std::string group);
	
#ifdef USE_CURLOPT_CAINFO
	void detectCABundlePath() const;
#endif
	
	std::string underline(std::string s) const;
	std::string bold(std::string s) const;
	
	struct columnSpec{
		columnSpec(std::string lab, std::string attr, bool canWrap=false, bool optional=false):
		label(lab),attribute(attr),allowWrap(canWrap),optional(optional){}
		
		std::string label;
		std::string attribute;
		bool allowWrap;
		bool optional;
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
	  std::atomic<bool> verbose_;
	  
	  explicit ProgressManager();
	  ~ProgressManager();
    
	  void MaybeStartShowingProgress(std::string message);
	  ///\param value a fraction in [0,1]
	  void SetProgress(float value);
	  void ShowSomeProgress();
	  void StopShowingProgress();
	};
	
	///The progress bar manager
	mutable ProgressManager pman_;
	
	struct ProgressToken{
		ProgressManager& pman;
		ProgressToken(ProgressManager& pman, const std::string& msg):pman(pman){
			start(msg);
		}
		~ProgressToken(){ end(); }
		void start(const std::string& msg){
			pman.MaybeStartShowingProgress(msg);
			pman.ShowSomeProgress();
		}
		void end(){ pman.StopShowingProgress(); }
	};
public:
	///An object during whose lifetime the CLient's progress indication is 
	///temporarily paused
	struct HideProgress{
		ProgressManager& pman;
		bool orig;
		HideProgress(ProgressManager& pman):pman(pman),orig(pman.verbose_){
			pman.verbose_=false;
		}
		~HideProgress(){ pman.verbose_=orig; }
	};
private:

	void showError(const std::string& maybeJSON);
	
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
	
	///return true if the argument matches the correct format for an instance ID
	static bool verifyInstanceID(const std::string& id);
	///return true if the argument matches the correct format for a user ID
	static bool verifyUserID(const std::string& id);
	///return true if the argument matches the correct format for a group ID
	static bool verifyGroupID(const std::string& id);
	///return true if the argument matches the correct format for a secret ID
	static bool verifySecretID(const std::string& id);
	
	mutable std::string endpointPath;
	mutable std::string apiEndpoint;
	std::string apiVersion;
	mutable std::string credentialPath;
	mutable std::string token;
	bool useANSICodes;
	bool enableFixup;
	std::size_t outputWidth;
	std::string outputFormat;
	std::string orderBy = "";
#ifdef USE_CURLOPT_CAINFO
	mutable std::string caBundlePath;
#endif
	
	friend void registerCommonOptions(CLI::App&, Client&);
	
	const static std::string defaultSystemNamespace;
	
	friend struct ClusterComponentOptions;
	
	struct ClusterComponent{
		enum ComponentStatus{
			NotInstalled,
			OutOfDate,
			UpToDate
		};
	
		std::string description;
		std::string currentVersion;
		ComponentStatus (Client::*check)(const std::string& configPath, const std::string& systemNamespace) const;
		void (Client::*install)(const std::string& configPath, const std::string& systemNamespace) const;
		void (Client::*remove)(const std::string& configPath, const std::string& systemNamespace) const;
		void (Client::*upgrade)(const std::string& configPath, const std::string& systemNamespace) const;
		void (Client::*ensure)(const std::string& configPath, const std::string& systemNamespace, bool) const;
	};
	
	std::map<std::string,ClusterComponent> clusterComponents;
	
	//Federation RBAC
	const static std::string federationRoleURL;
	ClusterComponent::ComponentStatus checkFederationRBAC(const std::string& configPath, const std::string& systemNamespace) const;
	void installFederationRBAC(const std::string& configPath, const std::string& systemNamespace) const;
	void removeFederationRBAC(const std::string& configPath, const std::string& systemNamespace) const;
	
	//Ingress Controller
	ClusterComponent::ComponentStatus checkIngressController(const std::string& configPath, const std::string& systemNamespace) const;
	void installIngressController(const std::string& configPath, const std::string& systemNamespace) const;
	void removeIngressController(const std::string& configPath, const std::string& systemNamespace) const;
	void upgradeIngressController(const std::string& configPath, const std::string& systemNamespace) const;
	std::string getIngressControllerAddress(const std::string& configPath, const std::string& systemNamespace) const;
	void ensureIngressController(const std::string& configPath, const std::string& systemNamespace, bool assumeYes) const;
	
	//Prometheus monitoring
	ClusterComponent::ComponentStatus checkPrometheusMonitoring(const std::string& configPath, const std::string& systemNamespace) const;
	void installPrometheusMonitoring(const std::string& configPath, const std::string& systemNamespace) const;
	void removePrometheusMonitoring(const std::string& configPath, const std::string& systemNamespace) const;
	void upgradePrometheusMonitoring(const std::string& configPath, const std::string& systemNamespace) const;
};

#endif
