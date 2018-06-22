#include <PersistentStore.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

#include <aws/core/utils/Outcome.h>
#include <aws/dynamodb/model/DeleteItemRequest.h>
#include <aws/dynamodb/model/GetItemRequest.h>
#include <aws/dynamodb/model/PutItemRequest.h>
#include <aws/dynamodb/model/QueryRequest.h>
#include <aws/dynamodb/model/ScanRequest.h>
#include <aws/dynamodb/model/UpdateItemRequest.h>

#include <aws/dynamodb/model/DescribeTableRequest.h>
#include <aws/dynamodb/model/CreateTableRequest.h>

PersistentStore::PersistentStore(Aws::Auth::AWSCredentials credentials, 
				Aws::Client::ClientConfiguration clientConfig):
	dbClient(std::move(credentials),std::move(clientConfig)),
	userTableName("SLATE_users"),
	voTableName("SLATE_VOs"),
	clusterTableName("SLATE_clusters")
{
	std::cout << "Starting database client" << std::endl;
	InitializeTables();
	std::cout << "Database client ready" << std::endl;
}

void PersistentStore::InitializeTables(){
	using namespace Aws::DynamoDB::Model;
	using AttDef=Aws::DynamoDB::Model::AttributeDefinition;
	using SAT=Aws::DynamoDB::Model::ScalarAttributeType;
	
	auto userTableOut=dbClient.DescribeTable(DescribeTableRequest()
											 .WithTableName(userTableName));
	if(!userTableOut.IsSuccess()){
		//Users table does not exist; create it
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
		if(!createOut.IsSuccess()){
			std::cerr << "Failed to users clusters table: " + createOut.GetError().GetMessage() << std::endl;
			throw std::runtime_error("Failed to create users table: " + createOut.GetError().GetMessage());
		}
		
		std::cout << "Waiting for users table to reach active status" << std::endl;
		do{
			std::this_thread::sleep_for(std::chrono::seconds(1));
			userTableOut=dbClient.DescribeTable(DescribeTableRequest()
												.WithTableName(userTableName));
		}while(userTableOut.IsSuccess() && 
			   userTableOut.GetResult().GetTable().GetTableStatus()!=TableStatus::ACTIVE);
		if(!userTableOut.IsSuccess()){
			std::cerr << "Users table does not seem to be available?" << std::endl;
			throw std::runtime_error("Users table does not seem to be available?");
		}
		
		{
			User portal;
			std::ifstream credFile("slate_portal_user");
			if(!credFile){
				std::cerr << "Unable to read portal user credentials" << std::endl;
				throw std::runtime_error("Unable to read portal user credentials");
			}
			credFile >> portal.id >> portal.name >> portal.email >> portal.token;
			if(credFile.fail()){
				std::cerr << "Unable to read portal user credentials" << std::endl;
				throw std::runtime_error("Unable to read portal user credentials");
			}
			portal.globusID="No Globus ID";
			portal.admin=true;
			portal.valid=true;
			
			if(!addUser(portal)){
				std::cerr << "Failed to inject portal user" << std::endl;
				throw std::runtime_error("Failed to inject portal user");
			}
		}
	}
	//std::cout << "Status of users table is " << (int)userTableOut.GetResult().GetTable().GetTableStatus() << std::endl;
	
	auto voTableOut=dbClient.DescribeTable(DescribeTableRequest()
											 .WithTableName(voTableName));
	if(!voTableOut.IsSuccess()){
		//VOs table does not exist; create it
		auto request=CreateTableRequest();
		request.SetTableName(voTableName);
		request.SetAttributeDefinitions({
			AttDef().WithAttributeName("ID").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("sortKey").WithAttributeType(SAT::S)
		});
		request.SetKeySchema({
			KeySchemaElement().WithAttributeName("ID").WithKeyType(KeyType::HASH),
			KeySchemaElement().WithAttributeName("sortKey").WithKeyType(KeyType::RANGE)
		});
		request.SetProvisionedThroughput(ProvisionedThroughput()
										 .WithReadCapacityUnits(1)
										 .WithWriteCapacityUnits(1));
		
		auto createOut=dbClient.CreateTable(request);
		if(!createOut.IsSuccess()){
			std::cerr << "Failed to create VOs table: " + createOut.GetError().GetMessage() << std::endl;
			throw std::runtime_error("Failed to create VOs table: " + createOut.GetError().GetMessage());
		}
		
		std::cout << "Waiting for VOs table to reach active status" << std::endl;
		do{
			std::this_thread::sleep_for(std::chrono::seconds(1));
			voTableOut=dbClient.DescribeTable(DescribeTableRequest()
												.WithTableName(voTableName));
		}while(voTableOut.IsSuccess() && 
			   voTableOut.GetResult().GetTable().GetTableStatus()!=TableStatus::ACTIVE);
		if(!voTableOut.IsSuccess()){
			std::cerr << "VOs table does not seem to be available?" << std::endl;
			throw std::runtime_error("VOs table does not seem to be available?");
		}
	}
	//std::cout << "Status of VOs table is " << (int)voTableOut.GetResult().GetTable().GetTableStatus() << std::endl;
	
	auto clusterTableOut=dbClient.DescribeTable(DescribeTableRequest()
											 .WithTableName(clusterTableName));
	if(!clusterTableOut.IsSuccess()){
		//clusters table does not exist; create it
		auto request=CreateTableRequest();
		request.SetTableName(clusterTableName);
		request.SetAttributeDefinitions({
			AttDef().WithAttributeName("ID").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("sortKey").WithAttributeType(SAT::S),
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
														  .WithNonKeyAttributes({"ID"}))
										  .WithProvisionedThroughput(ProvisionedThroughput()
																	 .WithReadCapacityUnits(1)
																	 .WithWriteCapacityUnits(1))
										  );
		
		auto createOut=dbClient.CreateTable(request);
		if(!createOut.IsSuccess()){
			std::cerr << "Failed to create clusters table: " + createOut.GetError().GetMessage() << std::endl;
			throw std::runtime_error("Failed to create clusters table: " + createOut.GetError().GetMessage());
		}
		
		std::cout << "Waiting for clusters table to reach active status" << std::endl;
		do{
			std::this_thread::sleep_for(std::chrono::seconds(1));
			clusterTableOut=dbClient.DescribeTable(DescribeTableRequest()
												.WithTableName(clusterTableName));
		}while(clusterTableOut.IsSuccess() && 
			   clusterTableOut.GetResult().GetTable().GetTableStatus()!=TableStatus::ACTIVE);
		if(!clusterTableOut.IsSuccess()){
			std::cerr << "Clusters table does not seem to be available?" << std::endl;
			throw std::runtime_error("Clusters table does not seem to be available?");
		}
	}
	//std::cout << "Status of Clusters table is " << (int)clusterTableOut.GetResult().GetTable().GetTableStatus() << std::endl;
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
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
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
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
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
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
		return User();
	}
	const auto& queryResult=outcome.GetResult();
	if(queryResult.GetCount()!=1){ //TODO: further action for >1 case?
		return User();
	}
	
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
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
		return User();
	}
	const auto& queryResult=outcome.GetResult();
	if(queryResult.GetCount()!=1){ //TODO: further action for >1 case?
		return User();
	}
	
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
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
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
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
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
			std::cerr << err.GetMessage() << std::endl;
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
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
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
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
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
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
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
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
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
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
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
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
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
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
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
			std::cerr << err.GetMessage() << std::endl;
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

//----

std::string PersistentStore::configPathForCluster(const std::string& cID){
	//return "/usr/lib/slate-service/etc/clusters/"+cID+"/config";
	return "etc/clusters/"+cID;
}

bool PersistentStore::addCluster(const Cluster& cluster){
	using Aws::DynamoDB::Model::AttributeValue;
	auto request=Aws::DynamoDB::Model::PutItemRequest()
	.WithTableName(clusterTableName)
	.WithItem({
		{"ID",AttributeValue(cluster.id)},
		{"sortKey",AttributeValue(cluster.id)},
		{"name",AttributeValue(cluster.name)},
		{"owningVO",AttributeValue(cluster.owningVO)},
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
		return false;
	}
	//!!!: For consumption by kubectl we store configs in the filesystem, 
	//rather than in the database
	{
		std::string filePath=configPathForCluster(cluster.id);
		std::ofstream confFile(filePath);
		if(!confFile){
			std::cerr << "Unable to write cluster config to " << filePath << std::endl;
			return false;
		}
		confFile << cluster.config;
		if(confFile.fail()){
			std::cerr << "Unable to write cluster config to " << filePath << std::endl;
			return false;
		}
	}
	return true;
}

Cluster PersistentStore::getCluster(const std::string& cID){
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.GetItem(Aws::DynamoDB::Model::GetItemRequest()
								  .WithTableName(clusterTableName)
								  .WithKey({{"ID",AttributeValue(cID)},
	                                        {"sortKey",AttributeValue(cID)}}));
	if(!outcome.IsSuccess()){
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
		return Cluster();
	}
	const auto& item=outcome.GetResult().GetItem();
	if(item.empty()) //no match found
		return Cluster{};
	Cluster cluster;
	cluster.valid=true;
	cluster.id=cID;
	cluster.name=item.find("name")->second.GetS();
	cluster.owningVO=item.find("owningVO")->second.GetS();
	return cluster;
}

bool PersistentStore::removeCluster(const std::string& cID){
	{
		std::string filePath=configPathForCluster(cID);
		int err=remove(filePath.c_str());
		if(err!=0){
			err=errno;
			std::cerr << "Failed to remove cluster config " << filePath
			<< " errno: " << errno << std::endl;
		}
	}
	
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.DeleteItem(Aws::DynamoDB::Model::DeleteItemRequest()
								     .WithTableName(clusterTableName)
								     .WithKey({{"ID",AttributeValue(cID)},
	                                           {"sortKey",AttributeValue(cID)}}));
	if(!outcome.IsSuccess()){
		//TODO: more principled logging or reporting of the nature of the error
		auto err=outcome.GetError();
		std::cerr << err.GetMessage() << std::endl;
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
			std::cerr << err.GetMessage() << std::endl;
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
