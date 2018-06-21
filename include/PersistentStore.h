#ifndef SLATE_PERSISTENT_STORE_H
#define SLATE_PERSISTENT_STORE_H

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/dynamodb/DynamoDBClient.h>


#include "Entities.h"

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
	
	bool removeVO(const std::string& voID);
	
	std::vector<std::string> getMembersOfVO(const std::string voID);
	
	std::vector<VO> listVOs();
	
	//----
	
	bool addCluster(const Cluster& cluster);
	
	Cluster getCluster(const std::string& cID);
	
	bool removeCluster(const std::string& cID);
	
	std::vector<Cluster> listClusters();
	
private:
	Aws::DynamoDB::DynamoDBClient dbClient;
	const std::string userTableName;
	const std::string voTableName;
	const std::string clusterTableName;
	
	void InitializeTables();
};

#endif //SLATE_PERSISTENT_STORE_H