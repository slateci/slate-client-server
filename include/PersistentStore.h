#ifndef SLATE_PERSISTENT_STORE_H
#define SLATE_PERSISTENT_STORE_H

#include <atomic>
#include <memory>
#include <set>
#include <string>

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/dynamodb/DynamoDBClient.h>
#include <aws/route53/Route53Client.h>

#include <libcuckoo/cuckoohash_map.hh>

#include <concurrent_multimap.h>
#include <DNSManipulator.h>
#include <Entities.h>
#include <FileHandle.h>
#include <Geocoder.h>

//In libstdc++ versions < 5 std::atomic seems to be broken for non-integral types
//In that case, we must use our own, minimal replacement
#ifdef __GNUC__
	#ifndef __clang__ //clang also sets __GNUC__, unfortunately
		#if __GNUC__ < 5
			#include <atomic_shim.h>
			#define slate_atomic simple_atomic
		#endif
	#endif
#endif
//In all other circustances we want to use the standard library
#ifndef slate_atomic
	#define slate_atomic std::atomic
#endif

///A wrapper type for tracking cached records which must be considered 
///expired after some time
template <typename RecordType>
struct CacheRecord{
	using steady_clock=std::chrono::steady_clock;
	
	using value_type=RecordType;
	
	///default construct a record which is considered expired/invalid
	CacheRecord():expirationTime(steady_clock::time_point::min()){}
	
	///construct a record which is considered expired but contains data
	///\param record the cached data
	CacheRecord(const value_type& record):
	record(record),expirationTime(steady_clock::time_point::min()){}
	
	///\param record the cached data
	///\param exprTime the time after which the record expires
	CacheRecord(const value_type& record, steady_clock::time_point exprTime):
	record(record),expirationTime(exprTime){}
	
	///\param validity duration until the record expires
	template <typename DurationType>
	CacheRecord(const value_type& record, DurationType validity):
	record(record),expirationTime(steady_clock::now()+validity){}
	
	///\param exprTime the time after which the record expires
	CacheRecord(value_type&& record, steady_clock::time_point exprTime):
	record(std::move(record)),expirationTime(exprTime){}
	
	///\param validity duration until the record expires
	template <typename DurationType>
	CacheRecord(value_type&& record, DurationType validity):
	record(std::move(record)),expirationTime(steady_clock::now()+validity){}
	
	///\return whether the record's expiration time has passed and it should 
	///        be discarded
	bool expired() const{ return (steady_clock::now() > expirationTime); }
	///\return whether the record has not yet expired, so it is still valid
	///        for use
	operator bool() const{ return (steady_clock::now() <= expirationTime); }
	///Implicit conversion to value_type
	///\return the data stored in the record
	///This function is not available when it would be ambiguous because the 
	///stored data type is also bool.
	template<typename ConvType = value_type>
	operator typename std::enable_if<!std::is_same<ConvType,bool>::value,ConvType>::type() const{ return record; }
	
	//The cached data
	value_type record;
	///The time at which the cached data should be discarded
	steady_clock::time_point expirationTime;
};

///Two cache records are equivalent if their contained data is equal, regardless
///of expiration times
template <typename T>
bool operator==(const CacheRecord<T>& r1, const CacheRecord<T>& r2){
	return (const T&)r1==(const T&)r2;
}

namespace std{
///The hash of a cache record is simply the hash of its stored data; the 
///expiration time is irrelevant.
template<typename T>
struct hash<CacheRecord<T>> : public std::hash<T>{};
	
///Define the hash of a set as the xor of the hashes of the items it contains. 
template<typename T>
struct hash<std::set<T>>{
	using result_type=std::size_t;
	using argument_type=std::set<T>;
	result_type operator()(const argument_type& t) const{
		result_type result=0;
		for(const auto& item : t)
			result^=std::hash<T>{}(item);
		return result;
	}
};
}

///An interface for sending email with the MailGun service
class EmailClient{
public:
	struct Email{
		std::string fromAddress;
		std::vector<std::string> toAddresses;
		std::vector<std::string> ccAddresses;
		std::vector<std::string> bccAddresses;
		std::string replyTo;
		std::string subject;
		std::string body;
	};

	EmailClient():valid(false){}
	EmailClient(const std::string& mailgunEndpoint, 
	            const std::string& mailgunKey, const std::string& emailDomain);
	bool canSendEmail() const{ return valid; }
	bool sendEmail(const Email& email);
private:
	std::string mailgunEndpoint;
	std::string mailgunKey;
	std::string emailDomain;
	bool valid;
};

class PersistentStore{
public:
	///\param credentials the AWS credentials used for authenitcation with the 
	///                   database
	///\param clientConfig specification of the database endpoint to contact
	///\param bootstrapUserFile the path from which the initial portal user
	///                         (superuser) credentials should be loaded
	///\param encryptionKeyFile the path to the file from which the encryption 
	///                         key used to protect secrets should be loaded
	///\param appLoggingServerName server to which application instances should 
	///                            send monitoring data
	///\param appLoggingServerPort port to which application instances should 
	///                            send monitoring data
	PersistentStore(const Aws::Auth::AWSCredentials& credentials, 
	                const Aws::Client::ClientConfiguration& clientConfig,
	                std::string bootstrapUserFile,
	                std::string encryptionKeyFile,
	                std::string appLoggingServerName,
	                unsigned int appLoggingServerPort);
	
	///Store a record for a new user
	///\return Whether the user record was successfully added to the database
	bool addUser(const User& user);
	
	///Find information about the user with a given ID
	///\param id the users ID
	///\return the corresponding user or an invalid user object if the id is not known
	User getUser(const std::string& id);
	
	///Find the user who owns the given access token. Currently does not bother 
	///to retreive the user's name, email address, or globus ID. 
	///\param token access token
	///\return the token owner or an invalid user object if the token is not known
	User findUserByToken(const std::string& token);
	
	///Find the user corresponding to the given Globus ID. Currently does not bother 
	///to retreive the user's name, email address, or admin status. 
	///\param globusID Globus ID to look up
	///\return the corresponding user or an invalid user object if the ID is not known
	User findUserByGlobusID(const std::string& globusID);
	
	///Change a user record
	///\param user the updated user record, with an ID matching the previous ID
	///\param user the previous user record
	///\return Whether the user record was successfully altered in the database
	bool updateUser(const User& user, const User& oldUser);
	
	///Delete a user record
	///\param id the ID of the user to delete
	///\return Whether the user record was successfully removed from the database
	bool removeUser(const std::string& id);
	
	///Compile a list of all current user records
	///\return all users, but with only IDs, names, and email addresses
	std::vector<User> listUsers();

	///Compile a list of all current user records for the given group
	///\return all users from the given group, but with only IDs, names, and email addresses
	std::vector<User> listUsersByGroup(const std::string& group);
	
	///Mark a user as a member of a group
	///\param uID the ID of the user to add
	///\param groupID the ID of the group to which to add the user
	///\return wther the addition operation succeeded
	bool addUserToGroup(const std::string& uID, std::string groupID);
	
	///Remove a user from a group
	///\param uID the ID of the user to remove
	///\param groupID the ID of the group from which to remove the user
	///\return wther the removal operation succeeded
	bool removeUserFromGroup(const std::string& uID, std::string groupID);
	
	///List all groups of which a user is a member
	///\param uID the ID of the user to look up
	///\param useNames if true perform the necessary extra lookups to transform
	///                the group IDs into the more human-friendly names
	///\return the IDs or names of all groups to which the user belongs
	std::vector<std::string> getUserGroupMemberships(const std::string& uID, bool useNames=false);
	
	///Check whether a user is a member of a group
	///\param uID the ID of the user to look up
	///\param groupID the ID of the group to look up
	///\return whether the user is a member of the group
	bool userInGroup(const std::string& uID, std::string groupID);
	
	//----
	
	///Create a record for a new group
	///\param group the new group
	///\pre the new group must have a unique ID and name
	///\return whether the addition operation was successful
	bool addGroup(const Group& group);
	
	///Delete a group record
	///\param groupID the ID of the group to delete
	///\return Whether the user record was successfully removed from the database
	bool removeGroup(const std::string& groupID);
	
	///Change a group record
	///\param group the updated group record, which must have matching ID 
	///          with the old record
	///\return Whether the group record was successfully altered in the database
	bool updateGroup(const Group& group);
	
	///Find all users who belong to a group
	///\groupID the ID of the group whose members are to be found
	///\return the IDs of all members of the group
	std::vector<std::string> getMembersOfGroup(const std::string groupID);
	
	///Find all cluster which are owned by a group
	///\groupID the ID of the group whose clusters are to be found
	///\return the IDs of all clusters owned by the group
	std::vector<std::string> clustersOwnedByGroup(const std::string groupID);
	
	///Find all current groups
	///\return all recorded groups
	std::vector<Group> listGroups();

	///Find all current groups for the current user
	///\return all recorded groups for the current user
	std::vector<Group> listGroupsForUser(const std::string& user);
	
	///Find the group, if any, with the given ID
	///\param name the ID to look up
	///\return the group corresponding to the ID, or an invalid group if none exists
	Group findGroupByID(const std::string& id);
	
	///Find the group, if any, with the given name
	///\param name the name to look up
	///\return the group corresponding to the name, or an invalid group if none exists
	Group findGroupByName(const std::string& name);
	
	///Find the group, if any, with the given UUID or name
	///\param idOrName the UUID or name of the group to look up
	///\return the group corresponding to the name, or an invalid group if none exists
	Group getGroup(const std::string& idOrName);
	
	//----
	
	///Store a record for a new cluster
	///\param cluster the new cluster
	///\return Whether the record was successfully added to the database
	bool addCluster(const Cluster& cluster);
	
	///Delete a cluster record
	///\param cID the ID of the cluster to delete
	///\return Whether the user record was successfully removed from the database
	bool removeCluster(const std::string& cID);
	
	///Change a cluster record
	///\param cluster the updated cluster record, which must have matching ID 
	///               with the old record
	///\return Whether the cluster record was successfully altered in the database
	bool updateCluster(const Cluster& cluster);
	
	///Find all current clusters
	///\return all recorded clusters
	std::vector<Cluster> listClusters();

	///Find all current clusters the given group is allowed to access
	///\return recorded clusters associated with given group
	std::vector<Cluster> listClustersByGroup(std::string group);
	
	///For consumption by kubectl and helm, cluster configurations are stored on
	///the filesystem. 
	///\return a handle containing the path to the current cluster config data
	SharedFileHandle configPathForCluster(const std::string& cID);
	
	///Find the cluster, if any, with the given ID
	///\param name the ID to look up
	///\return the cluster corresponding to the ID, or an invalid cluster if 
	///        none exists
	Cluster findClusterByID(const std::string& id);
	
	///Find the cluster, if any, with the given name
	///\param name the name to look up
	///\return the cluster corresponding to the name, or an invalid cluster if 
	///        none exists
	Cluster findClusterByName(const std::string& name);
	
	///Find the cluster, if any, with the given UUID or name
	///\param idOrName the UUID or name of the cluster to look up
	///\return the cluster corresponding to the name, or an invalid cluster if 
	///        none exists
	Cluster getCluster(const std::string& idOrName);
	
	///Grant a group access to use a cluster
	///\param groupID the ID or name of the group
	///\param cID the ID or name of the cluster
	///\return whether the authorization addition succeeded
	bool addGroupToCluster(std::string groupID, std::string cID);
	
	///Remove a group's access to use a cluster
	///\param groupID the ID or name of the group
	///\param cID the ID or name of the cluster
	///\return whether the authorization removal succeeded
	bool removeGroupFromCluster(std::string groupID, std::string cID);
	
	///List all groups which have access to use a given cluster
	///\param cID the ID or name of the cluster
	///\param useNames whether to return group names instead of IDs
	///\return the IDs (or names) of all groups authorized to use the cluster
	std::vector<std::string> listGroupsAllowedOnCluster(std::string cID, bool useNames=false);
	
	///Check whether a given group is allowed to deploy applications on given cluster.
	///This function does _not_ take into account the cluster's owning group, which
	///should implicitly always have access. This function does take into account
	///whether the cluster grants universal access; it is not necessary to also 
	///check clusterAllowsAllgroups. 
	///\param groupID the ID or name of the group
	///\param cID the ID or name of the cluster
	///\return whether the group may use the cluster
	bool groupAllowedOnCluster(std::string groupID, std::string cID);
	
	///Check whether access to the given cluster is allowed for all groups
	///\param cID the ID of the cluster
	///\return whether all groups may use the cluster
	bool clusterAllowsAllGroups(std::string cID);
	
	///Get all applications that the given group is allowed to use on the given 
	///cluster. If the group is allowed to use all applications, a single result 
	///will be returned, which will be a wildcard. 
	///\param groupID the ID or name of the group
	///\param cID the ID or name of the cluster
	///\return the names of all allowed applications for this group on this cluster
	std::set<std::string> listApplicationsGroupMayUseOnCluster(std::string groupID, std::string cID);
	
	///Add permission for a group to use a particular application on the given 
	///cluster. If the application name is the wildcard (or wildcard name) all
	///specific allowances for that group on that cluster are replaced by a single 
	///record of universal permission. Adding a specific permission when 
	///universal permission has already been granted will replace the universal
	///permission with the single permission. 
	///\param groupID the ID or name of the group
	///\param cID the ID or name of the cluster
	///\param appName the name of the application to allow
	///\return whether the authorization addition succeeded
	bool allowVoToUseApplication(std::string groupID, std::string cID, std::string appName);
	
	///Remove permission for a group to use a particular application on the given 
	///cluster. Adding a specific permission when universal permission has 
	///already been granted will fail. 
	///\param groupID the ID or name of the group
	///\param cID the ID or name of the cluster
	///\param appName the name of the application to deny
	///\return whether the authorization removal succeeded
	bool denyGroupUseOfApplication(std::string groupID, std::string cID, std::string appName);
	
	///Check whether the given group is permitted to use the given application on 
	///the given cluster. 
	///\param groupID the ID or name of the group
	///\param cID the ID or name of the cluster
	///\param appName the name of the application
	///\return whether use of the application is allowed
	bool groupMayUseApplication(std::string groupID, std::string cID, std::string appName);
	
	///Get the recorded location(s) at which a cluster's hardware is located
	///\param idOrName the ID or name of the cluster
	///\return the list of all locations on record
	std::vector<GeoLocation> getLocationsForCluster(std::string idOrName);
	
	///Record location(s) at which a cluster's hardware is located
	///\param idOrName the ID or name of the cluster
	///\param the list of all hardware locations
	///\return Whether the record was successfully added to the database
	bool setLocationsForCluster(std::string idOrName, const std::vector<GeoLocation>& locations);
	
	///Record the monitoring credential assigned to a cluster
	///\param cID the ID of the cluster
	///\param cred the credential to be set
	bool setClusterMonitoringCredential(const std::string& cID, const S3Credential& cred);
	
	///Remove the monitoring credential from a cluster record
	///\param cID the ID of the cluster
	bool removeClusterMonitoringCredential(const std::string& cID);
	
	///Look up the cluster, if any, to which the given monitoring credential 
	///has been assigned. 
	///\cred the credential for which to search
	Cluster findClusterUsingCredential(const S3Credential& cred);
	
	///\param idOrName the ID or name of the cluster
	///\return The cached information about whether the specified cluster was
	///        reachable recently. Note that a result is always returned, even 
	//         if no cached record exists, in which case the result will be 
	///        expired. Therefore, expired records should be considered to 
	///        contain no meaningful data. 
	CacheRecord<bool> getCachedClusterReachability(std::string idOrName);
	
	///Store a recently obtained result for whether a given cluster is reachable
	///\param idOrName the ID or name of the cluster
	///\param reachable whether the most recent attempt to contact the cluster 
	///                 succeeded
	void cacheClusterReachability(std::string idOrName, bool reachable);
	
	//----
	
	///Store a record for a new application instance
	///\param inst the application instance
	///\return Whether the record was successfully added to the database
	bool addApplicationInstance(const ApplicationInstance& inst);
	
	///Delete an application instance record
	///\param id the ID of the instance to delete
	///\return Whether the instance record was successfully removed from the database
	bool removeApplicationInstance(const std::string& id);
	
	///Find information about the application instance with a given ID
	///\param id the instance ID
	///\return the corresponding instance or an invalid application instance 
	///        object if the id is not known. If found, the instance's config
	///        will not be set; it must be fetched using 
	///        getApplicationInstanceConfig
	ApplicationInstance getApplicationInstance(const std::string& id);
	
	///Get the configuration information for an application instance with a 
	///given ID
	///\param id the instance ID
	///\return the corresponding instance configuration, or the empty string if
	///        the id is not known
	std::string getApplicationInstanceConfig(const std::string& id);
	
	///Compile a list of all current application instance records
	///\return all instances, but with only IDs, names, owning groups, clusters, 
	///        and creation times
	std::vector<ApplicationInstance> listApplicationInstances();

	///Compile a list of all current application instance records with given owningGroup or cluster
	///\return all instances with given owningGroup or cluster, but with only IDs, names, owning groups, clusters, 
	///        and creation times
	std::vector<ApplicationInstance> listApplicationInstancesByClusterOrGroup(std::string group, std::string cluster);
	
	///Compile a list of all current application instance records matching a 
	///given name
	///\param name the instance name for which to search
	///\return all matching instance records
	std::vector<ApplicationInstance> findInstancesByName(const std::string& name);
	
	//----
	
	std::string encryptSecret(const SecretData& s) const;
	SecretData decryptSecret(const Secret& s) const;
	
	///Store a record for a new secret
	///\param secret the secret to store
	///\return Whether the record was successfully added to the database
	bool addSecret(const Secret& secret);
	
	///Delete a secret record
	///\param id the ID of the secret to delete
	///\return Whether the secret record was successfully removed from the database
	bool removeSecret(const std::string& id);
	
	///Find information about the secret with a given ID
	///\param id the secret ID
	///\return the corresponding secret or an invalid secert object if the id is
	///        not known. The secret's data will still be encrypted. 
	Secret getSecret(const std::string& id);
	
	///\pre Either \p group or \p cluster may be unspecified (empty) but not both. 
	///\param group the name or ID of the group whose secrets should be listed. May be 
	///          empty to list for all groups on a cluster.
	///\param cluster the name or ID of the cluster for which secrets should be 
	///               listed. May be empty to list for all clusters. 
	std::vector<Secret> listSecrets(std::string group, std::string cluster);
	
	///Find the secret, if any, which has the specified name on the given cluster
	///and belonging to the specified group. 
	///This is not an efficient operation, as it must perform a linear scan of 
	///the list all secrets on the cluster owned by the group, which in turn 
	///requires a moderately expensive query to construct. 
	///\param group the ID or name of the group owning the secret
	///\param cluster the ID or name of the cluster on which the secret is stored
	///\param name the name of the secret
	Secret findSecretByName(std::string group, std::string cluster, std::string name);
	
	//----
	
	///Store a new, unused monitoring credential which will be available for allocation
	bool addMonitoringCredential(const S3Credential& cred);
	
	///Look up an existing monitoring credential
	S3Credential getMonitoringCredential(const std::string& accessKey);
	
	///List all currently stored credentials
	std::vector<S3Credential> listMonitoringCredentials();
	
	///Select a currently unused credential to assign to a cluster which does 
	///not currently have a credential. This may fail if there are no credentials
	///available for allocation. 
	///\return a tuple containing the selected credential and an empty string, 
	///        or an invalid credential if allocation was  not possible and a 
	///        string containing an error message
	std::tuple<S3Credential,std::string> allocateMonitoringCredential();
	
	///Mark a credential revoked, preventing it from being eligible for 
	///allocation and making it eligible for deletion. In-use credentials can be
	///revoked, but this function will not remove such credentials from the
	///records of the clusters to which they are assigned; 
	///findClusterUsingCredential and removeClusterMonitoringCredential must be
	///used to perform that cleanup. 
	bool revokeMonitoringCredential(const std::string& accessKey);
	
	///Remove the record of a credential, which must be in a revoked state. 
	bool deleteMonitoringCredential(const std::string& accessKey);
	
	//----

	///Look up one application, returning a cached result if possible.
	///\param repository the name of the repository in which to search
	///\prama appName the name of the application to look uo
	///\return the application details, which may not be valid if the application was not found
	///\throws std::runtime_error if the helm search command fails	
	Application findApplication(const std::string& repository, const std::string& appName);

	///Unconditionally look up the applications in the given repository, updating the cache while doing so. 
	///\param repository the name of the repository in which to search
	///\return the application details, which may be empty if no applications were found
	///\throws std::runtime_error if the helm search command fails	
	std::vector<Application> fetchApplications(const std::string& repository);

	///Look up all applications in a repository, returning cached results if possible.
	///\param repository the name of the repository in which to search
	///\return the application details, which may be empty if no applications were found
	///\throws std::runtime_error if the helm search command fails	
	std::vector<Application> listApplications(const std::string& repository);
	
	//----
	
	const std::string& getAppLoggingServerName() const{ return appLoggingServerName; }
	const unsigned int getAppLoggingServerPort() const{ return appLoggingServerPort; }
	
	///Return human-readable performance statistics
	std::string getStatistics() const;
	
	///The pseudo-ID associated with wildcard permissions.
	const static std::string wildcard;
	///The pseudo-name associated with wildcard permissions.
	const static std::string wildcardName;
	
	//----
	
	
	///Get the base DNS name which the system will provide for the given cluster
	std::string dnsNameForCluster(const Cluster& cluster) const;
	
	///\return Whether this object is able to make DNS changes, because it is
	///        associated with a valid Rout53 server and account. 
	bool canUpdateDNS() const{ return dnsClient.canUpdateDNS(); }
	
	///Get the version of a record currently stored in Route53, 
	///which may not match actual DNS queries due to propagation delay.
	std::vector<std::string> getDNSRecord(Aws::Route53::Model::RRType type, const std::string& name) const{
		return dnsClient.getDNSRecord(type, name);
	}
	///Create a DNS record associating a name with an address
	bool setDNSRecord(const std::string& name, const std::string& address){
		return dnsClient.setDNSRecord(name, address);
	}
	///Delete the DNS record which associates a particular name with an address
	bool removeDNSRecord(const std::string& name, const std::string& address){
		return dnsClient.removeDNSRecord(name, address);
	}
	
	const Geocoder& getGeocoder(){ return geocoder; }
	void setGeocoder(Geocoder&& g){ geocoder=std::move(g); }
	
	EmailClient& getEmailClient(){ return emailClient; }
	void setEmailClient(EmailClient&& e){ emailClient=std::move(e); }
	
	const std::string& getOpsEmail(){ return opsEmail; }
	void setOpsEmail(std::string address){ opsEmail=address; }
	
private:
	///Database interface object
	Aws::DynamoDB::DynamoDBClient dbClient;
	///Name of the users table in the database
	const std::string userTableName;
	///Name of the groups table in the database
	const std::string groupTableName;
	///Name of the clusters table in the database
	const std::string clusterTableName;
	///Name of the application instances table in the database
	const std::string instanceTableName;
	///Name of the secrets table in the database
	const std::string secretTableName;
	///Name of the monitoring credentials table in the database
	const std::string monCredTableName;
	
	///Sub-object for handling DNS
	DNSManipulator dnsClient;
	///The DNS domain under which clusters subdomains will be placed
	const std::string baseDomain;
	
	///Sub-object for handling geocoding lookups
	Geocoder geocoder;
	
	///Path to the temporary directory where cluster config files are written 
	///in order for kubectl and helm to read
	const FileHandle clusterConfigDir;
	
	///duration for which cached user records should remain valid
	const std::chrono::seconds userCacheValidity;
	slate_atomic<std::chrono::steady_clock::time_point> userCacheExpirationTime;
	cuckoohash_map<std::string,CacheRecord<User>> userCache;
	cuckoohash_map<std::string,CacheRecord<User>> userByTokenCache;
	cuckoohash_map<std::string,CacheRecord<User>> userByGlobusIDCache;
	concurrent_multimap<std::string,CacheRecord<std::string>> userByGroupCache;
	///duration for which cached group records should remain valid
	const std::chrono::seconds groupCacheValidity;
	slate_atomic<std::chrono::steady_clock::time_point> groupCacheExpirationTime;
	cuckoohash_map<std::string,CacheRecord<Group>> groupCache;
	cuckoohash_map<std::string,CacheRecord<Group>> groupByNameCache;
	concurrent_multimap<std::string,CacheRecord<Group>> groupByUserCache;
	///duration for which cached cluster records should remain valid
	const std::chrono::seconds clusterCacheValidity;
	slate_atomic<std::chrono::steady_clock::time_point> clusterCacheExpirationTime;
	cuckoohash_map<std::string,CacheRecord<Cluster>> clusterCache;
	cuckoohash_map<std::string,CacheRecord<Cluster>> clusterByNameCache;
	concurrent_multimap<std::string,CacheRecord<Cluster>> clusterByGroupCache;
	cuckoohash_map<std::string,SharedFileHandle> clusterConfigs;
	concurrent_multimap<std::string,CacheRecord<std::string>> clusterGroupAccessCache;
	cuckoohash_map<std::string,CacheRecord<std::set<std::string>>> clusterGroupApplicationCache;
	cuckoohash_map<std::string,CacheRecord<std::vector<GeoLocation>>> clusterLocationCache;
	///This cache is a little tricky since it represents state of the network, 
	///not something stored in the database, so it's data isn't directly handled
	///by the persistent store. 
	cuckoohash_map<std::string,CacheRecord<bool>> clusterConnectivityCache;
	///duration for which cached instance records should remain valid
	const std::chrono::seconds instanceCacheValidity;
	slate_atomic<std::chrono::steady_clock::time_point> instanceCacheExpirationTime;
	cuckoohash_map<std::string,CacheRecord<ApplicationInstance>> instanceCache;
	cuckoohash_map<std::string,CacheRecord<std::string>> instanceConfigCache;
	concurrent_multimap<std::string,CacheRecord<ApplicationInstance>> instanceByGroupCache;
	concurrent_multimap<std::string,CacheRecord<ApplicationInstance>> instanceByNameCache;
	concurrent_multimap<std::string,CacheRecord<ApplicationInstance>> instanceByClusterCache;
	concurrent_multimap<std::string,CacheRecord<ApplicationInstance>> instanceByGroupAndClusterCache;
	///duration for which cached secret records should remain valid
	const std::chrono::seconds secretCacheValidity;
	cuckoohash_map<std::string,CacheRecord<Secret>> secretCache;
	concurrent_multimap<std::string,CacheRecord<Secret>> secretByGroupCache;
	concurrent_multimap<std::string,CacheRecord<Secret>> secretByGroupAndClusterCache;
	///This cache also contains data not directly managed by the persistent store
	concurrent_multimap<std::string,CacheRecord<Application>> applicationCache;
	
	///Check that all necessary tables exist in the database, and create them if 
	///they do not
	void InitializeTables(std::string bootstrapUserFile);
	
	void InitializeUserTable(std::string bootstrapUserFile);
	void InitializeGroupTable();
	void InitializeClusterTable();
	void InitializeInstanceTable();
	void InitializeSecretTable();
	void InitializeMonCredTable();
	
	void loadEncyptionKey(const std::string& fileName);
	
	///For consumption by kubectl we store configs in the filesystem
	///These files have implicit validity derived from the corresponding entries
	///in clusterCache.
	void writeClusterConfigToDisk(const Cluster& cluster);
	
	///Ensure that a string is a group ID, rather than a group name. 
	///\param groupID the group ID or name. If the value is a valid name, it will 
	///               be replaced with the corresponding ID. 
	///\return true if the ID has been successfully normalized, false if it 
	///        could not be because it was neither a valid group ID nor name. 
	bool normalizeGroupID(std::string& groupID, bool allowWildcard=false);
	
	///Ensure that a string is a cluster ID, rather than a cluster name. 
	///\param cID the cluster ID or name. If the value is a valid name, it will 
	///           be replaced with the corresponding ID. 
	///\return true if the ID has been successfully normalized, false if it 
	///        could not be because it was neither a valid cluster ID nor name. 
	bool normalizeClusterID(std::string& cID);
	
	///The encryption key used for secrets
	SecretData secretKey;
	
	///The server to which application instances should send monitoring data
	std::string appLoggingServerName;
	///The port to which application instances should send monitoring data
	unsigned int appLoggingServerPort;
	
	EmailClient emailClient;
	std::string opsEmail;
	
	std::atomic<size_t> cacheHits, databaseQueries, databaseScans;
};

///\param store the database in which to look up the user
///\param token the proffered authentication token. May be NULL if missing.
const User authenticateUser(PersistentStore& store, const char* token);

#endif //SLATE_PERSISTENT_STORE_H
