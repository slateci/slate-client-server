#ifndef SLATE_PERSISTENT_STORE_H
#define SLATE_PERSISTENT_STORE_H

#include <atomic>
#include <memory>
#include <string>

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/dynamodb/DynamoDBClient.h>

#include <libcuckoo/cuckoohash_map.hh>

#include <concurrent_multimap.h>
#include <Entities.h>

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


///A RAII object for managing the lifetimes of temporary files
struct FileHandle{
public:
	///Construct a handle to own the file at the given path
	///\param filePath the path to the file, which should already exist
	///\param isDirectory whether the path is for a directory rather than a 
	///                   regular file
	FileHandle(const std::string& filePath, bool isDirectory=false):
	filePath(filePath),isDirectory(isDirectory){}
	///Destroys the associated file
	~FileHandle();
	///Copying is forbidden
	FileHandle(const FileHandle&)=delete;
	///Move from a handle
	FileHandle(FileHandle&& other):
	filePath(other.filePath),isDirectory(other.isDirectory){
		other.filePath="";
	}
	///Copy assignment is forbidden
	FileHandle& operator=(const FileHandle&)=delete;
	///Move assignment
	FileHandle& operator=(FileHandle&& other){
		if(this!=&other){
			std::swap(filePath,other.filePath);
			std::swap(isDirectory,other.isDirectory);
		}
		return *this;
	}
	///\return the path to the file
	const std::string& path() const{ return filePath; }
	///\return the path to the file
	operator std::string() const{ return filePath; }
private:
	///the path to the owned file
	std::string filePath;
	///whether the file is a directory
	bool isDirectory;
};

///Concatenate a string with the path stored in a file handle
std::string operator+(const char* s, const FileHandle& h);
///Concatenate the path stored in a file handle with a string
std::string operator+(const FileHandle& h, const char* s);

///Wrapper type for sharing ownership of temporay files
using SharedFileHandle=std::shared_ptr<FileHandle>;

///A wrapper type for tracking cached records which must be considered 
///expired after some time
template <typename RecordType>
struct CacheRecord{
	using steady_clock=std::chrono::steady_clock;
	
	///default construct a record which is considered expired/invalid
	CacheRecord():expirationTime(steady_clock::time_point::min()){}
	
	///construct a record which is considered expired but contains data
	///\param record the cached data
	CacheRecord(const RecordType& record):
	record(record),expirationTime(steady_clock::time_point::min()){}
	
	///\param record the cached data
	///\param exprTime the time after which the record expires
	CacheRecord(const RecordType& record, steady_clock::time_point exprTime):
	record(record),expirationTime(exprTime){}
	
	///\param validity duration until the record expires
	template <typename DurationType>
	CacheRecord(const RecordType& record, DurationType validity):
	record(record),expirationTime(steady_clock::now()+validity){}
	
	///\param exprTime the time after which the record expires
	CacheRecord(RecordType&& record, steady_clock::time_point exprTime):
	record(std::move(record)),expirationTime(exprTime){}
	
	///\param validity duration until the record expires
	template <typename DurationType>
	CacheRecord(RecordType&& record, DurationType validity):
	record(std::move(record)),expirationTime(steady_clock::now()+validity){}
	
	///\return whether the record's expiration time has passed and it should 
	///        be discarded
	bool expired() const{ return (steady_clock::now() > expirationTime); }
	///\return whether the record has not yet expired, so it is still valid
	///        for use
	operator bool() const{ return (steady_clock::now() <= expirationTime); }
	///\return the data stored in the record
	operator RecordType() const{ return record; }
	
	//The cached data
	RecordType record;
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
}

class PersistentStore{
public:
	///\param credentials the AWS credentials used for authenitcation with the 
	///                   database
	///\param clientConfig specification of the database endpoint to contact
	///\param appLoggingServerName server to which application instances should 
	///                            send monitoring data
	///\param appLoggingServerPort port to which application instances should 
	///                            send monitoring data
	PersistentStore(Aws::Auth::AWSCredentials credentials, 
	                Aws::Client::ClientConfiguration clientConfig,
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

	///Compile a list of all current user records for the given VO
	///\return all users from the given VO, but with only IDs, names, and email addresses
	std::vector<User> listUsersByVO(const std::string& vo);
	
	///Mark a user as a member of a VO
	///\param uID the ID of the user to add
	///\param voID the ID of the VO to which to add the user
	///\return wther the addition operation succeeded
	bool addUserToVO(const std::string& uID, std::string voID);
	
	///Remove a user from a VO
	///\param uID the ID of the user to remove
	///\param voID the ID of the VO from which to remove the user
	///\return wther the removal operation succeeded
	bool removeUserFromVO(const std::string& uID, std::string voID);
	
	///List all VOs of which a user is a member
	///\param uID the ID of the user to look up
	///\param useNames if true perform the necessary extra lookups to transform
	///                the VO IDs into the more human-friendly names
	///\return the IDs or names of all VOs to which the user belongs
	std::vector<std::string> getUserVOMemberships(const std::string& uID, bool useNames=false);
	
	///Check whether a user is a member of a VO
	///\param uID the ID of the user to look up
	///\param voID the ID of the VO to look up
	///\return whether the user is a member of the VO
	bool userInVO(const std::string& uID, std::string voID);
	
	//----
	
	///Create a record for a new VO
	///\param vo the new VO
	///\pre the new VO must have a unique ID and name
	///\return whether the addition operation was successful
	bool addVO(const VO& vo);
	
	///Delete a VO record
	///\param voID the ID of the VO to delete
	///\return Whether the user record was successfully removed from the database
	bool removeVO(const std::string& voID);
	
	///Find all users who belong to a VO
	///\voID the ID of the VO whose members are to be found
	///\return the IDs of all members of the VO
	std::vector<std::string> getMembersOfVO(const std::string voID);
	
	///Find all cluster which are owned by a VO
	///\voID the ID of the VO whose clusters are to be found
	///\return the IDs of all clusters owned by the VO
	std::vector<std::string> clustersOwnedByVO(const std::string voID);
	
	///Find all current VOs
	///\return all recorded VOs
	std::vector<VO> listVOs();

	///Find all current VOs for the current user
	///\return all recorded VOs for the current user
	std::vector<VO> listVOsForUser(const std::string& user);
	
	///Find the VO, if any, with the given ID
	///\param name the ID to look up
	///\return the VO corresponding to the ID, or an invalid VO if none exists
	VO findVOByID(const std::string& id);
	
	///Find the VO, if any, with the given name
	///\param name the name to look up
	///\return the VO corresponding to the name, or an invalid VO if none exists
	VO findVOByName(const std::string& name);
	
	///Find the VO, if any, with the given UUID or name
	///\param idOrName the UUID or name of the VO to look up
	///\return the VO corresponding to the name, or an invalid VO if none exists
	VO getVO(const std::string& idOrName);
	
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
	
	///Grant a VO access to use a cluster
	///\param voID the ID or name of the VO
	///\param cID the ID or name of the cluster
	///\return whether the authorization addition succeeded
	bool addVOToCluster(std::string voID, std::string cID);
	
	///Remove a VO's access to use a cluster
	///\param voID the ID or name of the VO
	///\param cID the ID or name of the cluster
	///\return whether the authorization removal succeeded
	bool removeVOFromCluster(std::string voID, std::string cID);
	
	///List all VOs which have access to use a given cluster
	///\param cID the ID or name of the cluster
	///\param useNames whether to return VO names instead of IDs
	///\return the IDs (or names) of all VOs authorized to use the cluster
	std::vector<std::string> listVOsAllowedOnCluster(std::string cID, bool useNames=false);
	
	///Check whether a given VO is allowed to deploy applications on given cluster.
	///This function does _not_ take into account the cluster's owning VO, which
	///should implicitly always have access.
	///\param voID the ID or name of the VO
	///\param cID the ID or name of the cluster
	///\return whether the VO may use the cluster
	bool voAllowedOnCluster(std::string voID, std::string cID);
	
	//----
	
	///Store a record for a new application instance
	///\param inst the application instance
	///\return Whether the record was successfully added to the database
	bool addApplicationInstance(const ApplicationInstance& inst);
	
	///Delete an application instance record
	///\param id the ID of the instance to delete
	///\return Whether the user record was successfully removed from the database
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
	///\return all instances, but with only IDs, names, owning VOs, clusters, 
	///        and creation times
	std::vector<ApplicationInstance> listApplicationInstances();

	///Compile a list of all current application instance records with given owningVO or cluster
	///\return all instances with given owningVO or cluster, but with only IDs, names, owning VOs, clusters, 
	///        and creation times
	std::vector<ApplicationInstance> listApplicationInstancesByClusterOrVO(std::string vo, std::string cluster);
	
	///Compile a list of all current application instance records matching a 
	///given name
	///\param name the instance name for which to search
	///\return all matching instance records
	std::vector<ApplicationInstance> findInstancesByName(const std::string& name);
	
	//----
	
	std::string encryptSecret(const SecretData& s) const;
	SecretData decryptSecret(const Secret& s) const;
	
	bool addSecret(const Secret& secret);
	
	bool removeSecret(const std::string& id);
	
	Secret getSecret(const std::string& id);
	
	///\param vo the name or ID of the VO whose secrets should be listed
	///\param cluster the name or ID of the cluster for which secrets should be 
	///               listed. May be empty to list for all clusters. 
	std::vector<Secret> listSecrets(std::string vo, std::string cluster);
	
	Secret findSecretByName(std::string vo, std::string cluster, std::string name);
	
	//----
	
	const std::string& getAppLoggingServerName() const{ return appLoggingServerName; }
	const unsigned int getAppLoggingServerPort() const{ return appLoggingServerPort; }
	
	///Return human-readable performance statistics
	std::string getStatistics() const;
	
	///Create a temporary file to which data can be written
	FileHandle makeTemporaryFile(const std::string& nameBase="");
	
private:
	///Database interface object
	Aws::DynamoDB::DynamoDBClient dbClient;
	///Name of the users table in the database
	const std::string userTableName;
	///Name of the VOs table in the database
	const std::string voTableName;
	///Name of the clusters table in the database
	const std::string clusterTableName;
	///Name of the application instances table in the database
	const std::string instanceTableName;
	///Name of the secrets instances table in the database
	const std::string secretTableName;
	
	///Path to the temporary directory where cluster config files are written 
	///in order for kubectl and helm to read
	const FileHandle clusterConfigDir;
	
	///duration for which cached user records should remain valid
	const std::chrono::seconds userCacheValidity;
	slate_atomic<std::chrono::steady_clock::time_point> userCacheExpirationTime;
	cuckoohash_map<std::string,CacheRecord<User>> userCache;
	cuckoohash_map<std::string,CacheRecord<User>> userByTokenCache;
	cuckoohash_map<std::string,CacheRecord<User>> userByGlobusIDCache;
	concurrent_multimap<std::string,CacheRecord<std::string>> userByVOCache;
	///duration for which cached VO records should remain valid
	const std::chrono::seconds voCacheValidity;
	slate_atomic<std::chrono::steady_clock::time_point> voCacheExpirationTime;
	cuckoohash_map<std::string,CacheRecord<VO>> voCache;
	cuckoohash_map<std::string,CacheRecord<VO>> voByNameCache;
	concurrent_multimap<std::string,CacheRecord<VO>> voByUserCache;
	///duration for which cached cluster records should remain valid
	const std::chrono::seconds clusterCacheValidity;
	slate_atomic<std::chrono::steady_clock::time_point> clusterCacheExpirationTime;
	cuckoohash_map<std::string,CacheRecord<Cluster>> clusterCache;
	cuckoohash_map<std::string,CacheRecord<Cluster>> clusterByNameCache;
	concurrent_multimap<std::string,CacheRecord<Cluster>> clusterByVOCache;
	cuckoohash_map<std::string,SharedFileHandle> clusterConfigs;
	concurrent_multimap<std::string,CacheRecord<std::string>> clusterVOAccessCache;
	///duration for which cached instance records should remain valid
	const std::chrono::seconds instanceCacheValidity;
	slate_atomic<std::chrono::steady_clock::time_point> instanceCacheExpirationTime;
	cuckoohash_map<std::string,CacheRecord<ApplicationInstance>> instanceCache;
	cuckoohash_map<std::string,CacheRecord<std::string>> instanceConfigCache;
	concurrent_multimap<std::string,CacheRecord<ApplicationInstance>> instanceByVOCache;
	concurrent_multimap<std::string,CacheRecord<ApplicationInstance>> instanceByNameCache;
	concurrent_multimap<std::string,CacheRecord<ApplicationInstance>> instanceByClusterCache;
	concurrent_multimap<std::string,CacheRecord<ApplicationInstance>> instanceByVOAndClusterCache;
	///duration for which cached secret records should remain valid
	const std::chrono::seconds secretCacheValidity;
	cuckoohash_map<std::string,CacheRecord<Secret>> secretCache;
	concurrent_multimap<std::string,CacheRecord<Secret>> secretByVOCache;
	concurrent_multimap<std::string,CacheRecord<Secret>> secretByVOAndClusterCache;
	
	///Check that all necessary tables exist in the database, and create them if 
	///they do not
	void InitializeTables();
	
	void InitializeUserTable();
	void InitializeVOTable();
	void InitializeClusterTable();
	void InitializeInstanceTable();
	void InitializeSecretTable();
	
	void loadEncyptionKey();
	
	///For consumption by kubectl we store configs in the filesystem
	///These files have implicit validity derived from the corresponding entries
	///in clusterCache.
	void writeClusterConfigToDisk(const Cluster& cluster);
	
	///The encryption key used for secrets
	SecretData secretKey;
	
	///The server to which application instances should send monitoring data
	std::string appLoggingServerName;
	///The port to which application instances should send monitoring data
	unsigned int appLoggingServerPort;
	
	std::atomic<size_t> cacheHits, databaseQueries, databaseScans;
};

///\param store the database in which to look up the user
///\param token the proffered authentication token. May be NULL if missing.
const User authenticateUser(PersistentStore& store, const char* token);

#endif //SLATE_PERSISTENT_STORE_H
