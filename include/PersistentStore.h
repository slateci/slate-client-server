#ifndef SLATE_PERSISTENT_STORE_H
#define SLATE_PERSISTENT_STORE_H

#include <memory>

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/dynamodb/DynamoDBClient.h>

#include <libcuckoo/cuckoohash_map.hh>

#include "Entities.h"

struct FileHandle{
public:
	FileHandle(const std::string& filePath):filePath(filePath){}
	~FileHandle();
	const std::string& path() const{ return filePath; }
	operator std::string() const{ return filePath; }
private:
	const std::string filePath;
};

std::string operator+(const char* s, const FileHandle& h);
std::string operator+(const FileHandle& h, const char* s);

using SharedFileHandle=std::shared_ptr<FileHandle>;

class PersistentStore{
public:
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
	///\return Whether the user record was successfully altered in the database
	bool updateUser(const User& user);
	
	///Delete a user record
	///\return Whether the user record was successfully removed from the database
	bool removeUser(const std::string& id);
	
	///Compile a list of all current user records
	///\return all users, but with only IDs, names, and email addresses
	std::vector<User> listUsers();
	
	bool addUserToVO(const std::string& uID, const std::string voID);
	
	bool removeUserFromVO(const std::string& uID, const std::string& voID);
	
	std::vector<std::string> getUserVOMemberships(const std::string& uID);
	
	bool userInVO(const std::string& uID, const std::string& voID);
	
	//----
	
	bool addVO(const VO& vo);
	
	///Delete a VO record
	///\return Whether the user record was successfully removed from the database
	bool removeVO(const std::string& voID);
	
	std::vector<std::string> getMembersOfVO(const std::string voID);
	
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
	///\return Whether the record was successfully added to the database
	bool addCluster(const Cluster& cluster);
	
	///Delete a cluster record
	///\return Whether the user record was successfully removed from the database
	bool removeCluster(const std::string& cID);
	
	std::vector<Cluster> listClusters();
	
	///For consumption by kubectl and helm, we store cluster configurations on
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
	///\return Whether the record was successfully added to the database
	bool addApplicationInstance(const ApplicationInstance& inst);
	
	///Delete a user record
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
	
private:
	Aws::DynamoDB::DynamoDBClient dbClient;
	const std::string userTableName;
	const std::string voTableName;
	const std::string clusterTableName;
	const std::string instanceTableName;
	
	const std::string clusterConfigDir;

	template <typename RecordType>
	struct CacheRecord{
		using steady_clock=std::chrono::steady_clock;
		
		///default construct a record which is considered expired/invalid
		CacheRecord():expirationTime(steady_clock::time_point::min()){}
		
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
		
		bool expired() const{ return (steady_clock::now() > expirationTime); }
		operator bool() const{ return (steady_clock::now() <= expirationTime); }
		operator RecordType() const{ return record; }
		
		RecordType record;
		steady_clock::time_point expirationTime;
	};
	
	///duration for which cached cluster records should remain valid
	const std::chrono::seconds clusterCacheValidity;
	cuckoohash_map<std::string,CacheRecord<Cluster>> clusterCache;
	cuckoohash_map<std::string,CacheRecord<Cluster>> clusterByNameCache;
	cuckoohash_map<std::string,SharedFileHandle> clusterConfigs;
	
	void InitializeTables();
	
	///For consumption by kubectl we store configs in the filesystem
	///These files have implicit validity derived from the corresponding entries
	///in clusterCache.
	void writeClusterConfigToDisk(const Cluster& cluster);
};

#endif //SLATE_PERSISTENT_STORE_H