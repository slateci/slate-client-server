#include <PersistentStore.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>

#include <unistd.h>

#include <aws/core/utils/Outcome.h>
#include <aws/dynamodb/model/DeleteItemRequest.h>
#include <aws/dynamodb/model/GetItemRequest.h>
#include <aws/dynamodb/model/PutItemRequest.h>
#include <aws/dynamodb/model/QueryRequest.h>
#include <aws/dynamodb/model/ScanRequest.h>
#include <aws/dynamodb/model/UpdateItemRequest.h>

#include <aws/dynamodb/model/DescribeTableRequest.h>
#include <aws/dynamodb/model/CreateTableRequest.h>

#include "Logging.h"
#include "Utilities.h"

namespace{
	template<typename MapType>
	bool insertOrReplace(MapType& map, 
	                     const typename MapType::key_type& key, 
	                     const typename MapType::mapped_type& val){
		return map.upsert(key,
						  //if the item is already in the map, update it
		                  [&val](typename MapType::mapped_type& existing){
		                  	existing=val;
		                  },
						  //otherwise insert it with the new value
		                  val);
	}
	
	std::string createConfigTempDir(){
		const std::string base="/tmp/slate_XXXXXXXX";
		//make a modifiable copy for mkdtemp to scribble over
		std::unique_ptr<char[]> tmpl(new char[base.size()+1]);
		strcpy(tmpl.get(),base.c_str());
		char* dirPath=mkdtemp(tmpl.get());
		if(!dirPath){
			int err=errno;
			log_fatal("Creating temporary cluster config directory failed with error " << err);
		}
		return dirPath;
	}
}

PersistentStore::PersistentStore(Aws::Auth::AWSCredentials credentials, 
				Aws::Client::ClientConfiguration clientConfig):
	dbClient(std::move(credentials),std::move(clientConfig)),
	userTableName("SLATE_users"),
	voTableName("SLATE_VOs"),
	clusterTableName("SLATE_clusters"),
	instanceTableName("SLATE_instances"),
	clusterConfigDir(createConfigTempDir()),
	clusterCacheValidity(60) //TODO: replace number
{
	log_info("Starting database client");
	InitializeTables();
	log_info("Database client ready");
}

void PersistentStore::InitializeTables(){
	using namespace Aws::DynamoDB::Model;
	using AttDef=Aws::DynamoDB::Model::AttributeDefinition;
	using SAT=Aws::DynamoDB::Model::ScalarAttributeType;
	
	auto userTableOut=dbClient.DescribeTable(DescribeTableRequest()
	                                         .WithTableName(userTableName));
	if(!userTableOut.IsSuccess() &&
	   userTableOut.GetError().GetErrorType()!=Aws::DynamoDB::DynamoDBErrors::RESOURCE_NOT_FOUND){
		log_fatal("Unable to connect to DynamoDB: "
		          << userTableOut.GetError().GetMessage());
	}
	if(!userTableOut.IsSuccess()){
		log_info("Users table does not exist; creating");
		auto request=CreateTableRequest();
		request.SetTableName(userTableName);
		request.SetAttributeDefinitions({
			AttDef().WithAttributeName("ID").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("sortKey").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("token").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("globusID").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("voID").WithAttributeType(SAT::S)
		});
		request.SetKeySchema({
			KeySchemaElement().WithAttributeName("ID").WithKeyType(KeyType::HASH),
			KeySchemaElement().WithAttributeName("sortKey").WithKeyType(KeyType::RANGE)
		});
		request.SetProvisionedThroughput(ProvisionedThroughput()
										 .WithReadCapacityUnits(1)
										 .WithWriteCapacityUnits(1));
		request.AddGlobalSecondaryIndexes(GlobalSecondaryIndex()
		                                  .WithIndexName("ByToken")
		                                  .WithKeySchema({KeySchemaElement()
		                                                  .WithAttributeName("token")
		                                                  .WithKeyType(KeyType::HASH)})
		                                  .WithProjection(Projection()
														  .WithProjectionType(ProjectionType::INCLUDE)
														  .WithNonKeyAttributes({"ID","admin"}))
										  .WithProvisionedThroughput(ProvisionedThroughput()
																	 .WithReadCapacityUnits(1)
																	 .WithWriteCapacityUnits(1))
										  );
		request.AddGlobalSecondaryIndexes(GlobalSecondaryIndex()
		                                  .WithIndexName("ByGlobusID")
		                                  .WithKeySchema({KeySchemaElement()
		                                                  .WithAttributeName("globusID")
		                                                  .WithKeyType(KeyType::HASH)})
		                                  .WithProjection(Projection()
														  .WithProjectionType(ProjectionType::INCLUDE)
														  .WithNonKeyAttributes({"ID","token"}))
										  .WithProvisionedThroughput(ProvisionedThroughput()
																	 .WithReadCapacityUnits(1)
																	 .WithWriteCapacityUnits(1))
										  );
		request.AddGlobalSecondaryIndexes(GlobalSecondaryIndex()
		                                  .WithIndexName("ByVO")
		                                  .WithKeySchema({KeySchemaElement()
		                                                  .WithAttributeName("voID")
		                                                  .WithKeyType(KeyType::HASH)})
		                                  .WithProjection(Projection()
														  .WithProjectionType(ProjectionType::INCLUDE)
														  .WithNonKeyAttributes({"ID"}))
										  .WithProvisionedThroughput(ProvisionedThroughput()
																	 .WithReadCapacityUnits(1)
																	 .WithWriteCapacityUnits(1))
										  );
		
		auto createOut=dbClient.CreateTable(request);
		if(!createOut.IsSuccess())
			log_fatal("Failed create to users table: " + createOut.GetError().GetMessage());
		
		log_info("Waiting for users table to reach active status");
		do{
			std::this_thread::sleep_for(std::chrono::seconds(1));
			userTableOut=dbClient.DescribeTable(DescribeTableRequest()
												.WithTableName(userTableName));
		}while(userTableOut.IsSuccess() && 
			   userTableOut.GetResult().GetTable().GetTableStatus()!=TableStatus::ACTIVE);
		if(!userTableOut.IsSuccess())
			log_fatal("Users table does not seem to be available? "
			          "Dynamo error: " << userTableOut.GetError().GetMessage());
		
		{
			User portal;
			std::ifstream credFile("slate_portal_user");
			if(!credFile)
				log_fatal("Unable to read portal user credentials");
			credFile >> portal.id >> portal.name >> portal.email >> portal.token;
			if(credFile.fail())
				log_fatal("Unable to read portal user credentials");
			portal.globusID="No Globus ID";
			portal.admin=true;
			portal.valid=true;
			
			if(!addUser(portal))
				log_fatal("Failed to inject portal user");
		}
		log_info("Created users table");
	}
	
	auto voTableOut=dbClient.DescribeTable(DescribeTableRequest()
											 .WithTableName(voTableName));
	if(!voTableOut.IsSuccess()){
		log_info("VOs table does not exist; creating");
		auto request=CreateTableRequest();
		request.SetTableName(voTableName);
		request.SetAttributeDefinitions({
			AttDef().WithAttributeName("ID").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("sortKey").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("name").WithAttributeType(SAT::S)
		});
		request.SetKeySchema({
			KeySchemaElement().WithAttributeName("ID").WithKeyType(KeyType::HASH),
			KeySchemaElement().WithAttributeName("sortKey").WithKeyType(KeyType::RANGE)
		});
		request.SetProvisionedThroughput(ProvisionedThroughput()
		                                 .WithReadCapacityUnits(1)
		                                 .WithWriteCapacityUnits(1));
		
		request.AddGlobalSecondaryIndexes(GlobalSecondaryIndex()
		                                  .WithIndexName("ByName")
		                                  .WithKeySchema({KeySchemaElement()
		                                                  .WithAttributeName("name")
		                                                  .WithKeyType(KeyType::HASH)})
		                                  .WithProjection(Projection()
		                                                  .WithProjectionType(ProjectionType::INCLUDE)
		                                                  .WithNonKeyAttributes({"ID"}))
		                                  .WithProvisionedThroughput(ProvisionedThroughput()
		                                                             .WithReadCapacityUnits(1)
		                                                             .WithWriteCapacityUnits(1))
		                                  );
		
		auto createOut=dbClient.CreateTable(request);
		if(!createOut.IsSuccess())
			log_fatal("Failed to create VOs table: " + createOut.GetError().GetMessage());
		
		log_info("Waiting for VOs table to reach active status");
		do{
			std::this_thread::sleep_for(std::chrono::seconds(1));
			voTableOut=dbClient.DescribeTable(DescribeTableRequest()
												.WithTableName(voTableName));
		}while(voTableOut.IsSuccess() && 
			   voTableOut.GetResult().GetTable().GetTableStatus()!=TableStatus::ACTIVE);
		if(!voTableOut.IsSuccess())
			log_fatal("VOs table does not seem to be available? "
			          "Dynamo error: " << voTableOut.GetError().GetMessage());
		log_info("Created VOs table");
	}
	
	auto clusterTableOut=dbClient.DescribeTable(DescribeTableRequest()
											 .WithTableName(clusterTableName));
	if(!clusterTableOut.IsSuccess()){
		log_info("Clusters table does not exist; creating");
		auto request=CreateTableRequest();
		request.SetTableName(clusterTableName);
		request.SetAttributeDefinitions({
			AttDef().WithAttributeName("ID").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("sortKey").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("name").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("owningVO").WithAttributeType(SAT::S)
		});
		request.SetKeySchema({
			KeySchemaElement().WithAttributeName("ID").WithKeyType(KeyType::HASH),
			KeySchemaElement().WithAttributeName("sortKey").WithKeyType(KeyType::RANGE)
		});
		request.SetProvisionedThroughput(ProvisionedThroughput()
		                                 .WithReadCapacityUnits(1)
		                                 .WithWriteCapacityUnits(1));
		
		request.AddGlobalSecondaryIndexes(GlobalSecondaryIndex()
		                                  .WithIndexName("ByVO")
		                                  .WithKeySchema({KeySchemaElement()
		                                                  .WithAttributeName("owningVO")
		                                                  .WithKeyType(KeyType::HASH)})
		                                  .WithProjection(Projection()
		                                                  .WithProjectionType(ProjectionType::INCLUDE)
		                                                  .WithNonKeyAttributes({"ID","name","config"}))
		                                  .WithProvisionedThroughput(ProvisionedThroughput()
		                                                             .WithReadCapacityUnits(1)
		                                                             .WithWriteCapacityUnits(1))
		                                  );
		request.AddGlobalSecondaryIndexes(GlobalSecondaryIndex()
		                                  .WithIndexName("ByName")
		                                  .WithKeySchema({KeySchemaElement()
		                                                  .WithAttributeName("name")
		                                                  .WithKeyType(KeyType::HASH)})
		                                  .WithProjection(Projection()
		                                                  .WithProjectionType(ProjectionType::INCLUDE)
		                                                  .WithNonKeyAttributes({"ID","owningVO","config"}))
		                                  .WithProvisionedThroughput(ProvisionedThroughput()
		                                                             .WithReadCapacityUnits(1)
		                                                             .WithWriteCapacityUnits(1))
		                                  );
		
		auto createOut=dbClient.CreateTable(request);
		if(!createOut.IsSuccess())
			log_fatal("Failed to create clusters table: " + createOut.GetError().GetMessage());
		
		log_info("Waiting for clusters table to reach active status");
		do{
			std::this_thread::sleep_for(std::chrono::seconds(1));
			clusterTableOut=dbClient.DescribeTable(DescribeTableRequest()
												.WithTableName(clusterTableName));
		}while(clusterTableOut.IsSuccess() && 
			   clusterTableOut.GetResult().GetTable().GetTableStatus()!=TableStatus::ACTIVE);
		if(!clusterTableOut.IsSuccess())
			log_fatal("Clusters table does not seem to be available? "
			          "Dynamo error: " << clusterTableOut.GetError().GetMessage());
		log_info("Created clusters table");
	}
	
	auto instanceTableOut=dbClient.DescribeTable(DescribeTableRequest()
	                                             .WithTableName(instanceTableName));
	if(!instanceTableOut.IsSuccess()){
		log_info("Instance table does not exist; creating");
		auto request=CreateTableRequest();
		request.SetTableName(instanceTableName);
		request.SetAttributeDefinitions({
			AttDef().WithAttributeName("ID").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("sortKey").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("name").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("owningVO").WithAttributeType(SAT::S),
			//AttDef().WithAttributeName("cluster").WithAttributeType(SAT::S),
			//AttDef().WithAttributeName("config").WithAttributeType(SAT::S),
			//AttDef().WithAttributeName("ctime").WithAttributeType(SAT::S)
		});
		request.SetKeySchema({
			KeySchemaElement().WithAttributeName("ID").WithKeyType(KeyType::HASH),
			KeySchemaElement().WithAttributeName("sortKey").WithKeyType(KeyType::RANGE)
		});
		request.SetProvisionedThroughput(ProvisionedThroughput()
		                                 .WithReadCapacityUnits(1)
		                                 .WithWriteCapacityUnits(1));
		
		request.AddGlobalSecondaryIndexes(GlobalSecondaryIndex()
		                                  .WithIndexName("ByVO")
		                                  .WithKeySchema({KeySchemaElement()
		                                                  .WithAttributeName("owningVO")
		                                                  .WithKeyType(KeyType::HASH)})
		                                  .WithProjection(Projection()
		                                                  .WithProjectionType(ProjectionType::INCLUDE)
		                                                  .WithNonKeyAttributes({"ID"})
		                                                  .WithNonKeyAttributes({"name"})
		                                                  .WithNonKeyAttributes({"application"})
		                                                  .WithNonKeyAttributes({"cluster"})
		                                                  .WithNonKeyAttributes({"ctime"}))
		                                  .WithProvisionedThroughput(ProvisionedThroughput()
		                                                             .WithReadCapacityUnits(1)
		                                                             .WithWriteCapacityUnits(1))
		                                  );
		request.AddGlobalSecondaryIndexes(GlobalSecondaryIndex()
		                                  .WithIndexName("ByName")
		                                  .WithKeySchema({KeySchemaElement()
		                                                  .WithAttributeName("name")
		                                                  .WithKeyType(KeyType::HASH)})
		                                  .WithProjection(Projection()
		                                                  .WithProjectionType(ProjectionType::INCLUDE)
		                                                  .WithNonKeyAttributes({"ID"})
		                                                  .WithNonKeyAttributes({"application"})
		                                                  .WithNonKeyAttributes({"owningVO"})
		                                                  .WithNonKeyAttributes({"cluster"})
		                                                  .WithNonKeyAttributes({"ctime"}))
		                                  .WithProvisionedThroughput(ProvisionedThroughput()
		                                                             .WithReadCapacityUnits(1)
		                                                             .WithWriteCapacityUnits(1))
		                                  );
		
		auto createOut=dbClient.CreateTable(request);
		if(!createOut.IsSuccess())
			log_fatal("Failed to create instance table: " + createOut.GetError().GetMessage());
		
		log_info("Waiting for instance table to reach active status");
		do{
			std::this_thread::sleep_for(std::chrono::seconds(1));
			instanceTableOut=dbClient.DescribeTable(DescribeTableRequest()
												    .WithTableName(voTableName));
		}while(instanceTableOut.IsSuccess() && 
			   instanceTableOut.GetResult().GetTable().GetTableStatus()!=TableStatus::ACTIVE);
		if(!instanceTableOut.IsSuccess())
			log_fatal("Instance table does not seem to be available? "
			          "Dynamo error: " << instanceTableOut.GetError().GetMessage());
		log_info("Created VOs table");
	}
}

bool PersistentStore::addUser(const User& user){
	using Aws::DynamoDB::Model::AttributeValue;
	auto request=Aws::DynamoDB::Model::PutItemRequest()
	.WithTableName(userTableName)
	.WithItem({
		{"ID",AttributeValue(user.id)},
		{"sortKey",AttributeValue(user.id)},
		{"name",AttributeValue(user.name)},
		{"globusID",AttributeValue(user.globusID)},
		{"token",AttributeValue(user.token)},
		{"email",AttributeValue(user.email)},
		{"admin",AttributeValue().SetBool(user.admin)}
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to add user record: " << err.GetMessage());
		return false;
	}
	
	return true;
}

User PersistentStore::getUser(const std::string& id){
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.GetItem(Aws::DynamoDB::Model::GetItemRequest()
								  .WithTableName(userTableName)
								  .WithKey({{"ID",AttributeValue(id)},
	                                        {"sortKey",AttributeValue(id)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch user record: " << err.GetMessage());
		return User();
	}
	const auto& item=outcome.GetResult().GetItem();
	if(item.empty()) //no match found
		return User{};
	User user;
	user.valid=true;
	user.id=id;
	user.name=item.find("name")->second.GetS();
	user.email=item.find("email")->second.GetS();
	user.token=item.find("token")->second.GetS();
	user.globusID=item.find("globusID")->second.GetS();
	user.admin=item.find("admin")->second.GetBool();
	return user;
}

User PersistentStore::findUserByToken(const std::string& token){
	using Aws::DynamoDB::Model::AttributeValue;
	auto request=Aws::DynamoDB::Model::QueryRequest()
	.WithTableName(userTableName)
	.WithIndexName("ByToken")
	.WithKeyConditionExpression("#token = :tok_val")
	.WithExpressionAttributeNames({
		{"#token","token"}
	})
	.WithExpressionAttributeValues({
		{":tok_val",AttributeValue(token)}
	});
	auto outcome=dbClient.Query(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to look up user by token: " << err.GetMessage());
		return User();
	}
	const auto& queryResult=outcome.GetResult();
	if(queryResult.GetCount()==0)
		return User();
	if(queryResult.GetCount()>1)
		log_fatal("Multiple user records are associated with token " << token << '!');
	
	User user;
	user.valid=true;
	user.token=token;
	user.id=queryResult.GetItems().front().find("ID")->second.GetS();
	user.admin=queryResult.GetItems().front().find("admin")->second.GetBool();
	return user;
}

User PersistentStore::findUserByGlobusID(const std::string& globusID){
	using AV=Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
								.WithTableName(userTableName)
								.WithIndexName("ByGlobusID")
								.WithKeyConditionExpression("#globusID = :id_val")
								.WithExpressionAttributeNames({{"#globusID","globusID"}})
								.WithExpressionAttributeValues({{":id_val",AV(globusID)}})
								);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to look up user by Globus ID: " << err.GetMessage());
		return User();
	}
	const auto& queryResult=outcome.GetResult();
	if(queryResult.GetCount()==0)
		return User();
	if(queryResult.GetCount()>1)
		log_fatal("Multiple user records are associated with Globus ID " << globusID << '!');
	
	User user;
	user.valid=true;
	user.id=queryResult.GetItems().front().find("ID")->second.GetS();
	user.token=queryResult.GetItems().front().find("token")->second.GetS();
	user.globusID=globusID;
	return user;
}

bool PersistentStore::updateUser(const User& user){
	using AV=Aws::DynamoDB::Model::AttributeValue;
	using AVU=Aws::DynamoDB::Model::AttributeValueUpdate;
	auto outcome=dbClient.UpdateItem(Aws::DynamoDB::Model::UpdateItemRequest()
	                                 .WithTableName(userTableName)
									 .WithKey({{"ID",AV(user.id)},
	                                           {"sortKey",AV(user.id)}})
	                                 .WithAttributeUpdates({
	                                            {"name",AVU().WithValue(AV(user.name))},
	                                            {"globusID",AVU().WithValue(AV(user.globusID))},
	                                            {"token",AVU().WithValue(AV(user.token))},
	                                            {"email",AVU().WithValue(AV(user.email))},
	                                            {"admin",AVU().WithValue(AV().SetBool(user.admin))}
	                                 }));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to update user record: " << err.GetMessage());
		return false;
	}
	
	return true;
}

bool PersistentStore::removeUser(const std::string& id){
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.DeleteItem(Aws::DynamoDB::Model::DeleteItemRequest()
								     .WithTableName(userTableName)
								     .WithKey({{"ID",AttributeValue(id)},
	                                           {"sortKey",AttributeValue(id)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to delete user record: " << err.GetMessage());
		return false;
	}
	return true;
}

std::vector<User> PersistentStore::listUsers(){
	std::vector<User> collected;
	Aws::DynamoDB::Model::ScanRequest request;
	request.SetTableName(userTableName);
	//request.SetAttributesToGet({"ID","name","email"});
	request.SetFilterExpression("attribute_exists(email)");
	bool keepGoing=false;
	
	do{
		auto outcome=dbClient.Scan(request);
		if(!outcome.IsSuccess()){
			//TODO: more principled logging or reporting of the nature of the error
			auto err=outcome.GetError();
			log_error("Failed to fetch user records: " << err.GetMessage());
			return collected;
		}
		const auto& result=outcome.GetResult();
		//set up fetching the next page if necessary
		if(!result.GetLastEvaluatedKey().empty()){
			keepGoing=true;
			request.SetExclusiveStartKey(result.GetLastEvaluatedKey());
		}
		else
			keepGoing=false;
		//collect results from this page
		for(const auto& item : result.GetItems()){
			User user;
			user.valid=true;
			user.id=item.find("ID")->second.GetS();
			user.name=item.find("name")->second.GetS();
			user.email=item.find("email")->second.GetS();
			collected.push_back(user);
		}
	}while(keepGoing);
	return collected;
}

bool PersistentStore::addUserToVO(const std::string& uID, const std::string voID){
	using Aws::DynamoDB::Model::AttributeValue;
	auto request=Aws::DynamoDB::Model::PutItemRequest()
	.WithTableName(userTableName)
	.WithItem({
		{"ID",AttributeValue(uID)},
		{"sortKey",AttributeValue(uID+":"+voID)},
		{"voID",AttributeValue(voID)}
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to add user VO membership record: " << err.GetMessage());
		return false;
	}
	
	return true;
}

bool PersistentStore::removeUserFromVO(const std::string& uID, const std::string& voID){
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.DeleteItem(Aws::DynamoDB::Model::DeleteItemRequest()
								     .WithTableName(userTableName)
								     .WithKey({{"ID",AttributeValue(uID)},
	                                           {"sortKey",AttributeValue(uID+":"+voID)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to delete user VO membership record: " << err.GetMessage());
		return false;
	}
	return true;
}

std::vector<std::string> PersistentStore::getUserVOMemberships(const std::string& uID){
	using Aws::DynamoDB::Model::AttributeValue;
	auto request=Aws::DynamoDB::Model::QueryRequest()
	.WithTableName(userTableName)
	.WithKeyConditionExpression("#id = :id AND begins_with(#sortKey,:prefix)")
	.WithExpressionAttributeNames({
		{"#id","ID"},
		{"#sortKey","sortKey"}
	})
	.WithExpressionAttributeValues({
		{":id",AttributeValue(uID)},
		{":prefix",AttributeValue(uID+":VO")}
	});
	auto outcome=dbClient.Query(request);
	std::vector<std::string> vos;
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch user's VO membership records: " << err.GetMessage());
		return vos;
	}
	
	const auto& queryResult=outcome.GetResult();
	for(const auto& item : queryResult.GetItems()){
		if(item.count("voID"))
			vos.push_back(item.find("voID")->second.GetS());
	}
	return vos;
}

bool PersistentStore::userInVO(const std::string& uID, const std::string& voID){
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.GetItem(Aws::DynamoDB::Model::GetItemRequest()
								  .WithTableName(userTableName)
								  .WithKey({{"ID",AttributeValue(uID)},
	                                        {"sortKey",AttributeValue(uID+":"+voID)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch user VO membership record: " << err.GetMessage());
		return false;
	}
	const auto& item=outcome.GetResult().GetItem();
	if(item.empty()) //no match found
		return false;
	return true;
}

//----

bool PersistentStore::addVO(const VO& vo){
	using AV=Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.PutItem(Aws::DynamoDB::Model::PutItemRequest()
	                              .WithTableName(voTableName)
	                              .WithItem({{"ID",AV(vo.id)},
	                                         {"sortKey",AV(vo.id)},
	                                         {"name",AV(vo.name)}
	                              }));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to add VO record: " << err.GetMessage());
		return false;
	}
	
	return true;
}

bool PersistentStore::removeVO(const std::string& voID){
	using Aws::DynamoDB::Model::AttributeValue;
	
	//delete all memberships in the VO
	for(auto uID : getMembersOfVO(voID)){
		if(!removeUserFromVO(uID,voID))
			return false;
	}
	
	//delete the VO record itself
	auto outcome=dbClient.DeleteItem(Aws::DynamoDB::Model::DeleteItemRequest()
								     .WithTableName(voTableName)
								     .WithKey({{"ID",AttributeValue(voID)},
	                                           {"sortKey",AttributeValue(voID)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to delete VO record: " << err.GetMessage());
		return false;
	}
	return true;
}

std::vector<std::string> PersistentStore::getMembersOfVO(const std::string voID){
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
	                            .WithTableName(userTableName)
	                            .WithIndexName("ByVO")
	                            .WithKeyConditionExpression("#voID = :id_val")
	                            .WithExpressionAttributeNames({{"#voID","voID"}})
								.WithExpressionAttributeValues({{":id_val",AttributeValue(voID)}})
	                            );
	std::vector<std::string> users;
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch VO membership records: " << err.GetMessage());
		return users;
	}
	const auto& queryResult=outcome.GetResult();
	users.reserve(queryResult.GetCount());
	for(const auto& item : queryResult.GetItems())
		users.push_back(item.find("ID")->second.GetS());
	
	return users;
}

std::vector<VO> PersistentStore::listVOs(){
	std::vector<VO> collected;
	Aws::DynamoDB::Model::ScanRequest request;
	request.SetTableName(voTableName);
	request.SetFilterExpression("attribute_exists(#name)");
	request.SetExpressionAttributeNames({{"#name","name"}});
	bool keepGoing=false;
	
	do{
		auto outcome=dbClient.Scan(request);
		if(!outcome.IsSuccess()){
			//TODO: more principled logging or reporting of the nature of the error
			auto err=outcome.GetError();
			log_error("Failed to fetch VO records: " << err.GetMessage());
			return collected;
		}
		const auto& result=outcome.GetResult();
		//set up fetching the next page if necessary
		if(!result.GetLastEvaluatedKey().empty()){
			keepGoing=true;
			request.SetExclusiveStartKey(result.GetLastEvaluatedKey());
		}
		else
			keepGoing=false;
		//collect results from this page
		for(const auto& item : result.GetItems()){
			VO vo;
			vo.valid=true;
			vo.id=item.find("ID")->second.GetS();
			vo.name=item.find("name")->second.GetS();
			collected.push_back(vo);
		}
	}while(keepGoing);
	return collected;
}

VO PersistentStore::findVOByID(const std::string& id){
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.GetItem(Aws::DynamoDB::Model::GetItemRequest()
	                              .WithTableName(voTableName)
	                              .WithKey({{"ID",AttributeValue(id)},
	                                        {"sortKey",AttributeValue(id)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch VO record: " << err.GetMessage());
		return VO();
	}
	const auto& item=outcome.GetResult().GetItem();
	if(item.empty()) //no match found
		return VO{};
	VO vo;
	vo.valid=true;
	vo.id=id;
	vo.name=findOrThrow(item,"name","VO record missing name attribute").GetS();
	return vo;
}

VO PersistentStore::findVOByName(const std::string& name){
	using AV=Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
	                            .WithTableName(voTableName)
	                            .WithIndexName("ByName")
	                            .WithKeyConditionExpression("#name = :name_val")
	                            .WithExpressionAttributeNames({{"#name","name"}})
	                            .WithExpressionAttributeValues({{":name_val",AV(name)}})
	                            );
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to look up VO by name: " << err.GetMessage());
		return VO();
	}
	const auto& queryResult=outcome.GetResult();
	if(queryResult.GetCount()==0)
		return VO();
	if(queryResult.GetCount()>1)
		log_fatal("VO name \"" << name << "\" is not unique!");
	
	VO vo;
	vo.valid=true;
	vo.id=findOrThrow(queryResult.GetItems().front(),"ID","VO record missing ID attribute").GetS();
	vo.name=name;
	return vo;
}

VO PersistentStore::getVO(const std::string& idOrName){
	if(idOrName.find(IDGenerator::voIDPrefix)==0)
		return findVOByID(idOrName);
	return findVOByName(idOrName);
}

//----

std::pair<std::string,std::mutex&> PersistentStore::configPathForCluster(const std::string& cID){
	findClusterByID(cID); //need to do this to ensure local data is fresh
	return std::pair<std::string,std::mutex&>(clusterConfigDir+"/"+cID,*getClusterConfigLock(cID));
}

bool PersistentStore::addCluster(const Cluster& cluster){
	using Aws::DynamoDB::Model::AttributeValue;
	auto request=Aws::DynamoDB::Model::PutItemRequest()
	.WithTableName(clusterTableName)
	.WithItem({
		{"ID",AttributeValue(cluster.id)},
		{"sortKey",AttributeValue(cluster.id)},
		{"name",AttributeValue(cluster.name)},
		{"config",AttributeValue(cluster.config)},
		{"owningVO",AttributeValue(cluster.owningVO)},
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to add cluster record: " << err.GetMessage());
		return false;
	}
	
	CacheRecord<Cluster> record(cluster,clusterCacheValidity);
	insertOrReplace(clusterCache,cluster.id,record);
	insertOrReplace(clusterByNameCache,cluster.name,record);
	writeClusterConfigToDisk(cluster);
	
	return true;
}

std::shared_ptr<std::mutex> PersistentStore::getClusterConfigLock(const std::string& cID){
	std::shared_ptr<std::mutex> mut=std::make_shared<std::mutex>();
	auto getExisting=[&](std::shared_ptr<std::mutex>& existing){
		mut=existing; //copy the existing pointer
	};
	clusterConfigLocks.upsert(cID,getExisting,mut);
	//At this point mut points to the mutex in the hashtable for filename, 
	//either because it was already there or because the new mutex we create has 
	//been put there. This suffices to ensure that all threads will see a 
	//consistent mutex, but we do not yet own it. That must be done by the caller.
	return mut;
	//Unfortunately, there is no obviously safe way to ever clean up mutexes, 
	//without locking the entire collection. So, at the moment we just 
	//accumulate them, although this should in practice never matter, as cluster
	//deletions should be very rare. 
}

bool PersistentStore::writeClusterConfigToDisk(const Cluster& cluster){
	auto configInfo=configPathForCluster(cluster.id);
	std::string filePath=configInfo.first;
	//to prevent two threads making a mess by writing at a same time, we acquire
	//a per-cluster lock to write. 
	log_info("Locking " << &configInfo.second << " to write " << filePath);
	std::lock_guard<std::mutex> lock(configInfo.second);
	std::ofstream confFile(filePath);
	if(!confFile){
		log_error("Unable to open " << filePath << " for writing");
		return false;
	}
	confFile << cluster.config;
	if(confFile.fail()){
		log_error("Unable to write cluster config to " << filePath);
		return false;
	}
	return true;
}

Cluster PersistentStore::findClusterByID(const std::string& cID){
	//first see if we have this cached
	{
		CacheRecord<Cluster> record;
		if(clusterCache.find(cID,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				log_info("Found cluster info in cache");
				return record;
			}
			log_info("Cached cluster record expired");
		}
		else
			log_info("Cluster record not in cache");
	}
	log_info("Cluster info not in cache or expired");
	//need to query the database
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.GetItem(Aws::DynamoDB::Model::GetItemRequest()
								  .WithTableName(clusterTableName)
								  .WithKey({{"ID",AttributeValue(cID)},
	                                        {"sortKey",AttributeValue(cID)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch cluster record: " << err.GetMessage());
		return Cluster();
	}
	const auto& item=outcome.GetResult().GetItem();
	if(item.empty()) //no match found
		return Cluster{};
	Cluster cluster;
	cluster.valid=true;
	cluster.id=cID;
	cluster.name=findOrThrow(item,"name","Cluster record missing name attribute").GetS();
	cluster.owningVO=findOrThrow(item,"owningVO","Cluster record missing owningVO attribute").GetS();
	cluster.config=findOrThrow(item,"config","Cluster record missing config attribute").GetS();
	
	//cache this result for reuse
	CacheRecord<Cluster> record(cluster,clusterCacheValidity);
	insertOrReplace(clusterCache,cluster.id,record);
	log_info("cluster cache size: " << clusterCache.size());
	insertOrReplace(clusterByNameCache,cluster.name,record);
	writeClusterConfigToDisk(cluster);
	
	return cluster;
}

Cluster PersistentStore::findClusterByName(const std::string& name){
	//first see if we have this cached
	{
		CacheRecord<Cluster> record;
		if(clusterByNameCache.find(name,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				log_info("Found cluster info in cache");
				return record;
			}
		}
	}
	log_info("Cluster info not in cache or expired");
	//need to query the database
	using AV=Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
	                            .WithTableName(clusterTableName)
	                            .WithIndexName("ByName")
	                            .WithKeyConditionExpression("#name = :name_val")
	                            .WithExpressionAttributeNames({{"#name","name"}})
	                            .WithExpressionAttributeValues({{":name_val",AV(name)}})
	                            );
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to look up Cluster by name: " << err.GetMessage());
		return Cluster();
	}
	const auto& queryResult=outcome.GetResult();
	if(queryResult.GetCount()==0)
		return Cluster();
	if(queryResult.GetCount()>1)
		log_fatal("Cluster name \"" << name << "\" is not unique!");
	
	Cluster cluster;
	cluster.valid=true;
	cluster.id=findOrThrow(queryResult.GetItems().front(),"ID",
	                       "Cluster record missing ID attribute").GetS();
	cluster.name=name;
	cluster.owningVO=findOrThrow(queryResult.GetItems().front(),"owningVO",
	                             "Cluster record missing owningVO attribute").GetS();
	cluster.config=findOrThrow(queryResult.GetItems().front(),"config",
	                           "Cluster record missing config attribute").GetS();
	
	//cache this result for reuse
	CacheRecord<Cluster> record(cluster,clusterCacheValidity);
	insertOrReplace(clusterCache,cluster.id,record);
	insertOrReplace(clusterByNameCache,cluster.name,record);
	
	return cluster;
}

Cluster PersistentStore::getCluster(const std::string& idOrName){
	if(idOrName.find(IDGenerator::clusterIDPrefix)==0)
		return findClusterByID(idOrName);
	return findClusterByName(idOrName);
}

bool PersistentStore::removeCluster(const std::string& cID){
	{
		auto configInfo=configPathForCluster(cID);
		std::string filePath=configInfo.first;
		std::lock_guard<std::mutex> lock(configInfo.second);
		int err=remove(filePath.c_str());
		if(err!=0){
			err=errno;
			log_error("Failed to remove cluster config " << filePath
					  << " errno: " << err);
		}
	}
	
	//erase cache entries
	{
		//Somewhat hacky: we can't erase the byName cache entry unless we know 
		//the name. However, we keep the caches synchronized, so if there is 
		//such an entry to delete there is also an entry in the main cache, so 
		//we can grab that to get the name without having to read from the 
		//database.
		CacheRecord<Cluster> record;
		if(clusterCache.find(cID,record)){
			//don't particularly care whether the record is expired; if it is 
			//all that will happen is that we will delete the equally stale 
			//ecord in the other cache
			clusterByNameCache.erase(record.record.name);
		}
	}
	clusterCache.erase(cID);
	
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.DeleteItem(Aws::DynamoDB::Model::DeleteItemRequest()
								     .WithTableName(clusterTableName)
								     .WithKey({{"ID",AttributeValue(cID)},
	                                           {"sortKey",AttributeValue(cID)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to delete cluster record: " << err.GetMessage());
		return false;
	}
	return true;
}

std::vector<Cluster> PersistentStore::listClusters(){
	std::vector<Cluster> collected;
	Aws::DynamoDB::Model::ScanRequest request;
	request.SetTableName(clusterTableName);
	bool keepGoing=false;
	
	do{
		auto outcome=dbClient.Scan(request);
		if(!outcome.IsSuccess()){
			//TODO: more principled logging or reporting of the nature of the error
			auto err=outcome.GetError();
			log_error("Failed to fetch cluster records: " << err.GetMessage());
			return collected;
		}
		const auto& result=outcome.GetResult();
		//set up fetching the next page if necessary
		if(!result.GetLastEvaluatedKey().empty()){
			keepGoing=true;
			request.SetExclusiveStartKey(result.GetLastEvaluatedKey());
		}
		else
			keepGoing=false;
		//collect results from this page
		for(const auto& item : result.GetItems()){
			Cluster cluster;
			cluster.valid=true;
			cluster.id=item.find("ID")->second.GetS();
			cluster.name=item.find("name")->second.GetS();
			cluster.owningVO=item.find("owningVO")->second.GetS();
			collected.push_back(cluster);
		}
	}while(keepGoing);
	return collected;
}

bool PersistentStore::addApplicationInstance(const ApplicationInstance& inst){
	using Aws::DynamoDB::Model::AttributeValue;
	auto request=Aws::DynamoDB::Model::PutItemRequest()
	.WithTableName(instanceTableName)
	.WithItem({
		{"ID",AttributeValue(inst.id)},
		{"sortKey",AttributeValue(inst.id)},
		{"name",AttributeValue(inst.name)},
		{"application",AttributeValue(inst.application)},
		{"owningVO",AttributeValue(inst.owningVO)},
		{"cluster",AttributeValue(inst.cluster)},
		{"ctime",AttributeValue(inst.ctime)}
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to add application instance record: " << err.GetMessage());
		return false;
	}
	//We assume that configs will be accessed less often than the rest of the 
	//information about an instance, and they are relatively large, so we stroe 
	//them in separate, secondary items
	request=Aws::DynamoDB::Model::PutItemRequest()
	.WithTableName(instanceTableName)
	.WithItem({
		{"ID",AttributeValue(inst.id)},
		{"sortKey",AttributeValue(inst.id+":config")},
		{"config",AttributeValue(inst.config)}
	});
	outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to add application instance config record: " << err.GetMessage());
		return false;
	}
	
	return true;
}

bool PersistentStore::removeApplicationInstance(const std::string& id){
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.DeleteItem(Aws::DynamoDB::Model::DeleteItemRequest()
	                                      .WithTableName(instanceTableName)
	                                      .WithKey({{"ID",AttributeValue(id)},
	                                                {"sortKey",AttributeValue(id)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to delete instance record: " << err.GetMessage());
		return false;
	}
	outcome=dbClient.DeleteItem(Aws::DynamoDB::Model::DeleteItemRequest()
	                                      .WithTableName(instanceTableName)
	                                      .WithKey({{"ID",AttributeValue(id)},
	                                                {"sortKey",AttributeValue(id+":config")}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to delete instance config record: " << err.GetMessage());
		return false;
	}
	return true;
}

ApplicationInstance PersistentStore::getApplicationInstance(const std::string& id){
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.GetItem(Aws::DynamoDB::Model::GetItemRequest()
								  .WithTableName(instanceTableName)
								  .WithKey({{"ID",AttributeValue(id)},
	                                        {"sortKey",AttributeValue(id)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch application instance record: " << err.GetMessage());
		return ApplicationInstance();
	}
	const auto& item=outcome.GetResult().GetItem();
	if(item.empty()) //no match found
		return ApplicationInstance{};
	ApplicationInstance inst;
	inst.valid=true;
	inst.id=id;
	inst.name=findOrThrow(item,"name","Instance record missing name attribute").GetS();
	inst.application=findOrThrow(item,"application","Instance record missing application attribute").GetS();
	inst.owningVO=findOrThrow(item,"owningVO","Instance record missing owningVO attribute").GetS();
	inst.cluster=findOrThrow(item,"cluster","Instance record missing cluster attribute").GetS();
	inst.ctime=findOrThrow(item,"ctime","Instance record missing ctime attribute").GetS();
	return inst;
}

std::string PersistentStore::getApplicationInstanceConfig(const std::string& id){
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.GetItem(Aws::DynamoDB::Model::GetItemRequest()
	                              .WithTableName(instanceTableName)
	                              .WithKey({{"ID",AttributeValue(id)},
	                                        {"sortKey",AttributeValue(id+":config")}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch application instance config record: " << err.GetMessage());
		return std::string{};
	}
	const auto& item=outcome.GetResult().GetItem();
	if(item.empty()) //no match found
		return std::string{};
	return findOrThrow(item,"config","Instance config record missing config attribute").GetS();
}

std::vector<ApplicationInstance> PersistentStore::listApplicationInstances(){
	std::vector<ApplicationInstance> collected;
	Aws::DynamoDB::Model::ScanRequest request;
	request.SetTableName(instanceTableName);
	request.SetFilterExpression("attribute_exists(ctime)");
	bool keepGoing=false;
	
	do{
		auto outcome=dbClient.Scan(request);
		if(!outcome.IsSuccess()){
			//TODO: more principled logging or reporting of the nature of the error
			auto err=outcome.GetError();
			log_error("Failed to fetch application instance records: " << err.GetMessage());
			return collected;
		}
		const auto& result=outcome.GetResult();
		//set up fetching the next page if necessary
		if(!result.GetLastEvaluatedKey().empty()){
			keepGoing=true;
			request.SetExclusiveStartKey(result.GetLastEvaluatedKey());
		}
		else
			keepGoing=false;
		//collect results from this page
		for(const auto& item : result.GetItems()){
			ApplicationInstance inst;
			inst.valid=true;
			inst.id=findOrThrow(item,"ID","Instance record missing ID attribute").GetS();
			inst.name=findOrThrow(item,"name","Instance record missing name attribute").GetS();
			inst.application=findOrThrow(item,"application","Instance record missing application attribute").GetS();
			inst.owningVO=findOrThrow(item,"owningVO","Instance record missing ID attribute").GetS();
			inst.cluster=findOrThrow(item,"cluster","Instance record missing ID attribute").GetS();
			inst.ctime=findOrThrow(item,"ctime","Instance record missing ID attribute").GetS();
			collected.push_back(inst);
		}
	}while(keepGoing);
	return collected;
}

