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
	///\return the path to the file
	const std::string& path() const{ return filePath; }
	///\return the path to the file
	operator std::string() const{ return filePath; }
private:
	///the path to the owned file
	const std::string filePath;
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
	PersistentStore(Aws::Auth::AWSCredentials credentials, 
					Aws::Client::ClientConfiguration clientConfig);
	
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
	///\return Whether the user record was successfully altered in the database
	bool updateUser(const User& user);
	
	///Delete a user record
	///\param id the ID of the user to delete
	///\return Whether the user record was successfully removed from the database
	bool removeUser(const std::string& id);
	
	///Compile a list of all current user records
	///\return all users, but with only IDs, names, and email addresses
	std::vector<User> listUsers();
	
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
	///\return the IDs of all VOs to which the user belongs
	std::vector<std::string> getUserVOMemberships(const std::string& uID);
	
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
	
	///Find all current VOs
	///\return all recorded VOs
	std::vector<VO> listVOs();
	
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
	
	///Return human-readable performance statistics
	std::string getStatistics() const;
	
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
	
	///Path to the temporary directory where cluster config files are written 
	///in order for kubectl and helm to read
	const FileHandle clusterConfigDir;
	
	///duration for which cached user records should remain valid
	const std::chrono::seconds userCacheValidity;
	cuckoohash_map<std::string,CacheRecord<User>> userCache;
	cuckoohash_map<std::string,CacheRecord<User>> userByTokenCache;
	cuckoohash_map<std::string,CacheRecord<User>> userByGlobusIDCache;
	concurrent_multimap<std::string,CacheRecord<std::string>> userByVOCache;
	///duration for which cached VO records should remain valid
	const std::chrono::seconds voCacheValidity;
	cuckoohash_map<std::string,CacheRecord<VO>> voCache;
	cuckoohash_map<std::string,CacheRecord<VO>> voByNameCache;
	///duration for which cached cluster records should remain valid
	const std::chrono::seconds clusterCacheValidity;
	cuckoohash_map<std::string,CacheRecord<Cluster>> clusterCache;
	cuckoohash_map<std::string,CacheRecord<Cluster>> clusterByNameCache;
	concurrent_multimap<std::string,CacheRecord<Cluster>> clusterByVOCache;
	cuckoohash_map<std::string,SharedFileHandle> clusterConfigs;
	///duration for which cached user records should remain valid
	const std::chrono::seconds instanceCacheValidity;
	cuckoohash_map<std::string,CacheRecord<ApplicationInstance>> instanceCache;
	cuckoohash_map<std::string,CacheRecord<std::string>> instanceConfigCache;
	concurrent_multimap<std::string,CacheRecord<ApplicationInstance>> instanceByVOCache;
	concurrent_multimap<std::string,CacheRecord<ApplicationInstance>> instanceByNameCache;
	
	///Check that all necessary tabes exist in the database, and create them if 
	///they do not
	void InitializeTables();
	
	///For consumption by kubectl we store configs in the filesystem
	///These files have implicit validity derived from the corresponding entries
	///in clusterCache.
	void writeClusterConfigToDisk(const Cluster& cluster);
	
	std::atomic<size_t> cacheHits, databaseQueries, databaseScans;
};

#endif //SLATE_PERSISTENT_STORE_H