#include <PersistentStore.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>

#include <unistd.h>

#include <boost/lexical_cast.hpp>

#include <aws/core/utils/Outcome.h>
#include <aws/dynamodb/model/DeleteItemRequest.h>
#include <aws/dynamodb/model/GetItemRequest.h>
#include <aws/dynamodb/model/PutItemRequest.h>
#include <aws/dynamodb/model/QueryRequest.h>
#include <aws/dynamodb/model/ScanRequest.h>
#include <aws/dynamodb/model/UpdateItemRequest.h>

#include <aws/dynamodb/model/CreateGlobalSecondaryIndexAction.h>
#include <aws/dynamodb/model/CreateTableRequest.h>
#include <aws/dynamodb/model/DeleteTableRequest.h>
#include <aws/dynamodb/model/DescribeTableRequest.h>
#include <aws/dynamodb/model/UpdateTableRequest.h>

#include <HTTPRequests.h>
#include <Logging.h>
#include <ServerUtilities.h>
#include <Process.h>
extern "C"{
	#include <scrypt/scryptenc/scryptenc.h>
}
#include <KubeInterface.h>

EmailClient::EmailClient(const std::string& mailgunEndpoint, 
                         const std::string& mailgunKey, 
						 const std::string& emailDomain):
mailgunEndpoint(mailgunEndpoint),mailgunKey(mailgunKey),emailDomain(emailDomain)
{
	valid=!mailgunEndpoint.empty() && !mailgunKey.empty() && !emailDomain.empty();
}

bool EmailClient::sendEmail(const EmailClient::Email& email){
	if(!valid)
		return false;
	std::string url="https://api:"+mailgunKey+"@"+mailgunEndpoint+"/v3/"+emailDomain+"/messages";
	std::multimap<std::string,std::string> data{
		{"from",email.fromAddress},
		{"subject",email.subject},
		{"text",email.body}
	};
	for(const auto& to : email.toAddresses)
		data.emplace("to",to);
	for(const auto& cc : email.ccAddresses)
		data.emplace("cc",cc);
	for(const auto& bcc : email.bccAddresses)
		data.emplace("bcc",bcc);
	auto response=httpRequests::httpPostForm(url,data);
	if(response.status!=200){
		log_warn("Failed to send email: " << response.body);
		return false;
	}
	return true;
}

namespace{
	
bool hasIndex(const Aws::DynamoDB::Model::TableDescription& tableDesc, const std::string& name){
	using namespace Aws::DynamoDB::Model;
	const Aws::Vector<GlobalSecondaryIndexDescription>& indices=tableDesc.GetGlobalSecondaryIndexes();
	return std::find_if(indices.begin(),indices.end(),
						[&name](const GlobalSecondaryIndexDescription& gsid)->bool{
							return gsid.GetIndexName()==name;
						})!=indices.end();
}
	
bool indexHasNonKeyProjection(const Aws::DynamoDB::Model::TableDescription& tableDesc, 
                              const std::string& index, const std::string& attr){
	using namespace Aws::DynamoDB::Model;
	const Aws::Vector<GlobalSecondaryIndexDescription>& indices=tableDesc.GetGlobalSecondaryIndexes();
	auto indexIt=std::find_if(indices.begin(),indices.end(),
	                          [&index](const GlobalSecondaryIndexDescription& gsid)->bool{
	                          	return gsid.GetIndexName()==index;
	                          });
	if(indexIt==indices.end())
		return false;
	for(const auto& attr_ : indexIt->GetProjection().GetNonKeyAttributes()){
		if(attr_==attr)
			return true;
	}
	return false;
}

Aws::DynamoDB::Model::CreateGlobalSecondaryIndexAction
secondaryIndexToCreateAction(const Aws::DynamoDB::Model::GlobalSecondaryIndex& index){
	using namespace Aws::DynamoDB::Model;
	Aws::DynamoDB::Model::CreateGlobalSecondaryIndexAction createAction;
	createAction
	.WithIndexName(index.GetIndexName())
	.WithKeySchema(index.GetKeySchema())
	.WithProjection(index.GetProjection())
	.WithProvisionedThroughput(index.GetProvisionedThroughput());
	return createAction;
}

Aws::DynamoDB::Model::UpdateTableRequest
updateTableWithNewSecondaryIndex(const std::string& tableName, const Aws::DynamoDB::Model::GlobalSecondaryIndex& index){
	using namespace Aws::DynamoDB::Model;
	auto request=UpdateTableRequest();
	request.SetTableName(tableName);
	request.AddGlobalSecondaryIndexUpdates(GlobalSecondaryIndexUpdate()
	                                       .WithCreate(secondaryIndexToCreateAction(index)));
	return request;
}
	
void waitTableReadiness(Aws::DynamoDB::DynamoDBClient& dbClient, const std::string& tableName){
	using namespace Aws::DynamoDB::Model;
	log_info("Waiting for table " << tableName << " to reach active status");
	DescribeTableOutcome outcome;
	do{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		outcome=dbClient.DescribeTable(DescribeTableRequest()
		                               .WithTableName(tableName));
	}while(outcome.IsSuccess() && 
		   outcome.GetResult().GetTable().GetTableStatus()!=TableStatus::ACTIVE);
	if(!outcome.IsSuccess())
		log_fatal("Table " << tableName << " does not seem to be available? "
				  "Dynamo error: " << outcome.GetError().GetMessage());
}

void waitIndexReadiness(Aws::DynamoDB::DynamoDBClient& dbClient, 
                        const std::string& tableName, 
                        const std::string& indexName){
	using namespace Aws::DynamoDB::Model;
	log_info("Waiting for index " << indexName << " of table " << tableName << " to reach active status");
	DescribeTableOutcome outcome;
	using GSID=GlobalSecondaryIndexDescription;
	Aws::Vector<GSID> indices;
	Aws::Vector<GSID>::iterator index;
	do{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		outcome=dbClient.DescribeTable(DescribeTableRequest()
		                               .WithTableName(tableName));
	}while(outcome.IsSuccess() && (
		   (indices=outcome.GetResult().GetTable().GetGlobalSecondaryIndexes()).empty() ||
		   (index=std::find_if(indices.begin(),indices.end(),[&](const GSID& id){ return id.GetIndexName()==indexName; }))==indices.end() ||
		   index->GetIndexStatus()!=IndexStatus::ACTIVE));
	if(!outcome.IsSuccess())
		log_fatal("Table " << tableName << " does not seem to be available? "
				  "Dynamo error: " << outcome.GetError().GetMessage());
}
	


void waitUntilIndexDeleted(Aws::DynamoDB::DynamoDBClient& dbClient, 
                        const std::string& tableName, 
                        const std::string& indexName){
	using namespace Aws::DynamoDB::Model;
	log_info("Waiting for index " << indexName << " of table " << tableName << " to be deleted");
	DescribeTableOutcome outcome;
	using GSID=GlobalSecondaryIndexDescription;
	Aws::Vector<GSID> indices;
	Aws::Vector<GSID>::iterator index;
	do{
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		outcome=dbClient.DescribeTable(DescribeTableRequest()
		                               .WithTableName(tableName));
	}while(outcome.IsSuccess() &&
		   !(indices=outcome.GetResult().GetTable().GetGlobalSecondaryIndexes()).empty() &&
		   (index=std::find_if(indices.begin(),indices.end(),[&](const GSID& id){ return id.GetIndexName()==indexName; }))!=indices.end());
	if(!outcome.IsSuccess())
		log_fatal("Table " << tableName << " does not seem to be available? "
				  "Dynamo error: " << outcome.GetError().GetMessage());
}

///A default string value to use in place of missing properties, when having a 
///trivial value is not a big concern
const Aws::DynamoDB::Model::AttributeValue missingString(" ");

template<typename Cache, typename Key=typename Cache::key_type, typename Value=typename Cache::mapped_type>
void replaceCacheRecord(Cache& cache, const Key& key, const Value& value){
	cache.upsert(key,[&value](Value& existing){ existing=value; },value);
}

} //anonymous namespace

///Check whether the set of cached records for a category is up to date, and if
///so return only those which are not stale. 
#define maybeReturnCachedCategoryMembers(cache,key) \
do{ \
	auto cachedCategory = cache.find(key); \
	using ResultType=typename decltype(cache)::mapped_type::value_type; \
	std::vector<ResultType> results; \
	if(cachedCategory.second > std::chrono::steady_clock::now()){ \
		for(const auto record : cachedCategory.first){ \
			if(record){ \
				results.push_back(record); \
				cacheHits++; \
			} \
		} \
		return results; \
	} \
}while(0)

const std::string PersistentStore::wildcard="*";
const std::string PersistentStore::wildcardName="<all>";

PersistentStore::PersistentStore(const Aws::Auth::AWSCredentials& credentials, 
                                 const Aws::Client::ClientConfiguration& clientConfig,
                                 std::string bootstrapUserFile,
                                 std::string encryptionKeyFile,
                                 std::string appLoggingServerName,
                                 unsigned int appLoggingServerPort):
	dbClient(credentials,clientConfig),
	userTableName("SLATE_users"),
	groupTableName("SLATE_groups"),
	clusterTableName("SLATE_clusters"),
	instanceTableName("SLATE_instances"),
	secretTableName("SLATE_secrets"),
	monCredTableName("SLATE_moncreds"),
	dnsClient(credentials,clientConfig),
	baseDomain("slateci.net"),
	clusterConfigDir(makeTemporaryDir("/var/tmp/slate_")),
	userCacheValidity(std::chrono::minutes(5)),
	userCacheExpirationTime(std::chrono::steady_clock::now()),
	groupCacheValidity(std::chrono::minutes(30)),
	groupCacheExpirationTime(std::chrono::steady_clock::now()),
	clusterCacheValidity(std::chrono::minutes(30)),
	clusterCacheExpirationTime(std::chrono::steady_clock::now()),
	instanceCacheValidity(std::chrono::minutes(5)),
	instanceCacheExpirationTime(std::chrono::steady_clock::now()),
	secretCacheValidity(std::chrono::minutes(5)),
	secretKey(1024),
	appLoggingServerName(appLoggingServerName),
	appLoggingServerPort(appLoggingServerPort),
	cacheHits(0),databaseQueries(0),databaseScans(0)
{
	loadEncyptionKey(encryptionKeyFile);
	log_info("Starting database client");
	InitializeTables(bootstrapUserFile);
	log_info("Database client ready");
}

void PersistentStore::InitializeUserTable(std::string bootstrapUserFile){
	using namespace Aws::DynamoDB::Model;
	using AttDef=Aws::DynamoDB::Model::AttributeDefinition;
	using SAT=Aws::DynamoDB::Model::ScalarAttributeType;
	
	//define indices
	auto getByTokenIndex=[](){
		return GlobalSecondaryIndex()
		       .WithIndexName("ByToken")
		       .WithKeySchema({KeySchemaElement()
		                       .WithAttributeName("token")
		                       .WithKeyType(KeyType::HASH)})
		       .WithProjection(Projection()
		                       .WithProjectionType(ProjectionType::INCLUDE)
		                       .WithNonKeyAttributes({"ID","name","globusID","email","phone","institution","admin"}))
		       .WithProvisionedThroughput(ProvisionedThroughput()
		                                  .WithReadCapacityUnits(1)
		                                  .WithWriteCapacityUnits(1));
	};
	auto getByGlobusIDIndex=[](){
		return GlobalSecondaryIndex()
		       .WithIndexName("ByGlobusID")
		       .WithKeySchema({KeySchemaElement()
		                       .WithAttributeName("globusID")
		                       .WithKeyType(KeyType::HASH)})
		       .WithProjection(Projection()
		                       .WithProjectionType(ProjectionType::INCLUDE)
		                       .WithNonKeyAttributes({"ID","name","token","email","phone","institution","admin"}))
		       .WithProvisionedThroughput(ProvisionedThroughput()
		                                  .WithReadCapacityUnits(1)
		                                  .WithWriteCapacityUnits(1));
	};
	auto getByGroupIndex=[](){
		return GlobalSecondaryIndex()
		       .WithIndexName("ByGroup")
		       .WithKeySchema({KeySchemaElement()
		                       .WithAttributeName("groupID")
		                       .WithKeyType(KeyType::HASH)})
		       .WithProjection(Projection()
		                       .WithProjectionType(ProjectionType::INCLUDE)
		                       .WithNonKeyAttributes({"ID"}))
		       .WithProvisionedThroughput(ProvisionedThroughput()
		                                  .WithReadCapacityUnits(1)
		                                  .WithWriteCapacityUnits(1));
	};
	
	//check status of the table
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
			AttDef().WithAttributeName("groupID").WithAttributeType(SAT::S)
		});
		request.SetKeySchema({
			KeySchemaElement().WithAttributeName("ID").WithKeyType(KeyType::HASH),
			KeySchemaElement().WithAttributeName("sortKey").WithKeyType(KeyType::RANGE)
		});
		request.SetProvisionedThroughput(ProvisionedThroughput()
		                                 .WithReadCapacityUnits(1)
		                                 .WithWriteCapacityUnits(1));
		request.AddGlobalSecondaryIndexes(getByTokenIndex());
		request.AddGlobalSecondaryIndexes(getByGlobusIDIndex());
		request.AddGlobalSecondaryIndexes(getByGroupIndex());
		
		auto createOut=dbClient.CreateTable(request);
		if(!createOut.IsSuccess())
			log_fatal("Failed to create user table: " + createOut.GetError().GetMessage());
		
		waitTableReadiness(dbClient,userTableName);
		
		{
			try{
				User portal;
				std::ifstream credFile(bootstrapUserFile);
				if(!credFile)
					log_fatal("Unable to read portal user credentials");
				credFile >> portal.id >> portal.name >> portal.email 
						 >> portal.phone >> portal.institution >> portal.token;
				if(credFile.fail())
					log_fatal("Unable to read portal user credentials");
				portal.globusID="No Globus ID";
				portal.admin=true;
				portal.valid=true;
				if(!addUser(portal))
					log_fatal("Failed to inject portal user");
			}
			catch(...){
				log_error("Failed to inject portal user; deleting users table");
				//Demolish the whole table again. This is technically overkill, but it ensures that
				//on the next start up this step will be run again (hpefully with better results).
				auto outc=dbClient.DeleteTable(Aws::DynamoDB::Model::DeleteTableRequest().WithTableName(userTableName));
				//If the table deletion fails it is still possible to get stuck on a restart, but 
				//it isn't clear what else could be done about such a failure. 
				if(!outc.IsSuccess())
					log_error("Failed to delete users tble: " << outc.GetError().GetMessage());
				throw;
			}
		}
		log_info("Created user table");
	}
	else{ //table exists; check whether any indices are missing
		TableDescription tableDesc=userTableOut.GetResult().GetTable();
		
		//check whether any indices are out of date
		bool changed=false;
		if(hasIndex(tableDesc,"ByToken") && 
		  (!indexHasNonKeyProjection(tableDesc,"ByToken","phone") ||
		   !indexHasNonKeyProjection(tableDesc,"ByToken","institution"))){
			log_info("Deleting by-token index");
			UpdateTableRequest req=UpdateTableRequest().WithTableName(userTableName);
			req.AddGlobalSecondaryIndexUpdates(GlobalSecondaryIndexUpdate().WithDelete(DeleteGlobalSecondaryIndexAction().WithIndexName("ByToken")));
			auto updateResult=dbClient.UpdateTable(req);
			if(!updateResult.IsSuccess())
				log_fatal("Failed to delete incomplete ByToken secondary index from user table: " + updateResult.GetError().GetMessage());
			waitUntilIndexDeleted(dbClient,groupTableName,"ByToken");
			changed=true;
		}
		if(hasIndex(tableDesc,"ByGlobusID") && 
		  (!indexHasNonKeyProjection(tableDesc,"ByGlobusID","phone") ||
		   !indexHasNonKeyProjection(tableDesc,"ByGlobusID","institution"))){
			log_info("Deleting by-globus-id index");
			UpdateTableRequest req=UpdateTableRequest().WithTableName(userTableName);
			req.AddGlobalSecondaryIndexUpdates(GlobalSecondaryIndexUpdate().WithDelete(DeleteGlobalSecondaryIndexAction().WithIndexName("ByGlobusID")));
			auto updateResult=dbClient.UpdateTable(req);
			if(!updateResult.IsSuccess())
				log_fatal("Failed to delete incomplete ByGlobusID secondary index from user table: " + updateResult.GetError().GetMessage());
			waitUntilIndexDeleted(dbClient,groupTableName,"ByGlobusID");
			changed=true;
		}
		
		//if an index was deleted, update the table description so we know to recreate it
		if(changed){
			userTableOut=dbClient.DescribeTable(DescribeTableRequest()
			                                  .WithTableName(userTableName));
			tableDesc=userTableOut.GetResult().GetTable();
		}
		
		if(!hasIndex(tableDesc,"ByToken")){
			auto request=updateTableWithNewSecondaryIndex(userTableName,getByTokenIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("token").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-token index to user table: " + createOut.GetError().GetMessage());
			waitIndexReadiness(dbClient,userTableName,"ByToken");
			log_info("Added by-token index to user table");
		}
		if(!hasIndex(tableDesc,"ByGlobusID")){
			auto request=updateTableWithNewSecondaryIndex(userTableName,getByGlobusIDIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("globusID").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-GlobusID index to user table: " + createOut.GetError().GetMessage());
			waitIndexReadiness(dbClient,userTableName,"ByGlobusID");
			log_info("Added by-GlobusID index to user table");
		}
		if(!hasIndex(tableDesc,"ByGroup")){
			auto request=updateTableWithNewSecondaryIndex(userTableName,getByGroupIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("groupID").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-Group index to user table: " + createOut.GetError().GetMessage());
			waitIndexReadiness(dbClient,userTableName,"ByGroup");
			log_info("Added by-Group index to user table");
		}
	}
}

void PersistentStore::InitializeGroupTable(){
	using namespace Aws::DynamoDB::Model;
	using AttDef=Aws::DynamoDB::Model::AttributeDefinition;
	using SAT=Aws::DynamoDB::Model::ScalarAttributeType;
	
	//define indices
	auto getByNameIndex=[](){
		return GlobalSecondaryIndex()
		       .WithIndexName("ByName")
		       .WithKeySchema({KeySchemaElement()
		                       .WithAttributeName("name")
		                       .WithKeyType(KeyType::HASH)})
		       .WithProjection(Projection()
		                       .WithProjectionType(ProjectionType::INCLUDE)
		                       .WithNonKeyAttributes({"ID","email","phone","scienceField","description"}))
		       .WithProvisionedThroughput(ProvisionedThroughput()
		                                  .WithReadCapacityUnits(1)
		                                  .WithWriteCapacityUnits(1));
	};
	
	//check status of the table
	auto groupTableOut=dbClient.DescribeTable(DescribeTableRequest()
											 .WithTableName(groupTableName));
	if(!groupTableOut.IsSuccess() &&
	   groupTableOut.GetError().GetErrorType()!=Aws::DynamoDB::DynamoDBErrors::RESOURCE_NOT_FOUND){
		log_fatal("Unable to connect to DynamoDB: "
		          << groupTableOut.GetError().GetMessage());
	}
	if(!groupTableOut.IsSuccess()){
		log_info("groups table does not exist; creating");
		auto request=CreateTableRequest();
		request.SetTableName(groupTableName);
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
		request.AddGlobalSecondaryIndexes(getByNameIndex());
		
		auto createOut=dbClient.CreateTable(request);
		if(!createOut.IsSuccess())
			log_fatal("Failed to create groups table: " + createOut.GetError().GetMessage());
		
		waitTableReadiness(dbClient,groupTableName);
		log_info("Created groups table");
	}
	else{ //table exists; check whether any indices are missing
		TableDescription tableDesc=groupTableOut.GetResult().GetTable();
		
		//check whether any indices are out of date
		bool changed=false;
		if(hasIndex(tableDesc,"ByName") && 
		  (!indexHasNonKeyProjection(tableDesc,"ByName","email") ||
		   !indexHasNonKeyProjection(tableDesc,"ByName","phone") ||
		   !indexHasNonKeyProjection(tableDesc,"ByName","scienceField") ||
		   !indexHasNonKeyProjection(tableDesc,"ByName","description"))){
			log_info("Deleting by-name index");
			UpdateTableRequest req=UpdateTableRequest().WithTableName(groupTableName);
			req.AddGlobalSecondaryIndexUpdates(GlobalSecondaryIndexUpdate().WithDelete(DeleteGlobalSecondaryIndexAction().WithIndexName("ByName")));
			auto updateResult=dbClient.UpdateTable(req);
			if(!updateResult.IsSuccess())
				log_fatal("Failed to delete incomplete secondary index from Group table: " + updateResult.GetError().GetMessage());
			waitUntilIndexDeleted(dbClient,groupTableName,"ByName");
			changed=true;
		}
		
		//if an index was deleted, update the table description so we know to recreate it
		if(changed){
			groupTableOut=dbClient.DescribeTable(DescribeTableRequest()
			                                  .WithTableName(groupTableName));
			tableDesc=groupTableOut.GetResult().GetTable();
		}
		
		if(!hasIndex(tableDesc,"ByName")){
			auto request=updateTableWithNewSecondaryIndex(groupTableName,getByNameIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("name").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-name index to Group table: " + createOut.GetError().GetMessage());
			waitIndexReadiness(dbClient,groupTableName,"ByName");
			log_info("Added by-name index to Group table");
		}
	}
}

void PersistentStore::InitializeClusterTable(){
	using namespace Aws::DynamoDB::Model;
	using AttDef=Aws::DynamoDB::Model::AttributeDefinition;
	using SAT=Aws::DynamoDB::Model::ScalarAttributeType;
	
	//define indices
	auto getByGroupIndex=[](){
		return GlobalSecondaryIndex()
		       .WithIndexName("ByGroup")
		       .WithKeySchema({KeySchemaElement()
		                       .WithAttributeName("owningGroup")
		                       .WithKeyType(KeyType::HASH)})
		       .WithProjection(Projection()
		                       .WithProjectionType(ProjectionType::INCLUDE)
		                       .WithNonKeyAttributes({"ID","name","config","systemNamespace","owningOrganization","monCredential"}))
		       .WithProvisionedThroughput(ProvisionedThroughput()
		                                  .WithReadCapacityUnits(1)
		                                  .WithWriteCapacityUnits(1));
	};
	auto getByNameIndex=[](){
		return GlobalSecondaryIndex()
		       .WithIndexName("ByName")
		       .WithKeySchema({KeySchemaElement()
		                       .WithAttributeName("name")
		                       .WithKeyType(KeyType::HASH)})
		       .WithProjection(Projection()
		                       .WithProjectionType(ProjectionType::INCLUDE)
		                       .WithNonKeyAttributes({"ID","owningGroup","config","systemNamespace","owningOrganization","monCredential"}))
		       .WithProvisionedThroughput(ProvisionedThroughput()
		                                  .WithReadCapacityUnits(1)
		                                  .WithWriteCapacityUnits(1));
	};
	auto getGroupAccessIndex=[](){
		return GlobalSecondaryIndex()
		.WithIndexName("GroupAccess")
		.WithKeySchema({KeySchemaElement()
			.WithAttributeName("groupID")
			.WithKeyType(KeyType::HASH)})
		.WithProjection(Projection()
						.WithProjectionType(ProjectionType::INCLUDE)
						.WithNonKeyAttributes({"ID"}))
		.WithProvisionedThroughput(ProvisionedThroughput()
								   .WithReadCapacityUnits(1)
								   .WithWriteCapacityUnits(1));
	};
	
	//check status of the table
	auto clusterTableOut=dbClient.DescribeTable(DescribeTableRequest()
											 .WithTableName(clusterTableName));
	if(!clusterTableOut.IsSuccess() &&
	   clusterTableOut.GetError().GetErrorType()!=Aws::DynamoDB::DynamoDBErrors::RESOURCE_NOT_FOUND){
		log_fatal("Unable to connect to DynamoDB: "
		          << clusterTableOut.GetError().GetMessage());
	}
	if(!clusterTableOut.IsSuccess()){
		log_info("Clusters table does not exist; creating");
		auto request=CreateTableRequest();
		request.SetTableName(clusterTableName);
		request.SetAttributeDefinitions({
			AttDef().WithAttributeName("ID").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("sortKey").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("name").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("owningGroup").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("groupID").WithAttributeType(SAT::S)
		});
		request.SetKeySchema({
			KeySchemaElement().WithAttributeName("ID").WithKeyType(KeyType::HASH),
			KeySchemaElement().WithAttributeName("sortKey").WithKeyType(KeyType::RANGE)
		});
		request.SetProvisionedThroughput(ProvisionedThroughput()
		                                 .WithReadCapacityUnits(1)
		                                 .WithWriteCapacityUnits(1));
		
		request.AddGlobalSecondaryIndexes(getByGroupIndex());
		request.AddGlobalSecondaryIndexes(getByNameIndex());
		request.AddGlobalSecondaryIndexes(getGroupAccessIndex());
		
		auto createOut=dbClient.CreateTable(request);
		if(!createOut.IsSuccess())
			log_fatal("Failed to create clusters table: " + createOut.GetError().GetMessage());
		
		waitTableReadiness(dbClient,clusterTableName);
		log_info("Created clusters table");
	}
	else{ //table exists; check whether any indices are missing
		TableDescription tableDesc=clusterTableOut.GetResult().GetTable();
			
		//check whether any indices are out of date
		bool changed=false;
		if(hasIndex(tableDesc,"ByGroup") && 
		  (!indexHasNonKeyProjection(tableDesc,"ByGroup","systemNamespace") ||
		   !indexHasNonKeyProjection(tableDesc,"ByGroup","owningOrganization") ||
		   !indexHasNonKeyProjection(tableDesc,"ByGroup","monCredential"))){
			log_info("Deleting by-Group index");
			UpdateTableRequest req=UpdateTableRequest().WithTableName(clusterTableName);
			//req.AddAttributeDefinitions(AttDef().WithAttributeName("systemNamespace").WithAttributeType(SAT::S));
			req.AddGlobalSecondaryIndexUpdates(GlobalSecondaryIndexUpdate().WithDelete(DeleteGlobalSecondaryIndexAction().WithIndexName("ByGroup")));
			auto updateResult=dbClient.UpdateTable(req);
			if(!updateResult.IsSuccess())
				log_fatal("Failed to delete incomplete secondary index from cluster table: " + updateResult.GetError().GetMessage());
			waitUntilIndexDeleted(dbClient,clusterTableName,"ByGroup");
			changed=true;
		}
		
		if(hasIndex(tableDesc,"ByName") && 
		  (!indexHasNonKeyProjection(tableDesc,"ByName","systemNamespace") ||
		   !indexHasNonKeyProjection(tableDesc,"ByName","owningOrganization") ||
		   !indexHasNonKeyProjection(tableDesc,"ByName","monCredential"))){
			log_info("Deleting by-name index");
			UpdateTableRequest req=UpdateTableRequest().WithTableName(clusterTableName);
			req.AddGlobalSecondaryIndexUpdates(GlobalSecondaryIndexUpdate().WithDelete(DeleteGlobalSecondaryIndexAction().WithIndexName("ByName")));
			auto updateResult=dbClient.UpdateTable(req);
			if(!updateResult.IsSuccess())
				log_fatal("Failed to delete incomplete secondary index from cluster table: " + updateResult.GetError().GetMessage());
			waitUntilIndexDeleted(dbClient,clusterTableName,"ByName");
			changed=true;
		}
		
		//if an index was deleted, update the table description so we know to recreate it
		if(changed){
			clusterTableOut=dbClient.DescribeTable(DescribeTableRequest()
			                                       .WithTableName(clusterTableName));
			tableDesc=clusterTableOut.GetResult().GetTable();
		}
		
		if(!hasIndex(tableDesc,"ByGroup")){
			auto request=updateTableWithNewSecondaryIndex(clusterTableName,getByGroupIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("owningGroup").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-Group index to cluster table: " + createOut.GetError().GetMessage());
			waitIndexReadiness(dbClient,clusterTableName,"ByGroup");
			log_info("Added by-Group index to cluster table");
		}
		if(!hasIndex(tableDesc,"ByName")){
			auto request=updateTableWithNewSecondaryIndex(clusterTableName,getByNameIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("name").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-name index to cluster table: " + createOut.GetError().GetMessage());
			waitIndexReadiness(dbClient,clusterTableName,"ByName");
			log_info("Added by-name index to cluster table");
		}
		if(!hasIndex(tableDesc,"GroupAccess")){
			auto request=updateTableWithNewSecondaryIndex(clusterTableName,getGroupAccessIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("groupID").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add Group access index to cluster table: " + createOut.GetError().GetMessage());
			waitIndexReadiness(dbClient,clusterTableName,"GroupAccess");
			log_info("Added Group access index to cluster table");
		}
	}
}

void PersistentStore::InitializeInstanceTable(){
	using namespace Aws::DynamoDB::Model;
	using AttDef=Aws::DynamoDB::Model::AttributeDefinition;
	using SAT=Aws::DynamoDB::Model::ScalarAttributeType;
	
	//define indices
	auto getByGroupIndex=[](){
		return GlobalSecondaryIndex()
		       .WithIndexName("ByGroup")
		       .WithKeySchema({KeySchemaElement()
		                       .WithAttributeName("owningGroup")
		                       .WithKeyType(KeyType::HASH)})
		       .WithProjection(Projection()
		                       .WithProjectionType(ProjectionType::INCLUDE)
		                       .WithNonKeyAttributes({"ID","name","application","cluster","ctime"}))
		       .WithProvisionedThroughput(ProvisionedThroughput()
		                                  .WithReadCapacityUnits(1)
		                                  .WithWriteCapacityUnits(1));
	};
	auto getByNameIndex=[](){
		return GlobalSecondaryIndex()
		       .WithIndexName("ByName")
		       .WithKeySchema({KeySchemaElement()
		                       .WithAttributeName("name")
		                       .WithKeyType(KeyType::HASH)})
		       .WithProjection(Projection()
		                       .WithProjectionType(ProjectionType::INCLUDE)
		                       .WithNonKeyAttributes({"ID","application","owningGroup","cluster","ctime"}))
		       .WithProvisionedThroughput(ProvisionedThroughput()
		                                  .WithReadCapacityUnits(1)
		                                  .WithWriteCapacityUnits(1));
	};
	auto getByClusterIndex=[](){
		return GlobalSecondaryIndex()
		       .WithIndexName("ByCluster")
		       .WithKeySchema({KeySchemaElement()
				               .WithAttributeName("cluster")
				               .WithKeyType(KeyType::HASH)})
		       .WithProjection(Projection()
				               .WithProjectionType(ProjectionType::INCLUDE)
				               .WithNonKeyAttributes({"ID", "name", "application", "owningGroup", "ctime"}))
		       .WithProvisionedThroughput(ProvisionedThroughput()
				                          .WithReadCapacityUnits(1)
				                          .WithWriteCapacityUnits(1));
	};
	
	auto instanceTableOut=dbClient.DescribeTable(DescribeTableRequest()
	                                             .WithTableName(instanceTableName));
	if(!instanceTableOut.IsSuccess() &&
	   instanceTableOut.GetError().GetErrorType()!=Aws::DynamoDB::DynamoDBErrors::RESOURCE_NOT_FOUND){
		log_fatal("Unable to connect to DynamoDB: "
		          << instanceTableOut.GetError().GetMessage());
	}
	if(!instanceTableOut.IsSuccess()){
		log_info("Instance table does not exist; creating");
		auto request=CreateTableRequest();
		request.SetTableName(instanceTableName);
		request.SetAttributeDefinitions({
			AttDef().WithAttributeName("ID").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("sortKey").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("name").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("owningGroup").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("cluster").WithAttributeType(SAT::S),
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
		request.AddGlobalSecondaryIndexes(getByGroupIndex());
		request.AddGlobalSecondaryIndexes(getByNameIndex());
		request.AddGlobalSecondaryIndexes(getByClusterIndex());
		
		auto createOut=dbClient.CreateTable(request);
		if(!createOut.IsSuccess())
			log_fatal("Failed to create instance table: " + createOut.GetError().GetMessage());
		
		waitTableReadiness(dbClient,instanceTableName);
		log_info("Created Instances table");
	}
	else{ //table exists; check whether any indices are missing
		const TableDescription& tableDesc=instanceTableOut.GetResult().GetTable();
		
		if(!hasIndex(tableDesc,"ByGroup")){
			auto request=updateTableWithNewSecondaryIndex(instanceTableName,getByGroupIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("owningGroup").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-Group index to instance table: " + createOut.GetError().GetMessage());
			waitTableReadiness(dbClient,instanceTableName);
			log_info("Added by-Group index to instance table");
		}
		if(!hasIndex(tableDesc,"ByName")){
			auto request=updateTableWithNewSecondaryIndex(instanceTableName,getByNameIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("name").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-name index to instance table: " + createOut.GetError().GetMessage());
			waitTableReadiness(dbClient,instanceTableName);
			log_info("Added by-name index to instance table");
		}
		if(!hasIndex(tableDesc,"ByCluster")){
			auto request=updateTableWithNewSecondaryIndex(instanceTableName,getByClusterIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("cluster").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-cluster index to instance table: " + createOut.GetError().GetMessage());
			waitTableReadiness(dbClient,instanceTableName);
			log_info("Added by-cluster index to instance table");
		}
	}
}

void PersistentStore::InitializeSecretTable(){
	using namespace Aws::DynamoDB::Model;
	using AttDef=Aws::DynamoDB::Model::AttributeDefinition;
	using SAT=Aws::DynamoDB::Model::ScalarAttributeType;
	
	//define indices
	auto getByGroupIndex=[](){
		return GlobalSecondaryIndex()
		       .WithIndexName("ByGroup")
		       .WithKeySchema({KeySchemaElement()
		                       .WithAttributeName("owningGroup")
		                       .WithKeyType(KeyType::HASH)})
		       .WithProjection(Projection()
		                       .WithProjectionType(ProjectionType::INCLUDE)
		                       .WithNonKeyAttributes({"ID","name","cluster","ctime","contents"}))
		       .WithProvisionedThroughput(ProvisionedThroughput()
		                                  .WithReadCapacityUnits(1)
		                                  .WithWriteCapacityUnits(1));
	};
	
	auto getByClusterIndex=[](){
		return GlobalSecondaryIndex()
		       .WithIndexName("ByCluster")
		       .WithKeySchema({KeySchemaElement()
		                       .WithAttributeName("cluster")
		                       .WithKeyType(KeyType::HASH)})
		       .WithProjection(Projection()
		                       .WithProjectionType(ProjectionType::INCLUDE)
		                       .WithNonKeyAttributes({"ID","name","owningGroup","ctime","contents"}))
		       .WithProvisionedThroughput(ProvisionedThroughput()
		                                  .WithReadCapacityUnits(1)
		                                  .WithWriteCapacityUnits(1));
	};
	
	//check status of the table
	auto secretTableOut=dbClient.DescribeTable(DescribeTableRequest()
											  .WithTableName(secretTableName));
	if(!secretTableOut.IsSuccess() &&
	   secretTableOut.GetError().GetErrorType()!=Aws::DynamoDB::DynamoDBErrors::RESOURCE_NOT_FOUND){
		log_fatal("Unable to connect to DynamoDB: "
		          << secretTableOut.GetError().GetMessage());
	}
	if(!secretTableOut.IsSuccess()){
		log_info("Secrets table does not exist; creating");
		auto request=CreateTableRequest();
		request.SetTableName(secretTableName);
		request.SetAttributeDefinitions({
			AttDef().WithAttributeName("ID").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("sortKey").WithAttributeType(SAT::S),
			//AttDef().WithAttributeName("name").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("owningGroup").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("cluster").WithAttributeType(SAT::S),
			//AttDef().WithAttributeName("ctime").WithAttributeType(SAT::S),
			//AttDef().WithAttributeName("contents").WithAttributeType(SAT::B)
		});
		request.SetKeySchema({
			KeySchemaElement().WithAttributeName("ID").WithKeyType(KeyType::HASH),
			KeySchemaElement().WithAttributeName("sortKey").WithKeyType(KeyType::RANGE)
		});
		request.SetProvisionedThroughput(ProvisionedThroughput()
		                                 .WithReadCapacityUnits(1)
		                                 .WithWriteCapacityUnits(1));
		request.AddGlobalSecondaryIndexes(getByGroupIndex());
		request.AddGlobalSecondaryIndexes(getByClusterIndex());
		
		auto createOut=dbClient.CreateTable(request);
		if(!createOut.IsSuccess())
			log_fatal("Failed to create secrets table: " + createOut.GetError().GetMessage());
		
		waitTableReadiness(dbClient,secretTableName);
		log_info("Created secrets table");
	}
	else{ //table exists; check whether any indices are missing
		const TableDescription& tableDesc=secretTableOut.GetResult().GetTable();
		
		if(!hasIndex(tableDesc,"ByGroup")){
			auto request=updateTableWithNewSecondaryIndex(secretTableName,getByGroupIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("owningGroup").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-Group index to secret table: " + createOut.GetError().GetMessage());
			waitTableReadiness(dbClient,secretTableName);
			log_info("Added by-Group index to secret table");
		}
		if(!hasIndex(tableDesc,"ByCluster")){
			auto request=updateTableWithNewSecondaryIndex(secretTableName,getByClusterIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("cluster").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-cluster index to secret table: " + createOut.GetError().GetMessage());
			waitTableReadiness(dbClient,secretTableName);
			log_info("Added by-cluster index to secret table");
		}
	}
}

void PersistentStore::InitializeMonCredTable(){
	using namespace Aws::DynamoDB::Model;
	using AttDef=Aws::DynamoDB::Model::AttributeDefinition;
	using SAT=Aws::DynamoDB::Model::ScalarAttributeType;
	
	//check status of the table
	auto credTableOut=dbClient.DescribeTable(DescribeTableRequest()
											  .WithTableName(monCredTableName));
	if(!credTableOut.IsSuccess() &&
	   credTableOut.GetError().GetErrorType()!=Aws::DynamoDB::DynamoDBErrors::RESOURCE_NOT_FOUND){
		log_fatal("Unable to connect to DynamoDB: "
		          << credTableOut.GetError().GetMessage());
	}
	if(!credTableOut.IsSuccess()){
		log_info("Monitoring Credentials table does not exist; creating");
		auto request=CreateTableRequest();
		request.SetTableName(monCredTableName);
		request.SetAttributeDefinitions({
			AttDef().WithAttributeName("accessKey").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("sortKey").WithAttributeType(SAT::S),
		});
		request.SetKeySchema({
			KeySchemaElement().WithAttributeName("accessKey").WithKeyType(KeyType::HASH),
			KeySchemaElement().WithAttributeName("sortKey").WithKeyType(KeyType::RANGE)
		});
		request.SetProvisionedThroughput(ProvisionedThroughput()
		                                 .WithReadCapacityUnits(1)
		                                 .WithWriteCapacityUnits(1));
		
		auto createOut=dbClient.CreateTable(request);
		if(!createOut.IsSuccess())
			log_fatal("Failed to create monitoring credentials table: " + createOut.GetError().GetMessage());
		
		waitTableReadiness(dbClient,monCredTableName);
		log_info("Created monitoring credentials table");
	}
	/*else{ //table exists; check whether any indices are missing
		const TableDescription& tableDesc=credTableOut.GetResult().GetTable();
	}*/
}

void PersistentStore::InitializeTables(std::string bootstrapUserFile){
	InitializeUserTable(bootstrapUserFile);
	InitializeGroupTable();
	InitializeClusterTable();
	InitializeInstanceTable();
	InitializeSecretTable();
	InitializeMonCredTable();
}

void PersistentStore::loadEncyptionKey(const std::string& fileName){
	std::ifstream infile(fileName);
	if(!infile)
		log_fatal("Unable to open " << fileName << " to read encryption key");
	infile.read(secretKey.data.get(),1024);
	if(infile.bad() || infile.gcount()==0)
		log_fatal("Failed to read encryption key");
	secretKey.dataSize=infile.gcount();
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
		{"phone",AttributeValue(user.phone)},
		{"institution",AttributeValue(user.institution)},
		{"admin",AttributeValue().SetBool(user.admin)}
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to add user record: " << err.GetMessage());
		return false;
	}
	
	//update caches
	CacheRecord<User> record(user,userCacheValidity);
	replaceCacheRecord(userCache,user.id,record);
	replaceCacheRecord(userByTokenCache,user.token,record);
	replaceCacheRecord(userByGlobusIDCache,user.globusID,record);
	
	return true;
}

User PersistentStore::getUser(const std::string& id){
	//first see if we have this cached
	{
		CacheRecord<User> record;
		if(userCache.find(id,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return record;
			}
		}
	}
	//need to query the database
	databaseQueries++;
	log_info("Querying database for user " << id);
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
	user.name=findOrThrow(item,"name","user record missing name attribute").GetS();
	user.email=findOrThrow(item,"email","user record missing email attribute").GetS();
	user.phone=findOrDefault(item,"phone",missingString).GetS();
	user.institution=findOrDefault(item,"institution",missingString).GetS();
	user.token=findOrThrow(item,"token","user record missing token attribute").GetS();
	user.globusID=findOrThrow(item,"globusID","user record missing globusID attribute").GetS();
	user.admin=findOrThrow(item,"admin","user record missing admin attribute").GetBool();
	
	//update caches
	CacheRecord<User> record(user,userCacheValidity);
	replaceCacheRecord(userCache,user.id,record);
	replaceCacheRecord(userByTokenCache,user.token,record);
	replaceCacheRecord(userByGlobusIDCache,user.globusID,record);
	
	return user;
}

User PersistentStore::findUserByToken(const std::string& token){
	//first see if we have this cached
	{
		CacheRecord<User> record;
		if(userByTokenCache.find(token,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return record;
			}
		}
	}
	//need to query the database
	databaseQueries++;
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
	
	const auto& item=queryResult.GetItems().front();
	User user;
	user.valid=true;
	user.token=token;
	user.id=findOrThrow(item,"ID","user record missing ID attribute").GetS();
	user.name=findOrThrow(item,"name","user record missing name attribute").GetS();
	user.globusID=findOrThrow(item,"globusID","user record missing globusID attribute").GetS();
	user.email=findOrThrow(item,"email","user record missing eamil attribute").GetS();
	user.phone=findOrDefault(item,"phone",missingString).GetS();
	user.institution=findOrDefault(item,"institution",missingString).GetS();
	user.admin=findOrThrow(item,"admin","user record missing admin attribute").GetBool();
	
	//update caches
	CacheRecord<User> record(user,userCacheValidity);
	replaceCacheRecord(userCache,user.id,record);
	replaceCacheRecord(userByTokenCache,user.token,record);
	replaceCacheRecord(userByGlobusIDCache,user.globusID,record);
	
	return user;
}

User PersistentStore::findUserByGlobusID(const std::string& globusID){
	//first see if we have this cached
	{
		CacheRecord<User> record;
		if(userByGlobusIDCache.find(globusID,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return record;
			}
		}
	}
	//need to query the database
	databaseQueries++;
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
	
	const auto& item=queryResult.GetItems().front();
	User user;
	user.valid=true;
	user.id=findOrThrow(item,"ID","user record missing name attribute").GetS();
	user.name=findOrThrow(item,"name","user record missing name attribute").GetS();
	user.globusID=globusID;
	user.token=findOrThrow(item,"token","user record missing token attribute").GetS();
	user.email=findOrThrow(item,"email","user record missing eamil attribute").GetS();
	user.phone=findOrDefault(item,"phone",missingString).GetS();
	user.institution=findOrDefault(item,"institution",missingString).GetS();
	user.admin=findOrThrow(item,"admin","user record missing admin attribute").GetBool();
	
	//update caches
	CacheRecord<User> record(user,userCacheValidity);
	replaceCacheRecord(userCache,user.id,record);
	replaceCacheRecord(userByTokenCache,user.token,record);
	replaceCacheRecord(userByGlobusIDCache,user.globusID,record);
	
	return user;
}

bool PersistentStore::updateUser(const User& user, const User& oldUser){
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
	                                            {"phone",AVU().WithValue(AV(user.phone))},
	                                            {"institution",AVU().WithValue(AV(user.institution))},
	                                            {"admin",AVU().WithValue(AV().SetBool(user.admin))}
	                                 }));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to update user record: " << err.GetMessage());
		return false;
	}
	
	//update caches
	CacheRecord<User> record(user,userCacheValidity);
	replaceCacheRecord(userCache,user.id,record);
	//if the token has changed, ensure that any old cache record is removed
	if(oldUser.token!=user.token)
		userByTokenCache.erase(oldUser.token);
	replaceCacheRecord(userByTokenCache,user.token,record);
	replaceCacheRecord(userByGlobusIDCache,user.globusID,record);
	
	return true;
}

bool PersistentStore::removeUser(const std::string& id){
	//erase cache entries
	{
		//Somewhat hacky: we can't erase the secondary cache entries unless we know 
		//the keys. However, we keep the caches synchronized, so if there is 
		//such an entry to delete there is also an entry in the main cache, so 
		//we can grab that to get the name without having to read from the 
		//database.
		CacheRecord<User> record;
		bool cached=userCache.find(id,record);
		if(cached){
			//don't particularly care whether the record is expired; if it is 
			//all that will happen is that we will delete the equally stale 
			//record in the other cache
			userByTokenCache.erase(record.record.token);
			userByGlobusIDCache.erase(record.record.globusID);
		}
		userCache.erase(id);
	}
	
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
	//First check if users are cached
	if(userCacheExpirationTime.load() > std::chrono::steady_clock::now()){
		auto table = userCache.lock_table();
		for(auto itr = table.cbegin(); itr != table.cend(); itr++){
			auto user = itr->second;
			cacheHits++;
			collected.push_back(user);
		}
		table.unlock();
		return collected;
	}
	
	databaseScans++;
	Aws::DynamoDB::Model::ScanRequest request;
	request.SetTableName(userTableName);
	//request.SetAttributesToGet({"ID","name","email"});
	request.SetFilterExpression("attribute_not_exists(#groupID)");
	request.SetExpressionAttributeNames({{"#groupID", "groupID"}});
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
			user.globusID=item.find("globusID")->second.GetS();
			user.token=item.find("token")->second.GetS();
			user.name=item.find("name")->second.GetS();
			user.email=item.find("email")->second.GetS();
			user.phone=findOrDefault(item,"phone",missingString).GetS();
			user.institution=findOrDefault(item,"institution",missingString).GetS();
			user.admin=item.find("admin")->second.GetBool();
			collected.push_back(user);

			CacheRecord<User> record(user,userCacheValidity);
	replaceCacheRecord(		userCache,user.id,record);
		}
	}while(keepGoing);
	userCacheExpirationTime=std::chrono::steady_clock::now()+userCacheValidity;
	
	return collected;
}

std::vector<User> PersistentStore::listUsersByGroup(const std::string& group){
	//first check if list of users is cached
	auto cached = userByGroupCache.find(group);
	if (cached.second > std::chrono::steady_clock::now()) {
		auto records = cached.first;
		std::vector<User> users;
		for (auto record : records) {
			cacheHits++;
			auto user = getUser(record);
			users.push_back(user);
		}
		return users;
	}

	std::vector<User> users;
	using AV=Aws::DynamoDB::Model::AttributeValue;
	databaseQueries++;

	Aws::DynamoDB::Model::QueryOutcome outcome;
	outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
			       .WithTableName(userTableName)
			       .WithIndexName("ByGroup")
			       .WithKeyConditionExpression("#groupID = :group_val")
			       .WithExpressionAttributeNames({{"#groupID", "groupID"}})
			       .WithExpressionAttributeValues({{":group_val", AV(group)}})
			       );
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to list Users by Group: " << err.GetMessage());
		return users;
	}

	const auto& queryResult=outcome.GetResult();
	if(queryResult.GetCount()==0)
		return users;

	for(const auto& item : queryResult.GetItems()){
		User user;
		user.id=findOrThrow(item, "ID", "User record missing ID attribute").GetS();
		user=getUser(user.id);
		users.push_back(user);

		//update caches
		CacheRecord<User> record(user,userCacheValidity);
		replaceCacheRecord(userCache,user.id,record);
		CacheRecord<std::string> groupRecord(user.id,userCacheValidity);
		userByGroupCache.insert_or_assign(group,groupRecord);
	}
	userByGroupCache.update_expiration(group,std::chrono::steady_clock::now()+userCacheValidity);
	
	return users;	
}

bool PersistentStore::addUserToGroup(const std::string& uID, std::string groupID){
	//check whether the 'ID' we got was actually a name
	if(!normalizeGroupID(groupID))
		return false;

	Group group = findGroupByID(groupID);
	User user = getUser(uID);
	
	using Aws::DynamoDB::Model::AttributeValue;
	auto request=Aws::DynamoDB::Model::PutItemRequest()
	  .WithTableName(userTableName)
	  .WithItem({
		{"ID",AttributeValue(uID)},
		{"sortKey",AttributeValue(uID+":"+groupID)},
		{"groupID",AttributeValue(groupID)}
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to add user Group membership record: " << err.GetMessage());
		return false;
	}
	
	//update cache
	CacheRecord<std::string> record(uID,userCacheValidity);
	userByGroupCache.insert_or_assign(groupID,record);
	CacheRecord<Group> groupRecord(group,groupCacheValidity); 
	groupByUserCache.insert_or_assign(user.id, groupRecord);
	
	return true;
}

bool PersistentStore::removeUserFromGroup(const std::string& uID, std::string groupID){
	//check whether the 'ID' we got was actually a name
	if(!normalizeGroupID(groupID))
		return false;
	
	//remove any cache entry
	userByGroupCache.erase(groupID,CacheRecord<std::string>(uID));

	CacheRecord<Group> record;
	bool cached=groupCache.find(groupID,record);
	if (cached)
		groupByUserCache.erase(uID, record);
	
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.DeleteItem(Aws::DynamoDB::Model::DeleteItemRequest()
								     .WithTableName(userTableName)
								     .WithKey({{"ID",AttributeValue(uID)},
	                                           {"sortKey",AttributeValue(uID+":"+groupID)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to delete user Group membership record: " << err.GetMessage());
		return false;
	}
	return true;
}

std::vector<std::string> PersistentStore::getUserGroupMemberships(const std::string& uID, bool useNames){
	using Aws::DynamoDB::Model::AttributeValue;
	databaseQueries++;
	log_info("Querying database for user " << uID << " Group memberships");
	auto request=Aws::DynamoDB::Model::QueryRequest()
	.WithTableName(userTableName)
	.WithKeyConditionExpression("#id = :id AND begins_with(#sortKey,:prefix)")
	.WithExpressionAttributeNames({
		{"#id","ID"},
		{"#sortKey","sortKey"}
	})
	.WithExpressionAttributeValues({
		{":id",AttributeValue(uID)},
		{":prefix",AttributeValue(uID+":"+IDGenerator::groupIDPrefix)}
	});
	auto outcome=dbClient.Query(request);
	std::vector<std::string> vos;
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch user's Group membership records: " << err.GetMessage());
		return vos;
	}
	
	const auto& queryResult=outcome.GetResult();
	for(const auto& item : queryResult.GetItems()){
		if(item.count("groupID"))
			vos.push_back(item.find("groupID")->second.GetS());
	}
	
	if(useNames){
		//do extra lookups to replace IDs with nicer names
		for(std::string& groupStr : vos){
			Group group=findGroupByID(groupStr);
			groupStr=group.name;
		}
	}
	
	return vos;
}

bool PersistentStore::userInGroup(const std::string& uID, std::string groupID){
	//TODO: possible issue: We only store memberships, so repeated queries about
	//a user's belonging to a Group to which that user does not in fact belong will
	//never be in the cache, and will always incur a database query. This should
	//not be a problem for normal/well intentioned use, but seems like a way to
	//turn accident or malice into denial of service or a large AWS bill. 
	
	//check whether the 'ID' we got was actually a name
	if(!normalizeGroupID(groupID))
		return false;
	
	//first see if we have this cached
	{
		CacheRecord<std::string> record(uID);
		if(userByGroupCache.find(groupID,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return record;
			}
		}
	}
	//need to query the database
	databaseQueries++;
	log_info("Querying database for user " << uID << " membership in Group " << groupID);
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.GetItem(Aws::DynamoDB::Model::GetItemRequest()
								  .WithTableName(userTableName)
								  .WithKey({{"ID",AttributeValue(uID)},
	                                        {"sortKey",AttributeValue(uID+":"+groupID)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch user Group membership record: " << err.GetMessage());
		return false;
	}
	const auto& item=outcome.GetResult().GetItem();
	if(item.empty()) //no match found
		return false;
	
	//update cache
	CacheRecord<std::string> record(uID,userCacheValidity);
	userByGroupCache.insert_or_assign(groupID,record);
	
	return true;
}

//----

bool PersistentStore::addGroup(const Group& group){
	if(group.email.empty())
		throw std::runtime_error("Group email must not be empty because Dynamo");
	if(group.phone.empty())
		throw std::runtime_error("Group phone must not be empty because Dynamo");
	if(group.scienceField.empty())
		throw std::runtime_error("Group scienceField must not be empty because Dynamo");
	if(group.description.empty())
		throw std::runtime_error("Group description must not be empty because Dynamo");
	using AV=Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.PutItem(Aws::DynamoDB::Model::PutItemRequest()
	                              .WithTableName(groupTableName)
	                              .WithItem({{"ID",AV(group.id)},
	                                         {"sortKey",AV(group.id)},
	                                         {"name",AV(group.name)},
	                                         {"email",AV(group.email)},
	                                         {"phone",AV(group.phone)},
	                                         {"scienceField",AV(group.scienceField)},
	                                         {"description",AV(group.description)}
	                              }));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to add Group record: " << err.GetMessage());
		return false;
	}
	
	//update caches
	CacheRecord<Group> record(group,groupCacheValidity);
	replaceCacheRecord(groupCache,group.id,record);
	replaceCacheRecord(groupByNameCache,group.name,record);
        
	return true;
}

bool PersistentStore::removeGroup(const std::string& groupID){
	using Aws::DynamoDB::Model::AttributeValue;
	
	//delete all memberships in the group
	for(auto uID : getMembersOfGroup(groupID)){
		if(!removeUserFromGroup(uID,groupID))
			return false;
	}
	
	//erase cache entries
	{
		//Somewhat hacky: we can't erase the secondary cache entries unless we know 
		//the keys. However, we keep the caches synchronized, so if there is 
		//such an entry to delete there is also an entry in the main cache, so 
		//we can grab that to get the name without having to read from the 
		//database.
		CacheRecord<Group> record;
		bool cached=groupCache.find(groupID,record);
		if(cached){
			//don't particularly care whether the record is expired; if it is 
			//all that will happen is that we will delete the equally stale 
			//record in the other cache
			groupByNameCache.erase(record.record.name);
		}
		groupCache.erase(groupID);
	}
	
	//delete the Group record itself
	auto outcome=dbClient.DeleteItem(Aws::DynamoDB::Model::DeleteItemRequest()
								     .WithTableName(groupTableName)
								     .WithKey({{"ID",AttributeValue(groupID)},
	                                           {"sortKey",AttributeValue(groupID)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to delete Group record: " << err.GetMessage());
		return false;
	}
	return true;
}

bool PersistentStore::updateGroup(const Group& group){
	using AV=Aws::DynamoDB::Model::AttributeValue;
	using AVU=Aws::DynamoDB::Model::AttributeValueUpdate;
	auto outcome=dbClient.UpdateItem(Aws::DynamoDB::Model::UpdateItemRequest()
	                                 .WithTableName(groupTableName)
	                                 .WithKey({{"ID",AV(group.id)},
	                                           {"sortKey",AV(group.id)}})
	                                 .WithAttributeUpdates({
	                                            {"name",AVU().WithValue(AV(group.name))},
	                                            {"email",AVU().WithValue(AV(group.email))},
	                                            {"phone",AVU().WithValue(AV(group.phone))},
	                                            {"scienceField",AVU().WithValue(AV(group.scienceField))},
	                                            {"description",AVU().WithValue(AV(group.description))},
	                                            })
	                                 );
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to update Group record: " << err.GetMessage());
		return false;
	}
	
	//update caches
	CacheRecord<Group> record(group,groupCacheValidity);
	replaceCacheRecord(groupCache,group.id,record);
	replaceCacheRecord(groupByNameCache,group.name,record);
	//in principle we should update the groupByUserCache here, but we don't know 
	//which users are the keys. However, that cache is used only for Group properties 
	//which cannot be changed (ID, name), so failing to update it does not do any harm. 
	
	return true;
}

std::vector<std::string> PersistentStore::getMembersOfGroup(const std::string groupID){
	using Aws::DynamoDB::Model::AttributeValue;
	databaseQueries++;
	log_info("Querying database for members of Group " << groupID);
	auto outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
	                            .WithTableName(userTableName)
	                            .WithIndexName("ByGroup")
	                            .WithKeyConditionExpression("#groupID = :id_val")
	                            .WithExpressionAttributeNames({{"#groupID","groupID"}})
								.WithExpressionAttributeValues({{":id_val",AttributeValue(groupID)}})
	                            );
	std::vector<std::string> users;
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch Group membership records: " << err.GetMessage());
		return users;
	}
	const auto& queryResult=outcome.GetResult();
	users.reserve(queryResult.GetCount());
	for(const auto& item : queryResult.GetItems())
		users.push_back(item.find("ID")->second.GetS());
	
	return users;
}

std::vector<std::string> PersistentStore::clustersOwnedByGroup(const std::string groupID){
	using Aws::DynamoDB::Model::AttributeValue;
	databaseQueries++;
	log_info("Querying database for clusters owned by Group " << groupID);
	auto outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
	                            .WithTableName(clusterTableName)
	                            .WithIndexName("ByGroup")
	                            .WithKeyConditionExpression("#groupID = :id_val")
	                            .WithExpressionAttributeNames({{"#groupID","owningGroup"}})
								.WithExpressionAttributeValues({{":id_val",AttributeValue(groupID)}})
	                            );
	std::vector<std::string> clusters;
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch Group owned cluster records: " << err.GetMessage());
		return clusters;
	}
	const auto& queryResult=outcome.GetResult();
	clusters.reserve(queryResult.GetCount());
	for(const auto& item : queryResult.GetItems())
		clusters.push_back(item.find("ID")->second.GetS());
	
	return clusters;
}

std::vector<Group> PersistentStore::listGroups(){
	//First check if vos are cached
	std::vector<Group> collected;
	if(groupCacheExpirationTime.load() > std::chrono::steady_clock::now()){
	        auto table = groupCache.lock_table();
		for(auto itr = table.cbegin(); itr != table.cend(); itr++){
		        auto group = itr->second;
			cacheHits++;
			collected.push_back(group);
		}
	
		table.unlock();
		return collected;
	}	

	databaseScans++;
	Aws::DynamoDB::Model::ScanRequest request;
	request.SetTableName(groupTableName);
	request.SetFilterExpression("attribute_exists(#name)");
	request.SetExpressionAttributeNames({{"#name","name"}});
	bool keepGoing=false;
	
	do{
		auto outcome=dbClient.Scan(request);
		if(!outcome.IsSuccess()){
			//TODO: more principled logging or reporting of the nature of the error
			auto err=outcome.GetError();
			log_error("Failed to fetch Group records: " << err.GetMessage());
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
			Group group;
			group.valid=true;
			group.id=findOrThrow(item,"ID","Group record missing ID attribute").GetS();
			group.name=findOrThrow(item,"name","Group record missing name attribute").GetS();
			group.email=findOrDefault(item,"email",missingString).GetS();
			group.phone=findOrDefault(item,"phone",missingString).GetS();
			group.scienceField=findOrDefault(item,"scienceField",missingString).GetS();
			group.description=findOrDefault(item,"description",missingString).GetS();
			collected.push_back(group);

			CacheRecord<Group> record(group,groupCacheValidity);
			replaceCacheRecord(groupCache,group.id,record);
			replaceCacheRecord(groupByNameCache,group.name,record);
		}
	}while(keepGoing);
	groupCacheExpirationTime=std::chrono::steady_clock::now()+groupCacheValidity;
	
	return collected;
}

std::vector<Group> PersistentStore::listGroupsForUser(const std::string& user){
	// first check if groups list is cached
	maybeReturnCachedCategoryMembers(groupByUserCache,user);

	std::vector<Group> vos;
	using AV=Aws::DynamoDB::Model::AttributeValue;
	databaseQueries++;

	Aws::DynamoDB::Model::QueryOutcome outcome;
	outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
			       .WithTableName(userTableName)
			       .WithKeyConditionExpression("ID = :user_val")
			       .WithFilterExpression("attribute_exists(#groupID)")
			       .WithExpressionAttributeValues({{":user_val", AV(user)}})
			       .WithExpressionAttributeNames({{"#groupID", "groupID"}})
			       );

	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to list groups by user: " << err.GetMessage());
		return vos;
	}

	const auto& queryResult=outcome.GetResult();
	if(queryResult.GetCount()==0)
		return vos;

	for(const auto& item : queryResult.GetItems()){
		std::string groupID = findOrThrow(item, "groupID", "User record missing groupID attribute").GetS();
		
	  	Group group = findGroupByID(groupID);
		vos.push_back(group);
		
		//update caches
		CacheRecord<Group> record(group,groupCacheValidity);
		replaceCacheRecord(groupCache,group.id,record);
		groupByNameCache.insert_or_assign(group.name,record);
		groupByUserCache.insert_or_assign(user,record);
	}
	groupByUserCache.update_expiration(user,std::chrono::steady_clock::now()+groupCacheValidity);
	
	return vos;
}

Group PersistentStore::findGroupByID(const std::string& id){
	//first see if we have this cached
	{
		CacheRecord<Group> record;
		if(groupCache.find(id,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return record;
			}
		}
	}
	//need to query the database
	databaseQueries++;
	log_info("Querying database for Group " << id);
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.GetItem(Aws::DynamoDB::Model::GetItemRequest()
	                              .WithTableName(groupTableName)
	                              .WithKey({{"ID",AttributeValue(id)},
	                                        {"sortKey",AttributeValue(id)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch Group record: " << err.GetMessage());
		return Group();
	}
	const auto& item=outcome.GetResult().GetItem();
	if(item.empty()) //no match found
		return Group{};
	Group group;
	group.valid=true;
	group.id=id;
	group.name=findOrThrow(item,"name","Group record missing name attribute").GetS();
	group.email=findOrDefault(item,"email",missingString).GetS();
	group.phone=findOrDefault(item,"phone",missingString).GetS();
	group.scienceField=findOrDefault(item,"scienceField",missingString).GetS();
	group.description=findOrDefault(item,"description",missingString).GetS();
	
	//update caches
	CacheRecord<Group> record(group,groupCacheValidity);
	replaceCacheRecord(groupCache,group.id,record);
	replaceCacheRecord(groupByNameCache,group.name,record);
	
	return group;
}

Group PersistentStore::findGroupByName(const std::string& name){
	//first see if we have this cached
	{
		CacheRecord<Group> record;
		if(groupByNameCache.find(name,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return record;
			}
		}
	}
	//need to query the database
	databaseQueries++;
	log_info("Querying database for Group " << name);
	using AV=Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
	                            .WithTableName(groupTableName)
	                            .WithIndexName("ByName")
	                            .WithKeyConditionExpression("#name = :name_val")
	                            .WithExpressionAttributeNames({{"#name","name"}})
	                            .WithExpressionAttributeValues({{":name_val",AV(name)}})
	                            );
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to look up Group by name: " << err.GetMessage());
		return Group();
	}
	const auto& queryResult=outcome.GetResult();
	if(queryResult.GetCount()==0)
		return Group();
	if(queryResult.GetCount()>1)
		log_fatal("Group name \"" << name << "\" is not unique!");
	
	const auto& item=queryResult.GetItems().front();
	Group group;
	group.valid=true;
	group.id=findOrThrow(item,"ID","Group record missing ID attribute").GetS();
	group.name=name;
	group.email=findOrDefault(item,"email",missingString).GetS();
	group.phone=findOrDefault(item,"phone",missingString).GetS();
	group.scienceField=findOrDefault(item,"scienceField",missingString).GetS();
	group.description=findOrDefault(item,"description",missingString).GetS();
	
	//update caches
	CacheRecord<Group> record(group,groupCacheValidity);
	replaceCacheRecord(groupCache,group.id,record);
	replaceCacheRecord(groupByNameCache,group.name,record);
	
	return group;
}

Group PersistentStore::getGroup(const std::string& idOrName){
	if(idOrName.find(IDGenerator::groupIDPrefix)==0)
		return findGroupByID(idOrName);
	return findGroupByName(idOrName);
}

//----

SharedFileHandle PersistentStore::configPathForCluster(const std::string& cID){
	if(!findClusterByID(cID)) //need to do this to ensure local data is fresh
		log_fatal(cID << " does not exist; cannot get config data");
	return clusterConfigs.find(cID);
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
		{"systemNamespace",AttributeValue(cluster.systemNamespace)},
		{"owningGroup",AttributeValue(cluster.owningGroup)},
		{"owningOrganization",AttributeValue(cluster.owningOrganization)},
		{"monCredential",AttributeValue(cluster.monitoringCredential.serialize())},
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to add cluster record: " << err.GetMessage());
		return false;
	}
	
	CacheRecord<Cluster> record(cluster,clusterCacheValidity);
	replaceCacheRecord(clusterCache,cluster.id,record);
	replaceCacheRecord(clusterByNameCache,cluster.name,record);
	clusterByGroupCache.insert_or_assign(cluster.owningGroup,record);
	writeClusterConfigToDisk(cluster);
	
	return true;
}

void PersistentStore::writeClusterConfigToDisk(const Cluster& cluster){
	FileHandle file=makeTemporaryFile(clusterConfigDir+"/"+cluster.id+"_v");
	std::ofstream confFile(file.path());
	if(!confFile)
		log_fatal("Unable to open " << file.path() << " for writing");
	confFile << cluster.config;
	if(confFile.fail())
		log_fatal("Unable to write cluster config to " << file.path());
	
	replaceCacheRecord(clusterConfigs,cluster.id,std::make_shared<FileHandle>(std::move(file)));
}

Cluster PersistentStore::findClusterByID(const std::string& cID){
	//first see if we have this cached
	{
		CacheRecord<Cluster> record;
		if(clusterCache.find(cID,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return record;
			}
		}
	}
	//need to query the database
	using Aws::DynamoDB::Model::AttributeValue;
	databaseQueries++;
	log_info("Querying database for cluster " << cID);
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
	cluster.owningGroup=findOrThrow(item,"owningGroup","Cluster record missing owningGroup attribute").GetS();
	cluster.config=findOrThrow(item,"config","Cluster record missing config attribute").GetS();
	cluster.systemNamespace=findOrThrow(item,"systemNamespace","Cluster record missing systemNamespace attribute").GetS();
	cluster.owningOrganization=findOrDefault(item,"owningOrganization",missingString).GetS();
	cluster.monitoringCredential=S3Credential::deserialize(findOrDefault(item,"monCredential",missingString).GetS());
	
	//cache this result for reuse
	CacheRecord<Cluster> record(cluster,clusterCacheValidity);
	replaceCacheRecord(clusterCache,cluster.id,record);
	clusterByNameCache.insert_or_assign(cluster.name,record);
	clusterByGroupCache.insert_or_assign(cluster.owningGroup,record);
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
				cacheHits++;
				return record;
			}
		}
	}
	//need to query the database
	using AV=Aws::DynamoDB::Model::AttributeValue;
	databaseQueries++;
	log_info("Querying database for cluster " << name);
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
	const auto& item=queryResult.GetItems().front();
	cluster.owningGroup=findOrThrow(item,"owningGroup",
	                             "Cluster record missing owningGroup attribute").GetS();
	cluster.config=findOrThrow(item,"config",
	                           "Cluster record missing config attribute").GetS();
	cluster.systemNamespace=findOrThrow(item,"systemNamespace",
	                                    "Cluster record missing systemNamespace attribute").GetS();
	cluster.owningOrganization=findOrDefault(item,"owningOrganization",missingString).GetS();
	cluster.monitoringCredential=S3Credential::deserialize(findOrDefault(item,"monCredential",missingString).GetS());
	
	//cache this result for reuse
	CacheRecord<Cluster> record(cluster,clusterCacheValidity);
	replaceCacheRecord(clusterCache,cluster.id,record);
	clusterByNameCache.insert_or_assign(cluster.name,record);
	clusterByGroupCache.insert_or_assign(cluster.owningGroup,record);
	writeClusterConfigToDisk(cluster);
	
	return cluster;
}

Cluster PersistentStore::getCluster(const std::string& idOrName){
	if(idOrName.find(IDGenerator::clusterIDPrefix)==0)
		return findClusterByID(idOrName);
	return findClusterByName(idOrName);
}

bool PersistentStore::removeCluster(const std::string& cID){
	//remove all records of groups granted access to the cluster
	for(const auto& guest : listGroupsAllowedOnCluster(cID))
		removeGroupFromCluster(guest,cID);
	
	//erase cache entries
	{
		//Somewhat hacky: we can't erase the byName cache entry unless we know 
		//the name. However, we keep the caches synchronized, so if there is 
		//such an entry to delete there is also an entry in the main cache, so 
		//we can grab that to get the name without having to read from the 
		//database.
		CacheRecord<Cluster> record;
		bool cached=clusterCache.find(cID,record);
		if(cached){
			//don't particularly care whether the record is expired; if it is 
			//all that will happen is that we will delete the equally stale 
			//record in the other cache
			clusterByNameCache.erase(record.record.name);
			clusterByGroupCache.erase(record.record.owningGroup,record);
		}
	}
	clusterCache.erase(cID);
	clusterConfigs.erase(cID);
	clusterLocationCache.erase(cID);
	
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
	outcome=dbClient.DeleteItem(Aws::DynamoDB::Model::DeleteItemRequest()
								.WithTableName(clusterTableName)
								.WithKey({{"ID",AttributeValue(cID)},
	                                      {"sortKey",AttributeValue(cID+":Locations")}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to delete cluster location record: " << err.GetMessage());
		return false;
	}
	return true;
}

bool PersistentStore::updateCluster(const Cluster& cluster){
	using AV=Aws::DynamoDB::Model::AttributeValue;
	using AVU=Aws::DynamoDB::Model::AttributeValueUpdate;
	auto outcome=dbClient.UpdateItem(Aws::DynamoDB::Model::UpdateItemRequest()
	                                 .WithTableName(clusterTableName)
	                                 .WithKey({{"ID",AV(cluster.id)},
	                                           {"sortKey",AV(cluster.id)}})
	                                 .WithAttributeUpdates({
	                                            {"name",AVU().WithValue(AV(cluster.name))},
	                                            {"config",AVU().WithValue(AV(cluster.config))},
	                                            {"systemNamespace",AVU().WithValue(AV(cluster.systemNamespace))},
	                                            {"owningGroup",AVU().WithValue(AV(cluster.owningGroup))},
	                                            {"owningOrganization",AVU().WithValue(AV(cluster.owningOrganization))},
	                                            {"monCredential",AVU().WithValue(AV(cluster.monitoringCredential.serialize()))}})
	                                 );
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to update cluster record: " << err.GetMessage());
		return false;
	}
	
	//update caches
	CacheRecord<Cluster> record(cluster,clusterCacheValidity);
	replaceCacheRecord(clusterCache,cluster.id,record);
	clusterByNameCache.insert_or_assign(cluster.name,record);
	clusterByGroupCache.insert_or_assign(cluster.owningGroup,record);
	writeClusterConfigToDisk(cluster);
	
	return true;
}

std::vector<Cluster> PersistentStore::listClusters(){
	std::vector<Cluster> collected;

	// first check if clusters are cached
	if(clusterCacheExpirationTime.load() > std::chrono::steady_clock::now()){
		auto table = clusterCache.lock_table();
		for(auto itr = table.cbegin(); itr != table.cend(); itr++){
			auto cluster = itr->second;
			cacheHits++;
			collected.push_back(cluster);
		 }
		
		table.unlock();
		return collected;
	}

	databaseScans++;
	Aws::DynamoDB::Model::ScanRequest request;
	request.SetTableName(clusterTableName);
	request.SetFilterExpression("attribute_not_exists(#groupID) AND attribute_exists(#name)");
	request.SetExpressionAttributeNames({{"#groupID", "groupID"},{"#name","name"}});
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
			cluster.id=findOrThrow(item,"ID","Cluster record missing ID attribute").GetS();
			cluster.name=findOrThrow(item,"name","Cluster record missing name attribute").GetS();
			cluster.owningGroup=findOrThrow(item,"owningGroup","Cluster record missing owningGroup attribute").GetS();
			cluster.config=findOrThrow(item,"config","Cluster record missing config attribute").GetS();
			cluster.systemNamespace=findOrThrow(item,"systemNamespace","Cluster record missing systemNamespace attribute").GetS();
			cluster.owningOrganization=findOrDefault(item,"owningOrganization",missingString).GetS();
			cluster.monitoringCredential=S3Credential::deserialize(findOrDefault(item,"monCredential",missingString).GetS());
			collected.push_back(cluster);
			
			CacheRecord<Cluster> record(cluster,clusterCacheValidity);
			replaceCacheRecord(clusterCache,cluster.id,record);
			clusterByNameCache.insert_or_assign(cluster.name,record);
			clusterByGroupCache.insert_or_assign(cluster.owningGroup,record);
			writeClusterConfigToDisk(cluster);
		}
	}while(keepGoing);
	clusterCacheExpirationTime=std::chrono::steady_clock::now()+clusterCacheValidity;
	
	return collected;
}

std::vector<Cluster> PersistentStore::listClustersByGroup(std::string group){
	std::vector<Cluster> collected;

	//check whether the Group 'ID' we got was actually a name
	if(!group.empty() && group.find(IDGenerator::groupIDPrefix)!=0){
		//if a name, find the corresponding group
		Group group_=findGroupByName(group);
		//if no such Group exists it does not have clusters associated with it
		if(!group_)
			return collected;
		//otherwise, get the actual Group ID and continue with the operation
		group=group_.id;
	}

	std::vector<Cluster> allClusters=listClusters();
	for (auto cluster : allClusters) {
		if (group == cluster.owningGroup || groupAllowedOnCluster(group, cluster.id))
			collected.push_back(cluster);
	}
			
	return collected;
}

bool PersistentStore::addGroupToCluster(std::string groupID, std::string cID){
	//check whether the Group 'ID' we got was actually a name
	if(!normalizeGroupID(groupID,true))
		return false;
	//check whether the cluster 'ID' we got was actually a name
	if(!normalizeClusterID(cID))
		return false;
	
	//remove any negative cache entry
	//HACK: a special record with a trailing dash is used to indicate that the 
	//group is known _not_ to have access
	clusterGroupAccessCache.erase(cID,CacheRecord<std::string>(groupID+"-"));
	
	using Aws::DynamoDB::Model::AttributeValue;
	auto request=Aws::DynamoDB::Model::PutItemRequest()
	.WithTableName(clusterTableName)
	.WithItem({
		{"ID",AttributeValue(cID)},
		{"sortKey",AttributeValue(cID+":"+groupID)},
		{"groupID",AttributeValue(groupID)}
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to add Group cluster access record: " << err.GetMessage());
		return false;
	}
	
	//update cache
	CacheRecord<std::string> record(groupID,clusterCacheValidity);
	clusterGroupAccessCache.insert_or_assign(cID,record);
	
	return true;
}

bool PersistentStore::removeGroupFromCluster(std::string groupID, std::string cID){
	//check whether the Group 'ID' we got was actually a name
	if(!normalizeGroupID(groupID,true))
		return false;
	//check whether the cluster 'ID' we got was actually a name
	if(!normalizeClusterID(cID))
		return false;
	
	//remove any cache entry
	clusterGroupAccessCache.erase(cID,CacheRecord<std::string>(groupID));
	
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.DeleteItem(Aws::DynamoDB::Model::DeleteItemRequest()
	                                 .WithTableName(clusterTableName)
	                                 .WithKey({{"ID",AttributeValue(cID)},
	                                           {"sortKey",AttributeValue(cID+":"+groupID)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to delete Group cluster access record: " << err.GetMessage());
		return false;
	}
	
	//Make a cache entry
	//HACK: group names (and IDs) may not end with a dash, so we use such a 
	//record to indicate that a group is known _not_ to have access
	CacheRecord<std::string> record(groupID+"-",clusterCacheValidity);
	clusterGroupAccessCache.insert_or_assign(cID,record);
	
	return true;
}

std::vector<std::string> PersistentStore::listGroupsAllowedOnCluster(std::string cID, bool useNames){
	//check whether the cluster 'ID' we got was actually a name
	if(!normalizeClusterID(cID))
		return {}; //A nonexistent cluster cannot have any allowed groups
	
	//check for a wildcard record
	if(clusterAllowsAllGroups(cID)){
		if(useNames)
			return {wildcardName};
		return {wildcard};
	}
	
	using Aws::DynamoDB::Model::AttributeValue;
	databaseQueries++;
	log_info("Querying database for groups allowed on cluster " << cID);
	auto request=Aws::DynamoDB::Model::QueryRequest()
	.WithTableName(clusterTableName)
	.WithKeyConditionExpression("#id = :id AND begins_with(#sortKey,:prefix)")
	.WithExpressionAttributeNames({
		{"#id","ID"},
		{"#sortKey","sortKey"}
	})
	.WithExpressionAttributeValues({
		{":id",AttributeValue(cID)},
		{":prefix",AttributeValue(cID+":"+IDGenerator::groupIDPrefix)}
	});
	auto outcome=dbClient.Query(request);
	std::vector<std::string> vos;
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch cluster's Group whitelist records: " << err.GetMessage());
		return vos;
	}
	
	const auto& queryResult=outcome.GetResult();
	for(const auto& item : queryResult.GetItems()){
		if(item.count("groupID"))
			vos.push_back(item.find("groupID")->second.GetS());
	}
	
	if(useNames){
		//do extra lookups to replace IDs with nicer names
		for(std::string& groupStr : vos){
			Group group=findGroupByID(groupStr);
			groupStr=group.name;
		}
	}
	
	return vos;
}

bool PersistentStore::groupAllowedOnCluster(std::string groupID, std::string cID){
	//TODO: possible issue: We only store memberships, so repeated queries about
	//a Group's access to a cluster to which it does not have access belong will
	//never be in the cache, and will always incur a database query. This should
	//not be a problem for normal/well intentioned use, but seems like a way to
	//turn accident or malice into denial of service or a large AWS bill. 
	
	//check whether the 'ID' we got was actually a name
	if(!normalizeGroupID(groupID))
		return false;
	if(!normalizeClusterID(cID))
		return false;
	
	//before checking for the specific Group, see if a wildcard record exists
	if(clusterAllowsAllGroups(cID))
		return true;
	
	//if no wildcard, look for the specific cluster
	//first see if we have this cached
	{
		CacheRecord<std::string> record(groupID);
		if(clusterGroupAccessCache.find(cID,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return true;
			}
		}
	}
	{ //HACK: look for a special record with a trailing dash to indicate that 
		//the group is known _not_ to have access
		CacheRecord<std::string> record(groupID+"-");
		if(clusterGroupAccessCache.find(cID,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return false;
			}
		}
	}
	//need to query the database
	databaseQueries++;
	log_info("Querying database for Group " << groupID << " access to cluster " << cID);
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.GetItem(Aws::DynamoDB::Model::GetItemRequest()
								  .WithTableName(clusterTableName)
								  .WithKey({{"ID",AttributeValue(cID)},
	                                        {"sortKey",AttributeValue(cID+":"+groupID)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch cluster Group access record: " << err.GetMessage());
		return false;
	}
	const auto& item=outcome.GetResult().GetItem();
	if(item.empty()){ //no match found
		//update cache
		//HACK: group names (and IDs) may not end with a dash, so we use such a 
		//record to indicate that a group is known _not_ to have access
		CacheRecord<std::string> record(groupID+"-",clusterCacheValidity);
		clusterGroupAccessCache.insert_or_assign(cID,record);
		return false;
	}
	
	//update cache
	CacheRecord<std::string> record(groupID,clusterCacheValidity);
	clusterGroupAccessCache.insert_or_assign(cID,record);
	
	return true;
}

bool PersistentStore::clusterAllowsAllGroups(std::string cID){
	{ //check cache first
		CacheRecord<std::string> record(wildcard);
		if(clusterGroupAccessCache.find(cID,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return record;
			}
		}
	}
	{ //HACK: look for a special record with a trailing dash to indicate that 
		//the group is known _not_ to have access
		CacheRecord<std::string> record(wildcard+"-");
		if(clusterGroupAccessCache.find(cID,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return false;
			}
		}
	}
	//query the database
	databaseQueries++;
	log_info("Querying database for wildcard access to cluster " << cID);
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.GetItem(Aws::DynamoDB::Model::GetItemRequest()
								  .WithTableName(clusterTableName)
								  .WithKey({{"ID",AttributeValue(cID)},
	                                        {"sortKey",AttributeValue(cID+":"+wildcard)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch cluster Group access record: " << err.GetMessage());
		return false;
	}
	const auto& item=outcome.GetResult().GetItem();
	if(item.empty()){ //no match found
		//update cache
		//HACK: group names (and IDs) may not end with a dash, so we use such a 
		//record to indicate that a group is known _not_ to have access
		CacheRecord<std::string> record(wildcard+"-",clusterCacheValidity);
		clusterGroupAccessCache.insert_or_assign(cID,record);
		return false;
	}
	//update cache
	CacheRecord<std::string> record(wildcard,clusterCacheValidity);
	clusterGroupAccessCache.insert_or_assign(cID,record);
	
	return true;
}

std::set<std::string> PersistentStore::listApplicationsGroupMayUseOnCluster(std::string groupID, std::string cID){
	//check whether the Group 'ID' we got was actually a name
	if(!normalizeGroupID(groupID,true))
		return {};
	//check whether the cluster 'ID' we got was actually a name
	if(!normalizeClusterID(cID))
		return {};
	
	std::string sortKey=cID+":"+groupID+":Applications";
	
	{ //check cache first
		CacheRecord<std::set<std::string>> record;
		if(clusterGroupApplicationCache.find(sortKey,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return record;
			}
		}
	}
	//query the database
	databaseQueries++;
	log_info("Querying database for applications " << groupID << " may use on " << cID);
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.GetItem(Aws::DynamoDB::Model::GetItemRequest()
								  .WithTableName(clusterTableName)
								  .WithKey({{"ID",AttributeValue(cID)},
	                                        {"sortKey",AttributeValue(sortKey)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch Group application use record: " << err.GetMessage());
		return {};
	}
	std::set<std::string> result;
	const auto& item=outcome.GetResult().GetItem();
	if(item.empty()){ //no record found, treat this as all allowed
		log_info("Found no record of allowed applications for " << groupID << " on " << cID << ", treating as universal");
		result={wildcardName};
	}
	else{
		auto applications=findOrThrow(item,"applications","Cluster record missing applications attribute").GetSS();
		result=std::set<std::string>(applications.begin(),applications.end());
		if(result.count("<none>"))
			result={};
	}
	//update cache
	CacheRecord<std::set<std::string>> record(result,clusterCacheValidity);
	replaceCacheRecord(clusterGroupApplicationCache,sortKey,record);
	
	return result;
}

bool PersistentStore::allowVoToUseApplication(std::string groupID, std::string cID, std::string appName){
	//check whether the Group 'ID' we got was actually a name
	if(!normalizeGroupID(groupID,true))
		return false;
	//check whether the cluster 'ID' we got was actually a name
	if(!normalizeClusterID(cID))
		return false;
	if(appName==wildcard)
		appName=wildcardName;
	
	std::string sortKey=cID+":"+groupID+":Applications";
	
	//This operation requires updating the record if it already exists, so we 
	//must first fetch it.
	std::set<std::string> allowed=listApplicationsGroupMayUseOnCluster(groupID,cID);
	
	if(allowed.count(wildcardName))
		allowed={};
	
	//if granting universal permission replace the whole list; otherwise add to it
	if(appName==wildcardName)
		allowed={wildcardName};
	else
		allowed.insert(appName);
	
	using Aws::DynamoDB::Model::AttributeValue;
	AttributeValue value;
	for(const auto& application : allowed)
		value.AddSItem(application);
	auto request=Aws::DynamoDB::Model::PutItemRequest()
	.WithTableName(clusterTableName)
	.WithItem({
		{"ID",AttributeValue(cID)},
		{"sortKey",AttributeValue(sortKey)},
		{"applications",value}
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to add Group application use record: " << err.GetMessage());
		return false;
	}
	
	//update cache
	CacheRecord<std::set<std::string>> record(allowed,clusterCacheValidity);
	replaceCacheRecord(clusterGroupApplicationCache,sortKey,record);
	
	return true;
}

bool PersistentStore::denyGroupUseOfApplication(std::string groupID, std::string cID, std::string appName){
	//check whether the Group 'ID' we got was actually a name
	if(!normalizeGroupID(groupID,true)){
		log_error("Invalid Group name");
		return false;
	}
	//check whether the cluster 'ID' we got was actually a name
	if(!normalizeClusterID(cID)){
		log_error("Invalid cluster name");
		return false;
	}
	if(appName==wildcard)
		appName=wildcardName;
	
	std::string sortKey=cID+":"+groupID+":Applications";
	
	//This operation requires updating the record if it already exists, so we 
	//must first fetch it.
	std::set<std::string> allowed=listApplicationsGroupMayUseOnCluster(groupID,cID);
	
	//update the set of allowed applications, or bail out if the operation makes no sense
	if(appName==wildcardName)
		allowed={}; //revoking all permission, replace with empty set
	else if(allowed.count(wildcardName))
		return false; //removing permission for one application while all others are allowed is not supported
	else if(!allowed.count(appName))
		return false; //can't remove permission for something already forbidden
	else
		allowed.erase(appName);
	
	//store the new set in the database
	using Aws::DynamoDB::Model::AttributeValue;
	AttributeValue value;
	for(const auto& application : allowed)
		value.AddSItem(application);
	if(allowed.empty())
		value.AddSItem("<none>");
	auto request=Aws::DynamoDB::Model::PutItemRequest()
	.WithTableName(clusterTableName)
	.WithItem({
		{"ID",AttributeValue(cID)},
		{"sortKey",AttributeValue(sortKey)},
		{"applications",value}
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to remove Group application use record: " << err.GetMessage());
		return false;
	}
	
	//update cache
	CacheRecord<std::set<std::string>> record(allowed,clusterCacheValidity);
	replaceCacheRecord(clusterGroupApplicationCache,sortKey,record);
	
	return true;
}

bool PersistentStore::groupMayUseApplication(std::string groupID, std::string cID, std::string appName){
	//no need to normalize groupID/cID because listApplicationsGroupMayUseOnCluster will do it
	auto allowed=listApplicationsGroupMayUseOnCluster(groupID,cID);
	if(allowed.count(wildcardName))
		return true;
	return allowed.count(appName);
}

std::vector<GeoLocation> PersistentStore::getLocationsForCluster(std::string cID){
	//check whether the cluster 'ID' we got was actually a name
	if(!normalizeClusterID(cID)){
		log_error("Invalid cluster name");
		return {};
	}
	
	std::string sortKey=cID+":Locations";
	{ //check cache first
		CacheRecord<std::vector<GeoLocation>> record;
		if(clusterLocationCache.find(cID,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return record;
			}
		}
	}
	
	//query the database
	databaseQueries++;
	log_info("Querying database for locations associated with cluster " << cID);
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.GetItem(Aws::DynamoDB::Model::GetItemRequest()
								  .WithTableName(clusterTableName)
								  .WithKey({{"ID",AttributeValue(cID)},
	                                        {"sortKey",AttributeValue(sortKey)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch cluster location record: " << err.GetMessage());
		return {};
	}
	std::vector<GeoLocation> result;
	const auto& item=outcome.GetResult().GetItem();
	if(!item.empty()){
		const Aws::Vector<Aws::String> rawPositions=findOrThrow(item,"locations","Cluster location record missing locations attribute").GetSS();
		for(const auto& sPos : rawPositions){
			try{
				result.push_back(boost::lexical_cast<GeoLocation>(sPos));
			}
			catch(boost::bad_lexical_cast& blc){
				log_fatal("Malformatted location stored for cluster " << cID << ": " << blc.what());
			}
		}
	}
	
	//update cache
	CacheRecord<std::vector<GeoLocation>> record(result,clusterCacheValidity);
	replaceCacheRecord(clusterLocationCache,cID,record);
	
	return result;
}

bool PersistentStore::setLocationsForCluster(std::string cID, const std::vector<GeoLocation>& locations){
	//check whether the cluster 'ID' we got was actually a name
	if(!normalizeClusterID(cID)){
		log_error("Invalid cluster name");
		return {};
	}
	
	std::string sortKey=cID+":Locations";
	
	using Aws::DynamoDB::Model::AttributeValue;
	AttributeValue value;
	for(const auto& location : locations)
		value.AddSItem(boost::lexical_cast<std::string>(location));
	auto request=Aws::DynamoDB::Model::PutItemRequest()
	.WithTableName(clusterTableName)
	.WithItem({
		{"ID",AttributeValue(cID)},
		{"sortKey",AttributeValue(sortKey)},
		{"locations",value}
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to store cluster location record: " << err.GetMessage());
		return false;
	}
	
	//update cache
	CacheRecord<std::vector<GeoLocation>> record(locations,clusterCacheValidity);
	replaceCacheRecord(clusterLocationCache,cID,record);
	
	return true;
}

bool PersistentStore::setClusterMonitoringCredential(const std::string& cID, const S3Credential& cred){
	using AV=Aws::DynamoDB::Model::AttributeValue;
	using AVU=Aws::DynamoDB::Model::AttributeValueUpdate;
	auto outcome=dbClient.UpdateItem(Aws::DynamoDB::Model::UpdateItemRequest()
	                                 .WithTableName(clusterTableName)
	                                 .WithKey({{"ID",AV(cID)},
	                                           {"sortKey",AV(cID)}})
	                                 .WithUpdateExpression("SET #monCredential = :cred")
	                                 .WithConditionExpression("attribute_not_exists(#monCredential) OR #monCredential = :nocred")
	                                 .WithExpressionAttributeNames({{"#monCredential","monCredential"}})
	                                 .WithExpressionAttributeValues({{":cred",AV(cred.serialize())},
	                                                                 {":nocred",AV(S3Credential{}.serialize())}})
	                                 );
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to set monitoring credential for cluster: " << err.GetMessage());
		return false;
	}
	
	//wipe out cache entry and force a load to update it
	clusterCache.erase(cID);
	findClusterByID(cID);
	
	return true;
}

bool PersistentStore::removeClusterMonitoringCredential(const std::string& cID){
	using AV=Aws::DynamoDB::Model::AttributeValue;
	using AVU=Aws::DynamoDB::Model::AttributeValueUpdate;
	auto outcome=dbClient.UpdateItem(Aws::DynamoDB::Model::UpdateItemRequest()
	                                 .WithTableName(clusterTableName)
	                                 .WithKey({{"ID",AV(cID)},
	                                           {"sortKey",AV(cID)}})
	                                 .WithUpdateExpression("SET #monCredential = :nocred")
	                                 .WithConditionExpression("attribute_exists(#monCredential)")
	                                 .WithExpressionAttributeNames({{"#monCredential","monCredential"}})
	                                 .WithExpressionAttributeValues({{":nocred",AV(S3Credential{}.serialize())}})
	                                 );
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to remove monitoring credential for cluster: " << err.GetMessage());
		return false;
	}
	
	//wipe out cache entry and force a load to update it
	clusterCache.erase(cID);
	findClusterByID(cID);
	
	return true;
}

Cluster PersistentStore::findClusterUsingCredential(const S3Credential& cred){
	databaseScans++;
	using AV=Aws::DynamoDB::Model::AttributeValue;
	using AVU=Aws::DynamoDB::Model::AttributeValueUpdate;
	auto outcome=dbClient.Scan(Aws::DynamoDB::Model::ScanRequest()
								.WithTableName(clusterTableName)
								.WithFilterExpression("#monCredential = :cred")
								.WithExpressionAttributeNames({{"#monCredential","monCredential"}})
								.WithExpressionAttributeValues({{":cred",AV(cred.serialize())}})
								);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to scan clusters: " << err.GetMessage());
		return Cluster{};
	}
	
	if(outcome.GetResult().GetItems().empty()) //no match found
		return Cluster{};
	const auto& item=outcome.GetResult().GetItems().front();
	
	Cluster cluster;
	cluster.valid=true;
	cluster.id=findOrThrow(item,"ID","Cluster record missing ID attribute").GetS();
	cluster.name=findOrThrow(item,"name","Cluster record missing name attribute").GetS();
	cluster.owningGroup=findOrThrow(item,"owningGroup","Cluster record missing owningGroup attribute").GetS();
	cluster.config=findOrThrow(item,"config","Cluster record missing config attribute").GetS();
	cluster.systemNamespace=findOrThrow(item,"systemNamespace","Cluster record missing systemNamespace attribute").GetS();
	cluster.owningOrganization=findOrDefault(item,"owningOrganization",missingString).GetS();
	cluster.monitoringCredential=S3Credential::deserialize(findOrDefault(item,"monCredential",missingString).GetS());
	
	//cache this result for reuse
	CacheRecord<Cluster> record(cluster,clusterCacheValidity);
	replaceCacheRecord(clusterCache,cluster.id,record);
	clusterByNameCache.insert_or_assign(cluster.name,record);
	clusterByGroupCache.insert_or_assign(cluster.owningGroup,record);
	writeClusterConfigToDisk(cluster);

	return cluster;
}

CacheRecord<bool> PersistentStore::getCachedClusterReachability(std::string cID){
	//check whether the cluster 'ID' we got was actually a name
	if(!normalizeClusterID(cID)){
		log_error("Invalid cluster name");
		return {};
	}
	CacheRecord<bool> record;
	clusterConnectivityCache.find(cID,record);
	//if(record)
	//	log_info("Found valid cluster reachability record in cache for " << cID);
	return record;
}

void PersistentStore::cacheClusterReachability(std::string cID, bool reachable){
	//check whether the cluster 'ID' we got was actually a name
	if(!normalizeClusterID(cID)){
		log_error("Invalid cluster name");
		return;
	}
	CacheRecord<bool> record(reachable,clusterCacheValidity);
	replaceCacheRecord(clusterConnectivityCache,cID,record);
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
		{"owningGroup",AttributeValue(inst.owningGroup)},
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
	
	//update caches
	CacheRecord<ApplicationInstance> record(inst,instanceCacheValidity);
	replaceCacheRecord(instanceCache,inst.id,record);
	instanceByGroupCache.insert_or_assign(inst.owningGroup,record);
	instanceByNameCache.insert_or_assign(inst.name,record);
	instanceByClusterCache.insert_or_assign(inst.cluster,record);
	instanceByGroupAndClusterCache.insert_or_assign(inst.owningGroup+":"+inst.cluster,record);
	instanceConfigCache.insert(inst.id,inst.config,instanceCacheValidity);
	
	return true;
}

bool PersistentStore::removeApplicationInstance(const std::string& id){
	//erase cache entries
	{
		//Somewhat hacky: we can't erase the secondary cache entries unless we know 
		//the keys. However, we keep the caches synchronized, so if there is 
		//such an entry to delete there is also an entry in the main cache, so 
		//we can grab that to get the name without having to read from the 
		//database.
		CacheRecord<ApplicationInstance> record;
		bool cached=instanceCache.find(id,record);
		if(cached){
			//don't particularly care whether the record is expired; if it is 
			//all that will happen is that we will delete the equally stale 
			//record in the other cache
			instanceByGroupCache.erase(record.record.owningGroup,record);
			instanceByNameCache.erase(record.record.name,record);
			instanceByClusterCache.erase(record.record.cluster,record);
			instanceByGroupAndClusterCache.erase(record.record.owningGroup+":"+record.record.cluster,record);
		}
		instanceCache.erase(id);
		instanceConfigCache.erase(id);
	}
	
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
	//first see if we have this cached
	{
		CacheRecord<ApplicationInstance> record;
		if(instanceCache.find(id,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return record;
			}
		}
	}
	//need to query the database
	databaseQueries++;
	log_info("Querying database for instance " << id);
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
	inst.owningGroup=findOrThrow(item,"owningGroup","Instance record missing owningGroup attribute").GetS();
	inst.cluster=findOrThrow(item,"cluster","Instance record missing cluster attribute").GetS();
	inst.ctime=findOrThrow(item,"ctime","Instance record missing ctime attribute").GetS();
	
	//update caches
	CacheRecord<ApplicationInstance> record(inst,instanceCacheValidity);
	replaceCacheRecord(instanceCache,inst.id,record);
	instanceByGroupCache.insert_or_assign(inst.owningGroup,record);
	instanceByNameCache.insert_or_assign(inst.name,record);
	instanceByClusterCache.insert_or_assign(inst.cluster,record);
	instanceByGroupAndClusterCache.insert_or_assign(inst.owningGroup+":"+inst.cluster,record);
	return inst;
}

std::string PersistentStore::getApplicationInstanceConfig(const std::string& id){
	//first see if we have this cached
	{
		CacheRecord<std::string> record;
		if(instanceConfigCache.find(id,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return record;
			}
		}
	}
	//need to query the database
	databaseQueries++;
	log_info("Querying database for instance " << id << " config");
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
	std::string config= findOrThrow(item,"config","Instance config record missing config attribute").GetS();
	
	//update cache
	CacheRecord<std::string> record(config,instanceCacheValidity);
	replaceCacheRecord(instanceConfigCache,id,record);
	
	return config;
}

std::vector<ApplicationInstance> PersistentStore::listApplicationInstances(){
	//First check if instances are cached
	std::vector<ApplicationInstance> collected;
	if(instanceCacheExpirationTime.load() > std::chrono::steady_clock::now()){
		auto table = instanceCache.lock_table();
		for(auto itr = table.cbegin(); itr != table.cend(); itr++){
			auto instance = itr->second;
			cacheHits++;
			collected.push_back(instance);
		 }
		
		table.unlock();
		return collected;
	}

	databaseScans++;
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
			inst.owningGroup=findOrThrow(item,"owningGroup","Instance record missing ID attribute").GetS();
			inst.cluster=findOrThrow(item,"cluster","Instance record missing ID attribute").GetS();
			inst.ctime=findOrThrow(item,"ctime","Instance record missing ID attribute").GetS();
			collected.push_back(inst);

			CacheRecord<ApplicationInstance> record(inst,instanceCacheValidity);
			replaceCacheRecord(instanceCache,inst.id,record);
			instanceByNameCache.insert_or_assign(inst.name,record);
			instanceByGroupCache.insert_or_assign(inst.owningGroup,record);
			instanceByClusterCache.insert_or_assign(inst.cluster,record);
			instanceByGroupAndClusterCache.insert_or_assign(inst.owningGroup+":"+inst.cluster,record);
		}
	}while(keepGoing);
	instanceCacheExpirationTime=std::chrono::steady_clock::now()+instanceCacheValidity;
	
	return collected;
}

std::vector<ApplicationInstance> PersistentStore::listApplicationInstancesByClusterOrGroup(std::string group, std::string cluster){
	std::vector<ApplicationInstance> instances;
	
	//check whether the Group 'ID' we got was actually a name
	if(!group.empty() && !normalizeGroupID(group))
		return instances; //a nonexistent Group cannot have any running instances
	//check whether the cluster 'ID' we got was actually a name
	if(!cluster.empty() && !normalizeClusterID(cluster))
		return instances; //a nonexistent cluster cannot run any instances
	
	// First check if the instances are cached
	if (!group.empty() && !cluster.empty())
		maybeReturnCachedCategoryMembers(instanceByGroupAndClusterCache,group+":"+cluster);
	else if (!group.empty())
		maybeReturnCachedCategoryMembers(instanceByGroupCache,group);
	else if (!cluster.empty())
		maybeReturnCachedCategoryMembers(instanceByClusterCache,cluster);

	// Query if cache is not updated
	using AV=Aws::DynamoDB::Model::AttributeValue;
	databaseQueries++;
	Aws::DynamoDB::Model::QueryOutcome outcome;

	if (!group.empty() && !cluster.empty()) {
		outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
				       .WithTableName(instanceTableName)
				       .WithIndexName("ByGroup")
				       .WithKeyConditionExpression("owningGroup = :group_val")
				       .WithFilterExpression("contains(#cluster, :cluster_val)")
				       .WithExpressionAttributeNames({{"#cluster", "cluster"}})
				       .WithExpressionAttributeValues({{":group_val", AV(group)}, {":cluster_val", AV(cluster)}})
				       );
	} else if (!group.empty()) {
		outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
				       .WithTableName(instanceTableName)
				       .WithIndexName("ByGroup")
				       .WithKeyConditionExpression("owningGroup = :group_val")
				       .WithExpressionAttributeValues({{":group_val", AV(group)}})
				       );
	} else if (!cluster.empty()) {
		outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
				       .WithTableName(instanceTableName)
				       .WithIndexName("ByCluster")
				       .WithKeyConditionExpression("#cluster = :cluster_val")
				       .WithExpressionAttributeNames({{"#cluster", "cluster"}})
				       .WithExpressionAttributeValues({{":cluster_val", AV(cluster)}})
				       );
	}
	
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to list Instances by Cluster or Group: " << err.GetMessage());
		return instances;
	}

	const auto& queryResult=outcome.GetResult();
	if(queryResult.GetCount()==0)
		return instances;

	for(const auto& item : queryResult.GetItems()){
		ApplicationInstance instance;
		instance.name=findOrThrow(item,"name","Instance record missing name attribute").GetS();
		instance.id=findOrThrow(item,"ID","Instance record missing ID attribute").GetS();
		instance.application=findOrThrow(item,"application","Instance record missing application attribute").GetS();
		instance.owningGroup=findOrThrow(item, "owningGroup", "Instance record missing owning Group attribute").GetS();
		instance.cluster=findOrThrow(item,"cluster","Instance record missing cluster attribute").GetS();
		instance.ctime=findOrThrow(item,"ctime","Instance record missing ctime attribute").GetS();
		instance.valid=true;
		
		instances.push_back(instance);
		
		//update caches
		CacheRecord<ApplicationInstance> record(instance,instanceCacheValidity);
		replaceCacheRecord(instanceCache,instance.id,record);
		instanceByGroupCache.insert_or_assign(instance.owningGroup,record);
		instanceByNameCache.insert_or_assign(instance.name,record);
		instanceByClusterCache.insert_or_assign(instance.cluster,record);
		instanceByGroupAndClusterCache.insert_or_assign(instance.owningGroup+":"+instance.cluster,record);
	}
	auto expirationTime = std::chrono::steady_clock::now() + instanceCacheValidity;
	if (!group.empty() && !cluster.empty())
		instanceByGroupAndClusterCache.update_expiration(group+":"+cluster, expirationTime);
        else if (!group.empty())
		instanceByGroupCache.update_expiration(group, expirationTime);
	else if (!cluster.empty())
		instanceByClusterCache.update_expiration(cluster, expirationTime);
	
	return instances;	
}

std::vector<ApplicationInstance> PersistentStore::findInstancesByName(const std::string& name){
	//TODO: read from cache
	std::vector<ApplicationInstance> instances;
	
	using AV=Aws::DynamoDB::Model::AttributeValue;
	databaseQueries++;
	log_info("Querying database for instance with name " << name);
	auto outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
	                            .WithTableName(instanceTableName)
	                            .WithIndexName("ByName")
	                            .WithKeyConditionExpression("#name = :name_val")
	                            .WithExpressionAttributeNames({{"#name","name"}})
	                            .WithExpressionAttributeValues({{":name_val",AV(name)}})
	                            );
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to look up Instances by name: " << err.GetMessage());
		return instances;
	}
	const auto& queryResult=outcome.GetResult();
	if(queryResult.GetCount()==0)
		return instances;
	//this is allowed
	//if(queryResult.GetCount()>1)
	//	log_fatal("Cluster name \"" << name << "\" is not unique!");
	
	for(const auto& item : queryResult.GetItems()){
		ApplicationInstance instance;
		
		instance.name=name;
		instance.id=findOrThrow(item,"ID","Instance record missing ID attribute").GetS();
		instance.application=findOrThrow(item,"application","Instance record missing application attribute").GetS();
		instance.owningGroup=findOrThrow(item,"owningGroup","Instance record missing owning Group attribute").GetS();
		instance.cluster=findOrThrow(item,"cluster","Instance record missing cluster attribute").GetS();
		instance.ctime=findOrThrow(item,"ctime","Instance record missing ctime attribute").GetS();
		instance.valid=true;
		
		instances.push_back(instance);
		
		//update caches since we bothered to pull stuff directly from the DB
		CacheRecord<ApplicationInstance> record(instance,instanceCacheValidity);
		replaceCacheRecord(instanceCache,instance.id,record);
		instanceByGroupCache.insert_or_assign(instance.owningGroup,record);
		instanceByNameCache.insert_or_assign(instance.name,record);
		instanceByClusterCache.insert_or_assign(instance.cluster,record);
		instanceByGroupAndClusterCache.insert_or_assign(instance.owningGroup+":"+instance.cluster,record);
	}
	return instances;
}

std::string PersistentStore::encryptSecret(const SecretData& s) const{
	std::size_t outLen=s.dataSize+128;
	std::string result(outLen,'\0');
	int err=scryptenc_buf((const uint8_t*)s.data.get(),s.dataSize,
	                      (uint8_t*)&result.front(),
	                      (const uint8_t*)secretKey.data.get(),secretKey.dataSize,
	                      17,8,1);
	if(err)
		throw std::runtime_error("Failed to encrypt with scrypt: error " + std::to_string(err));
	return result;
}

SecretData PersistentStore::decryptSecret(const Secret& s) const{
	if(s.data.size()<128)
		throw std::runtime_error("Invalid encrypted data: too short to contain header");
	std::size_t outLen=s.data.size()-128;
	SecretData output(outLen);
	int err=scryptdec_buf((const uint8_t *)&s.data.front(),s.data.size(),
						  (uint8_t*)output.data.get(),&outLen,
						  (const uint8_t*)secretKey.data.get(),secretKey.dataSize);
	if(err)
		throw std::runtime_error("Failed to decrypt with scrypt: error " + std::to_string(err));
	return output;
}

bool PersistentStore::addSecret(const Secret& secret){
	if(secret.data.substr(0,6)!="scrypt")
		throw std::runtime_error("Secret data does not have valid encryption header");
	if(secret.data.size()<128)
		throw std::runtime_error("Secret data does not have valid encryption header");
	
	using Aws::DynamoDB::Model::AttributeValue;
	auto request=Aws::DynamoDB::Model::PutItemRequest()
	.WithTableName(secretTableName)
	.WithItem({
		{"ID",AttributeValue(secret.id)},
		{"sortKey",AttributeValue(secret.id)},
		{"name",AttributeValue(secret.name)},
		{"owningGroup",AttributeValue(secret.group)},
		{"cluster",AttributeValue(secret.cluster)},
		{"ctime",AttributeValue(secret.ctime)},
		{"contents",AttributeValue().SetB(Aws::Utils::ByteBuffer((const unsigned char*)secret.data.data(),secret.data.size()))}
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to add secret record: " << err.GetMessage());
		return false;
	}
	
	//update caches
	CacheRecord<Secret> record(secret,secretCacheValidity);
	replaceCacheRecord(secretCache,secret.id,record);
	secretByGroupCache.insert_or_assign(secret.group,record);
	secretByGroupAndClusterCache.insert_or_assign(secret.group+":"+secret.cluster,record);
	
	return true;
}

bool PersistentStore::removeSecret(const std::string& id){
	//erase cache entries
	{
		//Somewhat hacky: we can't erase the secondary cache entries unless we know 
		//the keys. However, we keep the caches synchronized, so if there is 
		//such an entry to delete there is also an entry in the main cache, so 
		//we can grab that to get the name without having to read from the 
		//database.
		CacheRecord<Secret> record;
		bool cached=secretCache.find(id,record);
		if(cached){
			//don't particularly care whether the record is expired; if it is 
			//all that will happen is that we will delete the equally stale 
			//record in the other cache
			secretByGroupCache.erase(record.record.group,record);
			secretByGroupAndClusterCache.erase(record.record.group+":"+record.record.cluster);
		}
		secretCache.erase(id);
	}
	
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.DeleteItem(Aws::DynamoDB::Model::DeleteItemRequest()
	                                      .WithTableName(secretTableName)
	                                      .WithKey({{"ID",AttributeValue(id)},
	                                                {"sortKey",AttributeValue(id)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to delete secret record: " << err.GetMessage());
		return false;
	}
	
	return true;
}

Secret PersistentStore::getSecret(const std::string& id){
	//first see if we have this cached
	{
		CacheRecord<Secret> record;
		if(secretCache.find(id,record)){
			//we have a cached record; is it still valid?
			log_info("Found record of " << id << " in cache");
			if(record){ //it is, just return it
				cacheHits++;
				return record;
			}
		}
	}
	//need to query the database
	databaseQueries++;
	log_info("Querying database for secret " << id);
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.GetItem(Aws::DynamoDB::Model::GetItemRequest()
								  .WithTableName(secretTableName)
								  .WithKey({{"ID",AttributeValue(id)},
	                                        {"sortKey",AttributeValue(id)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch secret record: " << err.GetMessage());
		return Secret();
	}
	const auto& item=outcome.GetResult().GetItem();
	if(item.empty()) //no match found
		return Secret{};
	Secret secret;
	secret.valid=true;
	secret.id=id;
	secret.name=findOrThrow(item,"name","Secret record missing name attribute").GetS();
	secret.group=findOrThrow(item,"owningGroup","Secret record missing owning group attribute").GetS();
	secret.cluster=findOrThrow(item,"cluster","Secret record missing cluster attribute").GetS();
	secret.ctime=findOrThrow(item,"ctime","Secret record missing ctime attribute").GetS();
	const auto& secret_data=findOrThrow(item,"contents","Secret record missing contents attribute").GetB();
	secret.data=std::string((const std::string::value_type*)secret_data.GetUnderlyingData(),secret_data.GetLength());
	
	//update caches
	CacheRecord<Secret> record(secret,secretCacheValidity);
	replaceCacheRecord(secretCache,secret.id,record);
	secretByGroupCache.insert_or_assign(secret.group,record);
	secretByGroupAndClusterCache.insert_or_assign(secret.group+":"+secret.cluster,record);
	
	return secret;
}

std::vector<Secret> PersistentStore::listSecrets(std::string group, std::string cluster){
	std::vector<Secret> secrets;
	
	assert((!group.empty() || !cluster.empty()) && "Either a Group or a cluster must be specified");
	
	//check whether the Group 'ID' we got was actually a name
	if(!group.empty() && !normalizeGroupID(group))
		return secrets; //a Group which does not exist cannot own any secrets
	//check whether the cluster 'ID' we got was actually a name
	if(!cluster.empty() && !normalizeClusterID(cluster))
	   return secrets; //a nonexistent cluster cannot store any secrets
	
	// First check if the secrets are cached
	if (!group.empty() && !cluster.empty())
		maybeReturnCachedCategoryMembers(secretByGroupAndClusterCache,group+":"+cluster);
	else if (!group.empty())
		maybeReturnCachedCategoryMembers(secretByGroupCache,group);
	// Listing all secrets on a cluster should be a rare case, so we do not 
	// implement caching for it.

	// Query if cache is not updated
	using AV=Aws::DynamoDB::Model::AttributeValue;
	databaseQueries++;
	
	Aws::DynamoDB::Model::QueryOutcome outcome;
	if (!group.empty()) {
		Aws::DynamoDB::Model::QueryRequest query;
		query.WithTableName(secretTableName)
		     .WithIndexName("ByGroup")
		     .WithKeyConditionExpression("owningGroup = :group_val")
		     .WithExpressionAttributeValues({{":group_val", AV(group)}});
		if (!cluster.empty()) {
			query.SetFilterExpression("contains(#cluster, :cluster_val)");
			query.AddExpressionAttributeNames("#cluster", "cluster");
			query.AddExpressionAttributeValues(":cluster_val", AV(cluster));
		}
		
		outcome=dbClient.Query(query);
	}
	else if (!cluster.empty()) {
		outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
							   .WithTableName(secretTableName)
							   .WithIndexName("ByCluster")
							   .WithKeyConditionExpression("#cluster = :cluster_val")
							   .WithExpressionAttributeNames({{"#cluster", "cluster"}})
							   .WithExpressionAttributeValues({{":cluster_val", AV(cluster)}})
							   );
	}
	
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to list secrets: " << err.GetMessage());
		return secrets;
	}

	const auto& queryResult=outcome.GetResult();

	for(const auto& item : queryResult.GetItems()){
		Secret secret;
		secret.name=findOrThrow(item,"name","Secret record missing name attribute").GetS();
		secret.id=findOrThrow(item,"ID","Secret record missing ID attribute").GetS();
		if(group.empty())
			secret.group=findOrThrow(item, "owningGroup", "Secret record missing owning group attribute").GetS();
		else
			secret.group=group;
		if(cluster.empty())
			secret.cluster=findOrThrow(item,"cluster","Secret record missing cluster attribute").GetS();
		else
			secret.cluster=cluster;
		secret.ctime=findOrThrow(item,"ctime","Secret record missing ctime attribute").GetS();
		const auto& secret_data=findOrThrow(item,"contents","Secret record missing contents attribute").GetB();
		secret.data=std::string((const std::string::value_type*)secret_data.GetUnderlyingData(),secret_data.GetLength());
		secret.valid=true;
		
		secrets.push_back(secret);
		
		//update caches
		CacheRecord<Secret> record(secret,secretCacheValidity);
		replaceCacheRecord(secretCache,secret.id,record);
		secretByGroupCache.insert_or_assign(secret.group,record);
		secretByGroupAndClusterCache.insert_or_assign(secret.group+":"+secret.cluster,record);
	}
	auto expirationTime = std::chrono::steady_clock::now() + secretCacheValidity;
	if (!cluster.empty())
		secretByGroupAndClusterCache.update_expiration(group+":"+cluster, expirationTime);
	else
		secretByGroupCache.update_expiration(group, expirationTime);
	
	return secrets;
}

Secret PersistentStore::findSecretByName(std::string group, std::string cluster, std::string name){
	auto secrets=listSecrets(group, cluster);
	for(const auto& secret : secrets){
		if(secret.name==name)
			return secret;
	}
	return Secret();
}

bool PersistentStore::addMonitoringCredential(const S3Credential& cred){
	if(!cred)
		throw std::runtime_error("Cannot store invalid S3 credentials in Dynamo");
	if(cred.inUse)
		throw std::runtime_error("Already in-use credentials should not be added");
	if(cred.revoked)
		throw std::runtime_error("Already revoked credentials should not be added");

	using Aws::DynamoDB::Model::AttributeValue;
	auto request=Aws::DynamoDB::Model::PutItemRequest()
	.WithTableName(monCredTableName)
	.WithItem({
		{"accessKey",AttributeValue(cred.accessKey)},
		{"sortKey",AttributeValue(cred.accessKey)},
		{"secretKey",AttributeValue(cred.secretKey)},
		{"inUse",AttributeValue().SetBool(false)},
		{"revoked",AttributeValue().SetBool(false)},
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to add monitoring credential record: " << err.GetMessage());
		return false;
	}
	
	//credentials should be manipulated infrequently, so we do not cache them
	
	return true;
}

S3Credential PersistentStore::getMonitoringCredential(const std::string& accessKey){
	//we do not keep these cached, always query
	databaseQueries++;
	log_info("Querying database for monitoring credential " << accessKey);
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.GetItem(Aws::DynamoDB::Model::GetItemRequest()
								  .WithTableName(monCredTableName)
								  .WithKey({{"accessKey",AttributeValue(accessKey)},
	                                        {"sortKey",AttributeValue(accessKey)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch monitoring credential record: " << err.GetMessage());
		return S3Credential();
	}
	const auto& item=outcome.GetResult().GetItem();
	if(item.empty()) //no match found
		return S3Credential{};
	S3Credential cred;
	cred.accessKey=accessKey;
	cred.secretKey=findOrThrow(item,"secretKey","Monitoring credential record missing secretKey attribute").GetS();
	cred.inUse=findOrThrow(item,"inUse","Monitoring credential record missing inUse attribute").GetBool();
	cred.revoked=findOrThrow(item,"revoked","Monitoring credential record missing revoked attribute").GetBool();
	
	//no caching
	
	return cred;
}

std::vector<S3Credential> PersistentStore::listMonitoringCredentials(){
	std::vector<S3Credential> creds;

	using AV=Aws::DynamoDB::Model::AttributeValue;
	databaseScans++;
	Aws::DynamoDB::Model::ScanRequest request;
	request.SetTableName(monCredTableName);
	bool keepGoing=false;
	
	do{
		auto outcome=dbClient.Scan(request);
		if(!outcome.IsSuccess()){
			auto err=outcome.GetError();
			log_error("Failed to fetch monitoring credential records: " << err.GetMessage());
			return creds;
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
			S3Credential cred;
			cred.accessKey=findOrThrow(item,"accessKey","Monitoring credential record missing accessKey attribute").GetS();
			cred.secretKey=findOrThrow(item,"secretKey","Monitoring credential record missing secretKey attribute").GetS();
			cred.inUse=findOrThrow(item,"inUse","Monitoring credential record missing inUse attribute").GetBool();
			cred.revoked=findOrThrow(item,"revoked","Monitoring credential record missing revoked attribute").GetBool();
			
			creds.push_back(cred);
		}
	}while(keepGoing);
	
	return creds;
}

std::tuple<S3Credential,std::string> PersistentStore::allocateMonitoringCredential(){
	S3Credential cred;
	while(true){
		//find out what credentials are available
		databaseScans++;
		using AV=Aws::DynamoDB::Model::AttributeValue;
		using AVU=Aws::DynamoDB::Model::AttributeValueUpdate;
		auto outcome=dbClient.Scan(Aws::DynamoDB::Model::ScanRequest()
		                            .WithTableName(monCredTableName)
		                            .WithFilterExpression("#inUse = :false AND #revoked = :false")
	                                .WithExpressionAttributeNames({{"#inUse","inUse"},{"#revoked","revoked"}})
	                                .WithExpressionAttributeValues({{":false",AV().SetBool(false)}})
		                            );
		if(!outcome.IsSuccess()){
			auto err=outcome.GetError();
			log_error("Failed to look up available monitoring credentials: " << err.GetMessage());
			return std::make_tuple(cred,"Failed to look up available monitoring credentials: "+err.GetMessage());
		}
		const auto& queryResult=outcome.GetResult();
		if(queryResult.GetCount()==0){
			log_error("No monitoring credentials available for allocation");
			return std::make_tuple(cred,"No monitoring credentials available for allocation");
		}
		log_info("Found " << queryResult.GetCount() << " candidate credentials for allocation");
		//try to acquire one of the candidate credentials
		for(const auto& item : queryResult.GetItems()){
			std::string accessKey=findOrThrow(item,"accessKey","Monitoring credential record missing accessKey attribute").GetS();
			
			log_info("Attempting to allocate credential " << accessKey);
			//this should atomically check that the credential is still available 
			//and then mark it as in-use
			auto outcome=dbClient.UpdateItem(Aws::DynamoDB::Model::UpdateItemRequest()
			                                 .WithTableName(monCredTableName)
			                                 .WithKey({{"accessKey",AV(accessKey)},
			                                           {"sortKey",AV(accessKey)}})
			                                 //.WithAttributeUpdates({
			                                 //           {"inUse",AVU().WithValue(AV().SetBool(true))}
			                                 //           })
			                                 .WithUpdateExpression("SET #inUse = :true")
			                                 .WithConditionExpression("#inUse = :false AND #revoked = :false")
	                                         .WithExpressionAttributeNames({{"#inUse","inUse"},{"#revoked","revoked"}})
	                                         .WithExpressionAttributeValues({{":true",AV().SetBool(true)},
	                                                                         {":false",AV().SetBool(false)}})
			                                 );
			if(!outcome.IsSuccess()){
				auto err=outcome.GetError();
				log_info("Failed to allocate credential credential: " << err.GetMessage());
				continue;
			}
			
			cred.accessKey=accessKey;
			cred.secretKey=findOrThrow(item,"secretKey","Monitoring credential record missing secretKey attribute").GetS();
			cred.inUse=true;
			cred.revoked=false;
			return std::make_tuple(cred,"");
		}
		//if we failed to acquire any of the credentials, query again in the hope that something will be available
	}
	return std::make_tuple(cred,"Internal Error"); //unreachable
}

bool PersistentStore::revokeMonitoringCredential(const std::string& accessKey){
	using AV=Aws::DynamoDB::Model::AttributeValue;
	using AVU=Aws::DynamoDB::Model::AttributeValueUpdate;
	auto outcome=dbClient.UpdateItem(Aws::DynamoDB::Model::UpdateItemRequest()
	                                 .WithTableName(monCredTableName)
	                                 .WithKey({{"accessKey",AV(accessKey)},
	                                           {"sortKey",AV(accessKey)}})
	                                 //.WithAttributeUpdates({
	                                 //           {"inUse",AVU().WithValue(AV().SetBool(false))},
	                                 //           {"revoked",AVU().WithValue(AV().SetBool(true))}
	                                 //           })
	                                 .WithUpdateExpression("SET #inUse = :false, #revoked = :true")
	                                 .WithConditionExpression("attribute_exists(#inUse)")
	                                 .WithExpressionAttributeNames({{"#inUse","inUse"},{"#revoked","revoked"}})
	                                 .WithExpressionAttributeValues({{":true",AV().SetBool(true)},
	                                                                {":false",AV().SetBool(false)}})
	                                 );
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to mark monitoring credential revoked: " << err.GetMessage());
		return false;
	}
	return true;
}

bool PersistentStore::deleteMonitoringCredential(const std::string& accessKey){
	using AV=Aws::DynamoDB::Model::AttributeValue;
	using AVU=Aws::DynamoDB::Model::AttributeValueUpdate;
	auto outcome=dbClient.DeleteItem(Aws::DynamoDB::Model::DeleteItemRequest()
	                                 .WithTableName(monCredTableName)
	                                 .WithKey({{"accessKey",AV(accessKey)},
	                                           {"sortKey",AV(accessKey)}})
	                                 .WithConditionExpression("#inUse = :false")
	                                 .WithExpressionAttributeNames({{"#inUse","inUse"}})
	                                 .WithExpressionAttributeValues({{":false",AV().SetBool(false)}})
	                                 );
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to delete monitoring credential: " << err.GetMessage());
		return false;
	}
	return true;
}

Application PersistentStore::findApplication(const std::string& repository, const std::string& appName){
	{ //check for cached data first
		log_info("Checking for application " << appName << " in cache");
		auto cached = applicationCache.find(repository);
		if(cached.second > std::chrono::steady_clock::now()){
			auto records = cached.first;
			for(const auto& record : records){
				if(record.record.name==appName && record)
					return record;
			}
		}
	}
	//Need to query helm
	log_info("Querying helm for application " << appName);
	std::string target=repository+"/"+appName;
	std::vector<std::string> searchArgs={"search",target};
	if(kubernetes::getHelmMajorVersion()==3)
		searchArgs.insert(searchArgs.begin()+1,"repo");
	auto result=runCommand("helm", searchArgs);
	if(result.status)
		log_fatal("Command failed: helm search " << target << ": [err] " << result.error << " [out] " << result.output);
	log_info("Helm output: " << result.output);
	if(result.output.find("No results found")!=std::string::npos)
		return Application();
	//Deal with the possibility of multiple results, which could happen if
	//both "slate/stuff" and "slate/superduper" existed and the user requested
	//the application "s". Multiple results might also not indicate ambiguity, 
	//if the user searches for the full name of an application, which is also a
	//prefix of the name another application which exists
	std::vector<std::string> lines = string_split_lines(result.output);
	Application app;
	//ignore initial header line printed by helm
	for(size_t i=1; i<lines.size(); i++){
		auto tokens=string_split_columns(lines[i], '\t');
		if(trim(tokens.front())==target){
			if(tokens.size()>=4)
				app=Application(appName,tokens[2],tokens[1],tokens[3]);
			else
				app=Application(appName,"unknown","unknown","");
			break;
		}
	}

	CacheRecord<Application> record(app,instanceCacheValidity);
	applicationCache.insert_or_assign(repository,record);

	return app;
}

std::vector<Application> PersistentStore::fetchApplications(const std::string& repository){
	//Tell helm the terminal is rather wide to prevent truncation of results 
	//(unless they are rather long).
	unsigned int helmMajorVersion=kubernetes::getHelmMajorVersion();
	std::vector<std::string> searchArgs={"search",repository+"/"};
	if(helmMajorVersion==2)
		searchArgs.push_back("--col-width=1024");
	else if(helmMajorVersion==3){
		searchArgs.insert(searchArgs.begin()+1,"repo");
		searchArgs.push_back("--max-col-width=1024");
	}
	auto commandResult=runCommand("helm", searchArgs);
	if(commandResult.status)
		log_fatal("helm search failed: [err] " << commandResult.error << " [out] " << commandResult.output);
	std::vector<std::string> lines = string_split_lines(commandResult.output);
	std::vector<Application> results;
	for(unsigned int n=1; n<lines.size(); n++){ //skip headers on first line
		auto tokens = string_split_columns(lines[n], '\t');
		Application app;
		app.name=tokens[0].substr(repository.size()+1); //trim leading repository name
		app.version=tokens[2];
		app.chartVersion=tokens[1];
		app.description=tokens[3];
		app.valid=true;
		results.push_back(app);
		CacheRecord<Application> record(app,instanceCacheValidity);
		applicationCache.insert_or_assign(repository,record);
	}
	auto expirationTime = std::chrono::steady_clock::now() + instanceCacheValidity;
	applicationCache.update_expiration(repository, expirationTime);
	return results;
}

std::vector<Application> PersistentStore::listApplications(const std::string& repository){
	//check for cached data first
	maybeReturnCachedCategoryMembers(applicationCache,repository);
	//No cached data, or out of date.
	return fetchApplications(repository);
}

std::string PersistentStore::getStatistics() const{
	std::ostringstream os;
	os << "Cache hits: " << cacheHits.load() << "\n";
	os << "Database queries: " << databaseQueries.load() << "\n";
	os << "Database scans: " << databaseScans.load() << "\n";
	return os.str();
}

bool PersistentStore::normalizeGroupID(std::string& groupID, bool allowWildcard){
	if(allowWildcard){
		if(groupID==wildcard)
			return true;
		if(groupID==wildcardName){
			groupID=wildcard;
			return true;
		}
	}
	if(groupID.find(IDGenerator::groupIDPrefix)!=0){
		//if a name, find the corresponding group
		Group group=findGroupByName(groupID);
		//if no such Group exists we cannot get its ID
		if(!group)
			return false;
		//otherwise, get the actual Group ID
		groupID=group.id;
	}
	return true;
}

bool PersistentStore::normalizeClusterID(std::string& cID){
	if(cID.find(IDGenerator::clusterIDPrefix)!=0){
		//if a name, find the corresponding Cluster
		Cluster cluster=findClusterByName(cID);
		//if no such cluster exists we cannot get its ID
		if(!cluster)
			return false;
		//otherwise, get the actual cluster ID
		cID=cluster.id;
	}
	return true;
}

std::string PersistentStore::dnsNameForCluster(const Cluster& cluster) const{
	return cluster.name+'.'+baseDomain;
}

const User authenticateUser(PersistentStore& store, const char* token){
	if(token==nullptr) //no token => no way of identifying a valid user
		return User{};
	return store.findUserByToken(token);
}
