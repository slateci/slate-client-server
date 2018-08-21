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

#include <aws/dynamodb/model/CreateGlobalSecondaryIndexAction.h>
#include <aws/dynamodb/model/CreateTableRequest.h>
#include <aws/dynamodb/model/DescribeTableRequest.h>
#include <aws/dynamodb/model/UpdateTableRequest.h>

#include <Logging.h>
#include <Utilities.h>
extern "C"{
	#include <scrypt/scryptenc/scryptenc.h>
}

namespace{

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
	
bool hasIndex(const Aws::DynamoDB::Model::TableDescription& tableDesc, const std::string& name){
	using namespace Aws::DynamoDB::Model;
	const Aws::Vector<GlobalSecondaryIndexDescription>& indices=tableDesc.GetGlobalSecondaryIndexes();
	return std::find_if(indices.begin(),indices.end(),
						[&name](const GlobalSecondaryIndexDescription& gsid)->bool{
							return gsid.GetIndexName()==name;
						})!=indices.end();
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
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		outcome=dbClient.DescribeTable(DescribeTableRequest()
		                               .WithTableName(tableName));
	}while(outcome.IsSuccess() && 
		   outcome.GetResult().GetTable().GetTableStatus()!=TableStatus::ACTIVE);
	if(!outcome.IsSuccess())
		log_fatal("Table " << tableName << " does not seem to be available? "
				  "Dynamo error: " << outcome.GetError().GetMessage());
}
	
} //anonymous namespace

FileHandle::~FileHandle(){
	if(!filePath.empty()){
		if(!isDirectory){ //regular file
			int err=remove(filePath.c_str());
			if(err!=0){
				err=errno;
				log_error("Failed to remove file " << filePath << " errno: " << err);
			}
		}
		else{ //directory
			int err=rmdir(filePath.c_str());
			if(err!=0){
				err=errno;
				log_error("Failed to remove directory " << filePath << " errno: " << err);
			}
		}
	}
}

std::string operator+(const char* s, const FileHandle& h){
	return s+h.path();
}
std::string operator+(const FileHandle& h, const char* s){
	return h.path()+s;
}

PersistentStore::PersistentStore(Aws::Auth::AWSCredentials credentials, 
				Aws::Client::ClientConfiguration clientConfig):
	dbClient(std::move(credentials),std::move(clientConfig)),
	userTableName("SLATE_users"),
	voTableName("SLATE_VOs"),
	clusterTableName("SLATE_clusters"),
	instanceTableName("SLATE_instances"),
	secretTableName("SLATE_secrets"),
	clusterConfigDir(createConfigTempDir()),
	userCacheValidity(std::chrono::minutes(5)),
	userCacheExpirationTime(std::chrono::steady_clock::now()),
	voCacheValidity(std::chrono::minutes(30)),
	voCacheExpirationTime(std::chrono::steady_clock::now()),
	clusterCacheValidity(std::chrono::minutes(30)),
	clusterCacheExpirationTime(std::chrono::steady_clock::now()),
	instanceCacheValidity(std::chrono::minutes(5)),
	instanceCacheExpirationTime(std::chrono::steady_clock::now()),
	secretCacheValidity(std::chrono::minutes(5)),
	secretKey(1024),
	cacheHits(0),databaseQueries(0),databaseScans(0)
{
	loadEncyptionKey();
	log_info("Starting database client");
	InitializeTables();
	log_info("Database client ready");
}

void PersistentStore::InitializeUserTable(){
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
		                       .WithNonKeyAttributes({"ID","name","globusID","email","admin"}))
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
		                       .WithNonKeyAttributes({"ID","name","token","email","admin"}))
		       .WithProvisionedThroughput(ProvisionedThroughput()
		                                  .WithReadCapacityUnits(1)
		                                  .WithWriteCapacityUnits(1));
	};
	auto getByVOIndex=[](){
		return GlobalSecondaryIndex()
		       .WithIndexName("ByVO")
		       .WithKeySchema({KeySchemaElement()
		                       .WithAttributeName("voID")
		                       .WithKeyType(KeyType::HASH)})
		       .WithProjection(Projection()
		                       .WithProjectionType(ProjectionType::INCLUDE)
		                       .WithNonKeyAttributes({"ID", "name", "email"}))
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
			AttDef().WithAttributeName("voID").WithAttributeType(SAT::S)
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
		request.AddGlobalSecondaryIndexes(getByVOIndex());
		
		auto createOut=dbClient.CreateTable(request);
		if(!createOut.IsSuccess())
			log_fatal("Failed to create user table: " + createOut.GetError().GetMessage());
		
		waitTableReadiness(dbClient,userTableName);
		
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
		log_info("Created user table");
	}
	else{ //table exists; check whether any indices are missing
		const TableDescription& tableDesc=userTableOut.GetResult().GetTable();
		
		if(!hasIndex(tableDesc,"ByToken")){
			auto request=updateTableWithNewSecondaryIndex(userTableName,getByTokenIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("token").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-token index to user table: " + createOut.GetError().GetMessage());
			waitTableReadiness(dbClient,userTableName);
			log_info("Added by-token index to user table");
		}
		if(!hasIndex(tableDesc,"ByGlobusID")){
			auto request=updateTableWithNewSecondaryIndex(userTableName,getByGlobusIDIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("globusID").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-GlobusID index to user table: " + createOut.GetError().GetMessage());
			waitTableReadiness(dbClient,userTableName);
			log_info("Added by-GlobusID index to user table");
		}
		if(!hasIndex(tableDesc,"ByVO")){
			auto request=updateTableWithNewSecondaryIndex(userTableName,getByVOIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("voID").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-VO index to user table: " + createOut.GetError().GetMessage());
			waitTableReadiness(dbClient,userTableName);
			log_info("Added by-VO index to user table");
		}
	}
}

void PersistentStore::InitializeVOTable(){
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
		                       .WithNonKeyAttributes({"ID"}))
		       .WithProvisionedThroughput(ProvisionedThroughput()
		                                  .WithReadCapacityUnits(1)
		                                  .WithWriteCapacityUnits(1));
	};
	
	//check status of the table
	auto voTableOut=dbClient.DescribeTable(DescribeTableRequest()
											 .WithTableName(voTableName));
	if(!voTableOut.IsSuccess() &&
	   voTableOut.GetError().GetErrorType()!=Aws::DynamoDB::DynamoDBErrors::RESOURCE_NOT_FOUND){
		log_fatal("Unable to connect to DynamoDB: "
		          << voTableOut.GetError().GetMessage());
	}
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
		request.AddGlobalSecondaryIndexes(getByNameIndex());
		
		auto createOut=dbClient.CreateTable(request);
		if(!createOut.IsSuccess())
			log_fatal("Failed to create VOs table: " + createOut.GetError().GetMessage());
		
		waitTableReadiness(dbClient,voTableName);
		log_info("Created VOs table");
	}
	else{ //table exists; check whether any indices are missing
		const TableDescription& tableDesc=voTableOut.GetResult().GetTable();
		
		if(!hasIndex(tableDesc,"ByName")){
			auto request=updateTableWithNewSecondaryIndex(userTableName,getByNameIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("name").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-name index to VO table: " + createOut.GetError().GetMessage());
			waitTableReadiness(dbClient,voTableName);
			log_info("Added by-name index to VO table");
		}
	}
}

void PersistentStore::InitializeClusterTable(){
	using namespace Aws::DynamoDB::Model;
	using AttDef=Aws::DynamoDB::Model::AttributeDefinition;
	using SAT=Aws::DynamoDB::Model::ScalarAttributeType;
	
	//define indices
	auto getByVOIndex=[](){
		return GlobalSecondaryIndex()
		       .WithIndexName("ByVO")
		       .WithKeySchema({KeySchemaElement()
		                       .WithAttributeName("owningVO")
		                       .WithKeyType(KeyType::HASH)})
		       .WithProjection(Projection()
		                       .WithProjectionType(ProjectionType::INCLUDE)
		                       .WithNonKeyAttributes({"ID","name","config"}))
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
		                       .WithNonKeyAttributes({"ID","owningVO","config"}))
		       .WithProvisionedThroughput(ProvisionedThroughput()
		                                  .WithReadCapacityUnits(1)
		                                  .WithWriteCapacityUnits(1));
	};
	auto getVOAccessIndex=[](){
		return GlobalSecondaryIndex()
		.WithIndexName("VOAccess")
		.WithKeySchema({KeySchemaElement()
			.WithAttributeName("voID")
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
			AttDef().WithAttributeName("owningVO").WithAttributeType(SAT::S),
			AttDef().WithAttributeName("voID").WithAttributeType(SAT::S)
		});
		request.SetKeySchema({
			KeySchemaElement().WithAttributeName("ID").WithKeyType(KeyType::HASH),
			KeySchemaElement().WithAttributeName("sortKey").WithKeyType(KeyType::RANGE)
		});
		request.SetProvisionedThroughput(ProvisionedThroughput()
		                                 .WithReadCapacityUnits(1)
		                                 .WithWriteCapacityUnits(1));
		
		request.AddGlobalSecondaryIndexes(getByVOIndex());
		request.AddGlobalSecondaryIndexes(getByNameIndex());
		request.AddGlobalSecondaryIndexes(getVOAccessIndex());
		
		auto createOut=dbClient.CreateTable(request);
		if(!createOut.IsSuccess())
			log_fatal("Failed to create clusters table: " + createOut.GetError().GetMessage());
		
		waitTableReadiness(dbClient,clusterTableName);
		log_info("Created clusters table");
	}
	else{ //table exists; check whether any indices are missing
		const TableDescription& tableDesc=clusterTableOut.GetResult().GetTable();
		
		if(!hasIndex(tableDesc,"ByVO")){
			auto request=updateTableWithNewSecondaryIndex(clusterTableName,getByVOIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("owningVO").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-VO index to cluster table: " + createOut.GetError().GetMessage());
			waitTableReadiness(dbClient,clusterTableName);
			log_info("Added by-VO index to cluster table");
		}
		if(!hasIndex(tableDesc,"ByName")){
			auto request=updateTableWithNewSecondaryIndex(clusterTableName,getByNameIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("name").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-name index to cluster table: " + createOut.GetError().GetMessage());
			waitTableReadiness(dbClient,clusterTableName);
			log_info("Added by-name index to cluster table");
		}
		if(!hasIndex(tableDesc,"VOAccess")){
			auto request=updateTableWithNewSecondaryIndex(clusterTableName,getVOAccessIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("voID").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add VO access index to cluster table: " + createOut.GetError().GetMessage());
			waitTableReadiness(dbClient,clusterTableName);
			log_info("Added VO access index to cluster table");
		}
	}
}

void PersistentStore::InitializeInstanceTable(){
	using namespace Aws::DynamoDB::Model;
	using AttDef=Aws::DynamoDB::Model::AttributeDefinition;
	using SAT=Aws::DynamoDB::Model::ScalarAttributeType;
	
	//define indices
	auto getByVOIndex=[](){
		return GlobalSecondaryIndex()
		       .WithIndexName("ByVO")
		       .WithKeySchema({KeySchemaElement()
		                       .WithAttributeName("owningVO")
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
		                       .WithNonKeyAttributes({"ID","application","owningVO","cluster","ctime"}))
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
				               .WithNonKeyAttributes({"ID", "name", "application", "owningVO", "ctime"}))
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
			AttDef().WithAttributeName("owningVO").WithAttributeType(SAT::S),
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
		request.AddGlobalSecondaryIndexes(getByVOIndex());
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
		
		if(!hasIndex(tableDesc,"ByVO")){
			auto request=updateTableWithNewSecondaryIndex(instanceTableName,getByVOIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("owningVO").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-VO index to instance table: " + createOut.GetError().GetMessage());
			waitTableReadiness(dbClient,instanceTableName);
			log_info("Added by-VO index to instance table");
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
	auto getByVOIndex=[](){
		return GlobalSecondaryIndex()
		       .WithIndexName("ByVO")
		       .WithKeySchema({KeySchemaElement()
		                       .WithAttributeName("vo")
		                       .WithKeyType(KeyType::HASH)})
		       .WithProjection(Projection()
		                       .WithProjectionType(ProjectionType::INCLUDE)
		                       .WithNonKeyAttributes({"ID","name","cluster","ctime","contents"}))
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
			AttDef().WithAttributeName("vo").WithAttributeType(SAT::S),
			//AttDef().WithAttributeName("cluster").WithAttributeType(SAT::S),
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
		request.AddGlobalSecondaryIndexes(getByVOIndex());
		
		auto createOut=dbClient.CreateTable(request);
		if(!createOut.IsSuccess())
			log_fatal("Failed to create secrets table: " + createOut.GetError().GetMessage());
		
		waitTableReadiness(dbClient,secretTableName);
		log_info("Created secrets table");
	}
	else{ //table exists; check whether any indices are missing
		const TableDescription& tableDesc=secretTableOut.GetResult().GetTable();
		
		if(!hasIndex(tableDesc,"ByVO")){
			auto request=updateTableWithNewSecondaryIndex(secretTableName,getByVOIndex());
			request.WithAttributeDefinitions({AttDef().WithAttributeName("vo").WithAttributeType(SAT::S)});
			auto createOut=dbClient.UpdateTable(request);
			if(!createOut.IsSuccess())
				log_fatal("Failed to add by-VO index to secret table: " + createOut.GetError().GetMessage());
			waitTableReadiness(dbClient,secretTableName);
			log_info("Added by-VO index to secret table");
		}
	}
}

void PersistentStore::InitializeTables(){
	InitializeUserTable();
	InitializeVOTable();
	InitializeClusterTable();
	InitializeInstanceTable();
	InitializeSecretTable();
}

void PersistentStore::loadEncyptionKey(){
	static const std::string fileName="encryptionKey";
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
	userCache.insert_or_assign(user.id,record);
	userByTokenCache.insert_or_assign(user.token,record);
	userByGlobusIDCache.insert_or_assign(user.globusID,record);
	
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
	user.token=findOrThrow(item,"token","user record missing token attribute").GetS();
	user.globusID=findOrThrow(item,"globusID","user record missing globusID attribute").GetS();
	user.admin=findOrThrow(item,"admin","user record missing admin attribute").GetBool();
	
	//update caches
	CacheRecord<User> record(user,userCacheValidity);
	userCache.insert_or_assign(user.id,record);
	userByTokenCache.insert_or_assign(user.token,record);
	userByGlobusIDCache.insert_or_assign(user.globusID,record);
	
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
	user.admin=findOrThrow(item,"admin","user record missing admin attribute").GetBool();
	
	//update caches
	CacheRecord<User> record(user,userCacheValidity);
	userCache.insert_or_assign(user.id,record);
	userByTokenCache.insert_or_assign(user.token,record);
	userByGlobusIDCache.insert_or_assign(user.globusID,record);
	
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
	user.admin=findOrThrow(item,"admin","user record missing admin attribute").GetBool();
	
	//update caches
	CacheRecord<User> record(user,userCacheValidity);
	userCache.insert_or_assign(user.id,record);
	userByTokenCache.insert_or_assign(user.token,record);
	userByGlobusIDCache.insert_or_assign(user.globusID,record);
	
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
	                                            {"admin",AVU().WithValue(AV().SetBool(user.admin))}
	                                 }));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to update user record: " << err.GetMessage());
		return false;
	}
	
	//update caches
	CacheRecord<User> record(user,userCacheValidity);
	userCache.insert_or_assign(user.id,record);
	//if the token has changed, ensure that any old cache record is removed
	if(oldUser.token!=user.token)
		userByTokenCache.erase(oldUser.token);
	userByTokenCache.insert_or_assign(user.token,record);
	userByGlobusIDCache.insert_or_assign(user.globusID,record);
	
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
	request.SetFilterExpression("attribute_not_exists(#voID)");
	request.SetExpressionAttributeNames({{"#voID", "voID"}});
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
			collected.push_back(user);

			CacheRecord<User> record(user,userCacheValidity);
			userCache.insert_or_assign(user.id,record);
		}
	}while(keepGoing);
	userCacheExpirationTime=std::chrono::steady_clock::now()+userCacheValidity;
	
	return collected;
}

std::vector<User> PersistentStore::listUsersByVO(const std::string& vo){
	//first check if list of users is cached
	CacheRecord<std::string> record;
	auto cached = userByVOCache.find(vo);
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
			       .WithIndexName("ByVO")
			       .WithKeyConditionExpression("#voID = :vo_val")
			       .WithExpressionAttributeNames({{"#voID", "voID"}})
			       .WithExpressionAttributeValues({{":vo_val", AV(vo)}})
			       );

	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to list Users by VO: " << err.GetMessage());
		return users;
	}

	const auto& queryResult=outcome.GetResult();
	if(queryResult.GetCount()==0)
		return users;

	for(const auto& item : queryResult.GetItems()){
		User user;

		user.valid=true;
		user.id=findOrThrow(item, "ID", "User record missing ID attribute").GetS();
		user.name=findOrThrow(item, "name", "User record missing name attribute").GetS();
		user.email=findOrThrow(item, "email", "User record missing email attribute").GetS();
		
		users.push_back(user);

		auto voID=findOrThrow(item, "voID", "User record missing voID attribute").GetS();
		
		//update caches
		CacheRecord<User> record(user,userCacheValidity);
		userCache.insert_or_assign(user.id,record);
		CacheRecord<std::string> VOrecord(user.id,userCacheValidity);
		userByVOCache.insert_or_assign(voID,VOrecord);
	}
	userByVOCache.update_expiration(vo,std::chrono::steady_clock::now()+userCacheValidity);
	
	return users;	
}

bool PersistentStore::addUserToVO(const std::string& uID, std::string voID){
	//check whether the 'ID' we got was actually a name
	if(voID.find(IDGenerator::voIDPrefix)!=0){
		//if a name, find the corresponding VO
		VO vo=findVOByName(voID);
		//if no such VO exists we cannot add the user to it
		if(!vo)
			return false;
		//otherwise, get the actual VO ID and continue with the operation
		voID=vo.id;
	}

	VO vo = findVOByID(voID);
	User user = getUser(uID);
	
	using Aws::DynamoDB::Model::AttributeValue;
	auto request=Aws::DynamoDB::Model::PutItemRequest()
	.WithTableName(userTableName)
	.WithItem({
		{"ID",AttributeValue(uID)},
		{"name",AttributeValue(user.name)},
		{"email",AttributeValue(user.email)},
		{"sortKey",AttributeValue(uID+":"+voID)},
		{"voID",AttributeValue(voID)}
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to add user VO membership record: " << err.GetMessage());
		return false;
	}
	
	//update cache
	CacheRecord<std::string> record(uID,userCacheValidity);
	userByVOCache.insert_or_assign(voID,record);
	CacheRecord<VO> VOrecord(vo,voCacheValidity); 
	voByUserCache.insert_or_assign(user.id, VOrecord);
	
	return true;
}

bool PersistentStore::removeUserFromVO(const std::string& uID, std::string voID){
	//check whether the 'ID' we got was actually a name
	if(voID.find(IDGenerator::voIDPrefix)!=0){
		//if a name, find the corresponding VO
		VO vo=findVOByName(voID);
		//if no such VO exists, the user cannot be a member; no further work is needed
		if(!vo)
			return true;
		//otherwise, get the actual VO ID and continue with the operation
		voID=vo.id;
	}
	
	//remove any cache entry
	userByVOCache.erase(voID,CacheRecord<std::string>(uID));

	CacheRecord<VO> record;
	bool cached=voCache.find(voID,record);
	if (cached)
		voByUserCache.erase(uID, record);
	
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

std::vector<std::string> PersistentStore::getUserVOMemberships(const std::string& uID, bool useNames){
	using Aws::DynamoDB::Model::AttributeValue;
	databaseQueries++;
	log_info("Querying database for user " << uID << " VO memberships");
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
	
	if(useNames){
		//do extra lookups to replace IDs with nicer names
		for(std::string& voStr : vos){
			VO vo=findVOByID(voStr);
			voStr=vo.name;
		}
	}
	
	return vos;
}

bool PersistentStore::userInVO(const std::string& uID, std::string voID){
	//TODO: possible issue: We only store memberships, so repeated queries about
	//a user's belonging to a VO to which that user does not in fact belong will
	//never be in the cache, and will always incur a database query. This should
	//not be a problem for normal/well intentioned use, but seems like a way to
	//turn accident or malice into denial of service or a large AWS bill. 
	
	//check whether the 'ID' we got was actually a name
	if(voID.find(IDGenerator::voIDPrefix)!=0){
		//if a name, find the corresponding VO
		VO vo=findVOByName(voID);
		//if no such VO exists, the user cannot be a member
		if(!vo)
			return false;
		//otherwise, get the actual VO ID and continue with the lookup
		voID=vo.id;
	}
	
	//first see if we have this cached
	{
		CacheRecord<std::string> record(uID);
		if(userByVOCache.find(voID,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return record;
			}
		}
	}
	//need to query the database
	databaseQueries++;
	log_info("Querying database for user " << uID << " membership in VO " << voID);
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
	
	//update cache
	CacheRecord<std::string> record(uID,userCacheValidity);
	userByVOCache.insert_or_assign(voID,record);
	
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
	
	//update caches
	CacheRecord<VO> record(vo,voCacheValidity);
	voCache.insert_or_assign(vo.id,record);
	voByNameCache.insert_or_assign(vo.name,record);
        
	return true;
}

bool PersistentStore::removeVO(const std::string& voID){
	using Aws::DynamoDB::Model::AttributeValue;
	
	//delete all memberships in the VO
	for(auto uID : getMembersOfVO(voID)){
		if(!removeUserFromVO(uID,voID))
			return false;
	}
	
	//erase cache entries
	{
		//Somewhat hacky: we can't erase the secondary cache entries unless we know 
		//the keys. However, we keep the caches synchronized, so if there is 
		//such an entry to delete there is also an entry in the main cache, so 
		//we can grab that to get the name without having to read from the 
		//database.
		CacheRecord<VO> record;
		bool cached=voCache.find(voID,record);
		if(cached){
			//don't particularly care whether the record is expired; if it is 
			//all that will happen is that we will delete the equally stale 
			//record in the other cache
			voByNameCache.erase(record.record.name);
		}
		voCache.erase(voID);
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
	databaseQueries++;
	log_info("Querying database for members of VO " << voID);
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
	//First check if vos are cached
	std::vector<VO> collected;
	if(voCacheExpirationTime.load() > std::chrono::steady_clock::now()){
	        auto table = voCache.lock_table();
		for(auto itr = table.cbegin(); itr != table.cend(); itr++){
		        auto vo = itr->second;
			cacheHits++;
			collected.push_back(vo);
		}
	
		table.unlock();
		return collected;
	}	

	databaseScans++;
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

			CacheRecord<VO> record(vo,voCacheValidity);
			voCache.insert_or_assign(vo.id,record);
			voByNameCache.insert_or_assign(vo.name,record);
		}
	}while(keepGoing);
	voCacheExpirationTime=std::chrono::steady_clock::now()+voCacheValidity;
	
	return collected;
}

std::vector<VO> PersistentStore::listVOsForUser(const std::string& user){
	// first check if VOs list is cached
	CacheRecord<VO> record;
	auto cached = voByUserCache.find(user);
	if (cached.second > std::chrono::steady_clock::now()) {
		auto records = cached.first;
		std::vector<VO> vos;
		for (auto record : records) {
			cacheHits++;
			vos.push_back(record);
		}
		return vos;
	}

	std::vector<VO> vos;
	using AV=Aws::DynamoDB::Model::AttributeValue;
	databaseQueries++;

	Aws::DynamoDB::Model::QueryOutcome outcome;
	outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
			       .WithTableName(userTableName)
			       .WithKeyConditionExpression("ID = :user_val")
			       .WithFilterExpression("attribute_exists(#voID)")
			       .WithExpressionAttributeValues({{":user_val", AV(user)}})
			       .WithExpressionAttributeNames({{"#voID", "voID"}})
			       );

	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to list VOs by user: " << err.GetMessage());
		return vos;
	}

	const auto& queryResult=outcome.GetResult();
	if(queryResult.GetCount()==0)
		return vos;

	for(const auto& item : queryResult.GetItems()){
		std::string voID = findOrThrow(item, "voID", "User record missing voID attribute").GetS();
		
	  	VO vo = findVOByID(voID);
		vos.push_back(vo);
		
		//update caches
		CacheRecord<VO> record(vo,voCacheValidity);
		voCache.insert_or_assign(vo.id,record);
		voByNameCache.insert_or_assign(vo.name,record);
		voByUserCache.insert_or_assign(user,record);
	}
	voByUserCache.update_expiration(user,std::chrono::steady_clock::now()+voCacheValidity);
	
	return vos;
}

VO PersistentStore::findVOByID(const std::string& id){
	//first see if we have this cached
	{
		CacheRecord<VO> record;
		if(voCache.find(id,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return record;
			}
		}
	}
	//need to query the database
	databaseQueries++;
	log_info("Querying database for VO " << id);
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
	
	//update caches
	CacheRecord<VO> record(vo,voCacheValidity);
	voCache.insert_or_assign(vo.id,record);
	voByNameCache.insert_or_assign(vo.name,record);
	
	return vo;
}

VO PersistentStore::findVOByName(const std::string& name){
	//first see if we have this cached
	{
		CacheRecord<VO> record;
		if(voByNameCache.find(name,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return record;
			}
		}
	}
	//need to query the database
	databaseQueries++;
	log_info("Querying database for VO " << name);
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
	
	//update caches
	CacheRecord<VO> record(vo,voCacheValidity);
	voCache.insert_or_assign(vo.id,record);
	voByNameCache.insert_or_assign(vo.name,record);
	
	return vo;
}

VO PersistentStore::getVO(const std::string& idOrName){
	if(idOrName.find(IDGenerator::voIDPrefix)==0)
		return findVOByID(idOrName);
	return findVOByName(idOrName);
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
		{"owningVO",AttributeValue(cluster.owningVO)},
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to add cluster record: " << err.GetMessage());
		return false;
	}
	
	CacheRecord<Cluster> record(cluster,clusterCacheValidity);
	clusterCache.insert_or_assign(cluster.id,record);
	clusterByNameCache.insert_or_assign(cluster.name,record);
	clusterByVOCache.insert_or_assign(cluster.owningVO,record);
	writeClusterConfigToDisk(cluster);
	
	return true;
}

FileHandle PersistentStore::makeTemporaryFile(const std::string& nameBase){
	std::string base=clusterConfigDir+"/"+nameBase+"XXXXXXXX";
	//make a modifiable copy for mkdtemp to scribble over
	std::unique_ptr<char[]> filePath(new char[base.size()+1]);
	strcpy(filePath.get(),base.c_str());
	struct fdHolder{
		int fd;
		~fdHolder(){ close(fd); }
	} fd{mkstemp(filePath.get())};
	if(fd.fd==-1){
		int err=errno;
		log_fatal("Creating temporary file failed with error " << err);
	}
	return FileHandle(filePath.get());
}

void PersistentStore::writeClusterConfigToDisk(const Cluster& cluster){
	FileHandle file=makeTemporaryFile(cluster.id+"_v");
	std::ofstream confFile(file.path());
	if(!confFile)
		log_fatal("Unable to open " << file.path() << " for writing");
	confFile << cluster.config;
	if(confFile.fail())
		log_fatal("Unable to write cluster config to " << file.path());
	
	clusterConfigs.insert_or_assign(cluster.id,std::make_shared<FileHandle>(std::move(file)));
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
	cluster.owningVO=findOrThrow(item,"owningVO","Cluster record missing owningVO attribute").GetS();
	cluster.config=findOrThrow(item,"config","Cluster record missing config attribute").GetS();
	
	//cache this result for reuse
	CacheRecord<Cluster> record(cluster,clusterCacheValidity);
	clusterCache.insert_or_assign(cluster.id,record);
	clusterByNameCache.insert_or_assign(cluster.name,record);
	clusterByVOCache.insert_or_assign(cluster.owningVO,record);
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
	cluster.owningVO=findOrThrow(queryResult.GetItems().front(),"owningVO",
	                             "Cluster record missing owningVO attribute").GetS();
	cluster.config=findOrThrow(queryResult.GetItems().front(),"config",
	                           "Cluster record missing config attribute").GetS();
	
	//cache this result for reuse
	CacheRecord<Cluster> record(cluster,clusterCacheValidity);
	clusterCache.insert_or_assign(cluster.id,record);
	clusterByNameCache.insert_or_assign(cluster.name,record);
	clusterByVOCache.insert_or_assign(cluster.owningVO,record);
	writeClusterConfigToDisk(cluster);
	
	return cluster;
}

Cluster PersistentStore::getCluster(const std::string& idOrName){
	if(idOrName.find(IDGenerator::clusterIDPrefix)==0)
		return findClusterByID(idOrName);
	return findClusterByName(idOrName);
}

bool PersistentStore::removeCluster(const std::string& cID){
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
			clusterByVOCache.erase(record.record.owningVO,record);
		}
	}
	clusterCache.erase(cID);
	clusterConfigs.erase(cID);
	
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
	                                            {"owningVO",AVU().WithValue(AV(cluster.owningVO))}})
	                                 );
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to update cluster record: " << err.GetMessage());
		return false;
	}
	
	//update caches
	CacheRecord<Cluster> record(cluster,clusterCacheValidity);
	clusterCache.insert_or_assign(cluster.id,record);
	clusterByNameCache.insert_or_assign(cluster.name,record);
	clusterByVOCache.insert_or_assign(cluster.owningVO,record);
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
	request.SetFilterExpression("attribute_not_exists(#voID)");
	request.SetExpressionAttributeNames({{"#voID", "voID"}});
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

			CacheRecord<Cluster> record(cluster,clusterCacheValidity);
			clusterCache.insert_or_assign(cluster.id,record);
			clusterByNameCache.insert_or_assign(cluster.name,record);
			clusterByVOCache.insert_or_assign(cluster.owningVO,record);
		}
	}while(keepGoing);
	clusterCacheExpirationTime=std::chrono::steady_clock::now()+clusterCacheValidity;
	
	return collected;
}

bool PersistentStore::addVOToCluster(std::string voID, std::string cID){
	//check whether the VO 'ID' we got was actually a name
	if(voID.find(IDGenerator::voIDPrefix)!=0){
		//if a name, find the corresponding VO
		VO vo=findVOByName(voID);
		//if no such VO exists we cannot add the user to it
		if(!vo)
			return false;
		//otherwise, get the actual VO ID and continue with the operation
		voID=vo.id;
	}
	//check whether the cluster 'ID' we got was actually a name
	if(cID.find(IDGenerator::clusterIDPrefix)!=0){
		//if a name, find the corresponding Cluster
		Cluster cluster=findClusterByName(cID);
		//if no such cluster exists we cannot add the VO to it
		if(!cluster)
			return false;
		//otherwise, get the actual cluster ID and continue with the operation
		cID=cluster.id;
	}
	
	using Aws::DynamoDB::Model::AttributeValue;
	auto request=Aws::DynamoDB::Model::PutItemRequest()
	.WithTableName(clusterTableName)
	.WithItem({
		{"ID",AttributeValue(cID)},
		{"sortKey",AttributeValue(cID+":"+voID)},
		{"voID",AttributeValue(voID)}
	});
	auto outcome=dbClient.PutItem(request);
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to add VO cluster access record: " << err.GetMessage());
		return false;
	}
	
	//update cache
	CacheRecord<std::string> record(voID,clusterCacheValidity);
	clusterVOAccessCache.insert_or_assign(cID,record);
	
	return true;
}

bool PersistentStore::removeVOFromCluster(std::string voID, std::string cID){
	//check whether the VO 'ID' we got was actually a name
	if(voID.find(IDGenerator::voIDPrefix)!=0){
		//if a name, find the corresponding VO
		VO vo=findVOByName(voID);
		//if no such VO exists we cannot add the user to it
		if(!vo)
			return false;
		//otherwise, get the actual VO ID and continue with the operation
		voID=vo.id;
	}
	//check whether the cluster 'ID' we got was actually a name
	if(cID.find(IDGenerator::clusterIDPrefix)!=0){
		//if a name, find the corresponding Cluster
		Cluster cluster=findClusterByName(cID);
		//if no such cluster exists we cannot remove the VO from it
		if(!cluster)
			return false;
		//otherwise, get the actual cluster ID and continue with the operation
		cID=cluster.id;
	}
	
	//remove any cache entry
	clusterVOAccessCache.erase(cID,CacheRecord<std::string>(voID));
	
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.DeleteItem(Aws::DynamoDB::Model::DeleteItemRequest()
	                                 .WithTableName(clusterTableName)
	                                 .WithKey({{"ID",AttributeValue(cID)},
	                                           {"sortKey",AttributeValue(cID+":"+voID)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to delete VO cluster access record: " << err.GetMessage());
		return false;
	}
	return true;
}

std::vector<std::string> PersistentStore::listVOsAllowedOnCluster(std::string cID, bool useNames){
	//check whether the cluster 'ID' we got was actually a name
	if(cID.find(IDGenerator::clusterIDPrefix)!=0){
		//if a name, find the corresponding Cluster
		Cluster cluster=findClusterByName(cID);
		//if no such cluster exists no VOs can use it
		if(!cluster)
			return {};
		//otherwise, get the actual cluster ID and continue with the operation
		cID=cluster.id;
	}
	
	using Aws::DynamoDB::Model::AttributeValue;
	databaseQueries++;
	log_info("Querying database for VOs allowed on cluster " << cID);
	auto request=Aws::DynamoDB::Model::QueryRequest()
	.WithTableName(clusterTableName)
	.WithKeyConditionExpression("#id = :id AND begins_with(#sortKey,:prefix)")
	.WithExpressionAttributeNames({
		{"#id","ID"},
		{"#sortKey","sortKey"}
	})
	.WithExpressionAttributeValues({
		{":id",AttributeValue(cID)},
		{":prefix",AttributeValue(cID+":"+IDGenerator::voIDPrefix)}
	});
	auto outcome=dbClient.Query(request);
	std::vector<std::string> vos;
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch cluster's VO whitelist records: " << err.GetMessage());
		return vos;
	}
	
	const auto& queryResult=outcome.GetResult();
	for(const auto& item : queryResult.GetItems()){
		if(item.count("voID"))
			vos.push_back(item.find("voID")->second.GetS());
	}
	
	if(useNames){
		//do extra lookups to replace IDs with nicer names
		for(std::string& voStr : vos){
			VO vo=findVOByID(voStr);
			voStr=vo.name;
		}
	}
	
	return vos;
}

bool PersistentStore::voAllowedOnCluster(std::string voID, std::string cID){
	//TODO: possible issue: We only store memberships, so repeated queries about
	//a VO's access to a cluster to which it does not have access belong will
	//never be in the cache, and will always incur a database query. This should
	//not be a problem for normal/well intentioned use, but seems like a way to
	//turn accident or malice into denial of service or a large AWS bill. 
	
	//check whether the 'ID' we got was actually a name
	if(voID.find(IDGenerator::voIDPrefix)!=0){
		//if a name, find the corresponding VO
		VO vo=findVOByName(voID);
		//if no such VO exists, it cannot have access
		if(!vo)
			return false;
		//otherwise, get the actual VO ID and continue with the lookup
		voID=vo.id;
	}
	if(cID.find(IDGenerator::clusterIDPrefix)!=0){
		//if a name, find the corresponding VO
		Cluster cluster=findClusterByName(cID);
		//if no such cluster exists the VO cannot have access
		if(!cluster)
			return false;
		//otherwise, get the actual cluster ID and continue with the lookup
		cID=cluster.id;
	}
	
	//first see if we have this cached
	{
		CacheRecord<std::string> record(voID);
		if(clusterVOAccessCache.find(cID,record)){
			//we have a cached record; is it still valid?
			if(record){ //it is, just return it
				cacheHits++;
				return record;
			}
		}
	}
	//need to query the database
	databaseQueries++;
	log_info("Querying database for VO " << voID << " access to cluster " << cID);
	using Aws::DynamoDB::Model::AttributeValue;
	auto outcome=dbClient.GetItem(Aws::DynamoDB::Model::GetItemRequest()
								  .WithTableName(clusterTableName)
								  .WithKey({{"ID",AttributeValue(cID)},
	                                        {"sortKey",AttributeValue(cID+":"+voID)}}));
	if(!outcome.IsSuccess()){
		auto err=outcome.GetError();
		log_error("Failed to fetch cluster VO access record: " << err.GetMessage());
		return false;
	}
	const auto& item=outcome.GetResult().GetItem();
	if(item.empty()) //no match found
		return false;
	
	//update cache
	CacheRecord<std::string> record(voID,clusterCacheValidity);
	userByVOCache.insert_or_assign(cID,record);
	
	return true;
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
	
	//update caches
	CacheRecord<ApplicationInstance> record(inst,instanceCacheValidity);
	instanceCache.insert_or_assign(inst.id,record);
	instanceByVOCache.insert_or_assign(inst.owningVO,record);
	instanceByNameCache.insert_or_assign(inst.name,record);
	instanceByClusterCache.insert_or_assign(inst.cluster,record);
	instanceByVOAndClusterCache.insert_or_assign(inst.owningVO+":"+inst.cluster,record);
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
			instanceByVOCache.erase(record.record.owningVO,record);
			instanceByNameCache.erase(record.record.name,record);
			instanceByClusterCache.erase(record.record.cluster,record);
			instanceByVOAndClusterCache.erase(record.record.owningVO+":"+record.record.cluster,record);
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
			log_info("Found record of " << id << " in cache");
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
	inst.owningVO=findOrThrow(item,"owningVO","Instance record missing owningVO attribute").GetS();
	inst.cluster=findOrThrow(item,"cluster","Instance record missing cluster attribute").GetS();
	inst.ctime=findOrThrow(item,"ctime","Instance record missing ctime attribute").GetS();
	
	//update caches
	CacheRecord<ApplicationInstance> record(inst,instanceCacheValidity);
	instanceCache.insert_or_assign(inst.id,record);
	instanceByVOCache.insert_or_assign(inst.owningVO,record);
	instanceByNameCache.insert_or_assign(inst.name,record);
	instanceByClusterCache.insert_or_assign(inst.cluster,record);
	instanceByVOAndClusterCache.insert_or_assign(inst.owningVO+":"+inst.cluster,record);
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
	instanceConfigCache.insert_or_assign(id,record);
	
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
			inst.owningVO=findOrThrow(item,"owningVO","Instance record missing ID attribute").GetS();
			inst.cluster=findOrThrow(item,"cluster","Instance record missing ID attribute").GetS();
			inst.ctime=findOrThrow(item,"ctime","Instance record missing ID attribute").GetS();
			collected.push_back(inst);

			CacheRecord<ApplicationInstance> record(inst,instanceCacheValidity);
			instanceCache.insert_or_assign(inst.id,record);
			instanceByNameCache.insert_or_assign(inst.name,record);
			instanceByVOCache.insert_or_assign(inst.owningVO,record);
			instanceByClusterCache.insert_or_assign(inst.cluster,record);
			instanceByVOAndClusterCache.insert_or_assign(inst.owningVO+":"+inst.cluster,record);
		}
	}while(keepGoing);
	instanceCacheExpirationTime=std::chrono::steady_clock::now()+instanceCacheValidity;
	
	return collected;
}

std::vector<ApplicationInstance> PersistentStore::listApplicationInstancesByClusterOrVO(std::string vo, std::string cluster){
	std::vector<ApplicationInstance> instances;
	
	//check whether the VO 'ID' we got was actually a name
	if(!vo.empty() && vo.find(IDGenerator::voIDPrefix)!=0){
		//if a name, find the corresponding VO
		VO vo_=findVOByName(vo);
		//if no such VO exists it cannot have any running instances
		if(!vo_)
			return instances;
		//otherwise, get the actual VO ID and continue with the operation
		vo=vo_.id;
	}
	//check whether the cluster 'ID' we got was actually a name
	if(!cluster.empty() && cluster.find(IDGenerator::clusterIDPrefix)!=0){
		//if a name, find the corresponding Cluster
		Cluster cluster_=findClusterByName(cluster);
		//if no such cluster exists it cannot have any running instances
		if(!cluster_)
			return instances;
		//otherwise, get the actual cluster ID and continue with the operation
		cluster=cluster_.id;
	}
	
	// First check if the instances are cached
	if (!vo.empty() && !cluster.empty()) {
		CacheRecord<ApplicationInstance> record;
		auto cached = instanceByVOAndClusterCache.find(vo+":"+cluster);
		if(cached.second > std::chrono::steady_clock::now()){
			auto records = cached.first;
			cacheHits+=records.size();
			return std::vector<ApplicationInstance>(records.begin(),records.end());
		}
	} else if (!vo.empty()) {
		CacheRecord<ApplicationInstance> record;
		auto cached = instanceByVOCache.find(vo);
		if(cached.second > std::chrono::steady_clock::now()){
			auto records = cached.first;
			cacheHits+=records.size();
			return std::vector<ApplicationInstance>(records.begin(),records.end());
		}
	} else if (!cluster.empty()) {
		CacheRecord<ApplicationInstance> record;
		auto cached = instanceByClusterCache.find(cluster);
		if(cached.second > std::chrono::steady_clock::now()){
			auto records = cached.first;
			cacheHits+=records.size();
			return std::vector<ApplicationInstance>(records.begin(),records.end());
		}
	}

	// Query if cache is not updated
	using AV=Aws::DynamoDB::Model::AttributeValue;
	databaseQueries++;
	Aws::DynamoDB::Model::QueryOutcome outcome;

	if (!vo.empty() && !cluster.empty()) {
		outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
				       .WithTableName(instanceTableName)
				       .WithIndexName("ByVO")
				       .WithKeyConditionExpression("owningVO = :vo_val")
				       .WithFilterExpression("contains(#cluster, :cluster_val)")
				       .WithExpressionAttributeNames({{"#cluster", "cluster"}})
				       .WithExpressionAttributeValues({{":vo_val", AV(vo)}, {":cluster_val", AV(cluster)}})
				       );
	} else if (!vo.empty()) {
		outcome=dbClient.Query(Aws::DynamoDB::Model::QueryRequest()
				       .WithTableName(instanceTableName)
				       .WithIndexName("ByVO")
				       .WithKeyConditionExpression("owningVO = :vo_val")
				       .WithExpressionAttributeValues({{":vo_val", AV(vo)}})
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
		log_error("Failed to list Instances by Cluster or VO: " << err.GetMessage());
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
		instance.owningVO=findOrThrow(item, "owningVO", "Instance record missing owning VO attribute").GetS();
		instance.cluster=findOrThrow(item,"cluster","Instance record missing cluster attribute").GetS();
		instance.ctime=findOrThrow(item,"ctime","Instance record missing ctime attribute").GetS();
		instance.valid=true;
		
		instances.push_back(instance);
		
		//update caches
		CacheRecord<ApplicationInstance> record(instance,instanceCacheValidity);
		instanceCache.insert_or_assign(instance.id,record);
		instanceByVOCache.insert_or_assign(instance.owningVO,record);
		instanceByNameCache.insert_or_assign(instance.name,record);
		instanceByClusterCache.insert_or_assign(instance.cluster,record);
		instanceByVOAndClusterCache.insert_or_assign(instance.owningVO+":"+instance.cluster,record);
       	}
	auto expirationTime = std::chrono::steady_clock::now() + instanceCacheValidity;
	if (!vo.empty() && !cluster.empty())
		instanceByVOAndClusterCache.update_expiration(vo+":"+cluster, expirationTime);
        else if (!vo.empty())
		instanceByVOCache.update_expiration(vo, expirationTime);
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
		instance.owningVO=findOrThrow(item,"owningVO","Instance record missing owning VO attribute").GetS();
		instance.cluster=findOrThrow(item,"cluster","Instance record missing cluster attribute").GetS();
		instance.ctime=findOrThrow(item,"ctime","Instance record missing ctime attribute").GetS();
		instance.valid=true;
		
		instances.push_back(instance);
		
		//update caches since we bothered to pull stuff directly from the DB
		CacheRecord<ApplicationInstance> record(instance,instanceCacheValidity);
		instanceCache.insert_or_assign(instance.id,record);
		instanceByVOCache.insert_or_assign(instance.owningVO,record);
		instanceByNameCache.insert_or_assign(instance.name,record);
		instanceByClusterCache.insert_or_assign(instance.cluster,record);
		instanceByVOAndClusterCache.insert_or_assign(instance.owningVO+":"+instance.cluster,record);
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
		{"vo",AttributeValue(secret.vo)},
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
	secretCache.insert_or_assign(secret.id,record);
	secretByVOCache.insert_or_assign(secret.vo,record);
	secretByVOAndClusterCache.insert_or_assign(secret.vo+":"+secret.cluster,record);
	
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
			secretByVOCache.erase(record.record.vo,record);
			secretByVOAndClusterCache.erase(record.record.vo+":"+record.record.cluster);
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
	secret.vo=findOrThrow(item,"vo","Secret record missing vo attribute").GetS();
	secret.cluster=findOrThrow(item,"cluster","Secret record missing cluster attribute").GetS();
	secret.ctime=findOrThrow(item,"ctime","Secret record missing ctime attribute").GetS();
	const auto& secret_data=findOrThrow(item,"contents","Secret record missing contents attribute").GetB();
	secret.data=std::string((const std::string::value_type*)secret_data.GetUnderlyingData(),secret_data.GetLength());
	
	//update caches
	CacheRecord<Secret> record(secret,secretCacheValidity);
	secretCache.insert_or_assign(secret.id,record);
	secretByVOCache.insert_or_assign(secret.vo,record);
	secretByVOAndClusterCache.insert_or_assign(secret.vo+":"+secret.cluster,record);
	
	return secret;
}

std::vector<Secret> PersistentStore::listSecrets(std::string vo, std::string cluster){
	std::vector<Secret> secrets;
	
	//check whether the VO 'ID' we got was actually a name
	if(vo.find(IDGenerator::voIDPrefix)!=0){
		//if a name, find the corresponding VO
		VO vo_=findVOByName(vo);
		//if no such VO exists it cannot have any running instances
		if(!vo_)
			return secrets;
		//otherwise, get the actual VO ID and continue with the operation
		vo=vo_.id;
	}
	//check whether the cluster 'ID' we got was actually a name
	if(!cluster.empty() && cluster.find(IDGenerator::clusterIDPrefix)!=0){
		//if a name, find the corresponding Cluster
		Cluster cluster_=findClusterByName(cluster);
		//if no such cluster exists it cannot have any running instances
		if(!cluster_)
			return secrets;
		//otherwise, get the actual cluster ID and continue with the operation
		cluster=cluster_.id;
	}
	
	// First check if the instances are cached
	if (!vo.empty() && !cluster.empty()) {
		CacheRecord<ApplicationInstance> record;
		auto cached = secretByVOAndClusterCache.find(vo+":"+cluster);
		if(cached.second > std::chrono::steady_clock::now()){
			auto records = cached.first;
			cacheHits+=records.size();
			return std::vector<Secret>(records.begin(),records.end());
		}
	} else if (!vo.empty()) {
		CacheRecord<ApplicationInstance> record;
		auto cached = secretByVOCache.find(vo);
		if(cached.second > std::chrono::steady_clock::now()){
			auto records = cached.first;
			cacheHits+=records.size();
			return std::vector<Secret>(records.begin(),records.end());
		}
	}

	// Query if cache is not updated
	using AV=Aws::DynamoDB::Model::AttributeValue;
	databaseQueries++;

	Aws::DynamoDB::Model::QueryRequest query;
	query.WithTableName(secretTableName)
	     .WithIndexName("ByVO")
	     .WithKeyConditionExpression("vo = :vo_val")
	     .WithExpressionAttributeValues({{":vo_val", AV(vo)}});
	if (!cluster.empty()) {
		query.SetFilterExpression("contains(#cluster, :cluster_val)");
		query.AddExpressionAttributeNames("#cluster", "cluster");
		query.AddExpressionAttributeValues(":cluster_val", AV(cluster));
	}
	Aws::DynamoDB::Model::QueryOutcome outcome;
	outcome=dbClient.Query(query);
	
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
		secret.vo=findOrThrow(item, "vo", "Secret record missing owning VO attribute").GetS();
		secret.cluster=findOrThrow(item,"cluster","Secret record missing cluster attribute").GetS();
		secret.ctime=findOrThrow(item,"ctime","Secret record missing ctime attribute").GetS();
		const auto& secret_data=findOrThrow(item,"contents","Secret record missing contents attribute").GetB();
		secret.data=std::string((const std::string::value_type*)secret_data.GetUnderlyingData(),secret_data.GetLength());
		secret.valid=true;
		
		secrets.push_back(secret);
		
		//update caches
		CacheRecord<Secret> record(secret,secretCacheValidity);
		secretCache.insert_or_assign(secret.id,record);
		secretByVOCache.insert_or_assign(secret.vo,record);
		secretByVOAndClusterCache.insert_or_assign(secret.vo+":"+secret.cluster,record);
	}
	auto expirationTime = std::chrono::steady_clock::now() + secretCacheValidity;
	if (!cluster.empty())
		secretByVOAndClusterCache.update_expiration(vo+":"+cluster, expirationTime);
	else
		secretByVOCache.update_expiration(vo, expirationTime);
	
	return secrets;
}

Secret PersistentStore::findSecretByName(std::string vo, std::string cluster, std::string name){
	auto secrets=listSecrets(vo, cluster);
	for(const auto& secret : secrets){
		if(secret.name==name)
			return secret;
	}
	return Secret();
}

std::string PersistentStore::getStatistics() const{
	std::ostringstream os;
	os << "Cache hits: " << cacheHits.load() << "\n";
	os << "Database queries: " << databaseQueries.load() << "\n";
	os << "Database scans: " << databaseScans.load() << "\n";
	return os.str();
}

const User authenticateUser(PersistentStore& store, const char* token){
	if(token==nullptr) //no token => no way of identifying a valid user
		return User{};
	return store.findUserByToken(token);
}
