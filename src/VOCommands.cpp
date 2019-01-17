#include "VOCommands.h"

#include <boost/lexical_cast.hpp>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "Logging.h"
#include "Utilities.h"
#include "KubeInterface.h"
#include "ApplicationInstanceCommands.h"
#include "ClusterCommands.h"
#include "SecretCommands.h"

crow::response listVOs(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list VOs");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list VOs

	std::vector<VO> vos;

	if (req.url_params.get("user"))
		vos=store.listVOsForUser(user.id);
	else
		vos=store.listVOs();

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(vos.size(), alloc);
	for (const VO& vo : vos){
		rapidjson::Value metadata(rapidjson::kObjectType);
	        metadata.AddMember("id", rapidjson::StringRef(vo.id.c_str()), alloc);
		metadata.AddMember("name", rapidjson::StringRef(vo.name.c_str()), alloc);

		rapidjson::Value voResult(rapidjson::kObjectType);
		voResult.AddMember("apiVersion", "v1alpha1", alloc);
		voResult.AddMember("kind", "VO", alloc);
		voResult.AddMember("metadata", metadata, alloc);
		resultItems.PushBack(voResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);
	
	return crow::response(to_string(result));
}

crow::response createVO(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to create a VO");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//TODO: Are all users allowed to create/register VOs?
	//TODO: What other information is required to register a VO?
	
	//unpack the target user info
	rapidjson::Document body;
	try{
		body.Parse(req.body.c_str());
	}catch(std::runtime_error& err){
		return crow::response(400,generateError("Invalid JSON in request body"));
	}

	if(body.IsNull())
		return crow::response(400,generateError("Invalid JSON in request body"));
	if(!body.HasMember("metadata"))
		return crow::response(400,generateError("Missing user metadata in request"));
	if(!body["metadata"].IsObject())
		return crow::response(400,generateError("Incorrect type for configuration"));
	
	if(!body["metadata"].HasMember("name"))
		return crow::response(400,generateError("Missing VO name in request"));
	if(!body["metadata"]["name"].IsString())
		return crow::response(400,generateError("Incorrect type for VO name"));
	
	VO vo;
	vo.id=idGenerator.generateVOID();
	vo.name=body["metadata"]["name"].GetString();
	vo.valid=true;
	
	if(vo.name.empty())
		return crow::response(400,generateError("VO names may not be the empty string"));
	if(vo.name.find_first_not_of("abcdefghijklmnopqrstuvwxzy0123456789-")!=std::string::npos)
		return crow::response(400,generateError("VO names may only contain [a-z], [0-9] and -"));
	if(vo.name.back()=='-')
		return crow::response(400,generateError("VO names may not end with a dash"));
	if(vo.name.size()>54)
		return crow::response(400,generateError("VO names may not be more than 54 characters long"));
	if(vo.name.find(IDGenerator::voIDPrefix)==0)
		return crow::response(400,generateError("VO names may not begin with "+IDGenerator::voIDPrefix));
	if(store.findVOByName(vo.name))
		return crow::response(400,generateError("VO name is already in use"));
	
	log_info("Creating VO " << vo);
	bool created=store.addVO(vo);
	if(!created)
		return crow::response(500,generateError("VO creation failed"));
	
	//Make the creating user an initial member of the VO
	bool added=store.addUserToVO(user.id, vo.id);
	if(!added){
		//TODO: possible problem: If we get here, we may end up with a valid VO
		//but with no members and not return its ID either
		auto problem="Failed to add creating user "+
		             boost::lexical_cast<std::string>(user)+" to new VO "+
		             boost::lexical_cast<std::string>(vo);
		log_error(problem);
		return crow::response(500,generateError(problem));
	}
	
	log_info("Created " << vo << " on behalf of " << user);

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	result.AddMember("kind", "VO", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", rapidjson::StringRef(vo.id.c_str()), alloc);
	metadata.AddMember("name", rapidjson::StringRef(vo.name.c_str()), alloc);
	result.AddMember("metadata", metadata, alloc);
	
	return crow::response(to_string(result));
}

crow::response deleteVO(PersistentStore& store, const crow::request& req, const std::string& voID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to delete " << voID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//Only admins and members of a VO can delete it
	if(!user.admin && !store.userInVO(user.id,voID))
		return crow::response(403,generateError("Not authorized"));
	
	VO targetVO = store.getVO(voID);
	
	if(!targetVO)
		return crow::response(404,generateError("VO not found"));
	
	log_info("Deleting " << targetVO);
	bool deleted = store.removeVO(targetVO.id);

	if (!deleted)
		return crow::response(500, generateError("VO deletion failed"));
	
	// Remove all instances owned by the VO
	for(auto& instance : store.listApplicationInstancesByClusterOrVO(targetVO.id,""))
		internal::deleteApplicationInstance(store,instance,true);
	
	// Remove all secrets owned by the VO
	for(auto& secret : store.listSecrets(targetVO.id,""))
		internal::deleteSecret(store,secret,true);
	
	// Remove the VO's namespace on each cluster
	auto cluster_names = store.listClusters();
	for (auto& cluster : cluster_names){
		try{
			kubernetes::kubectl_delete_namespace(*store.configPathForCluster(cluster.id), targetVO);
		}
		catch(std::runtime_error& err){
			log_error("Failed to delete " << targetVO << " namespace from " << cluster << ": " << err.what());
		}
	}
	
	// Remove all clusters owned by the VO
	for(auto& cluster : cluster_names){
		if(cluster.owningVO==targetVO.id)
			internal::deleteCluster(store,cluster,true);
	}
	
	return(crow::response(200));
}

crow::response listVOMembers(PersistentStore& store, const crow::request& req, const std::string& voID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list members of " << voID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	VO targetVO = store.getVO(voID);
	if(!targetVO)
		return crow::response(404,generateError("VO not found"));
	//Only admins and members of a VO can list its members
	if(!user.admin && !store.userInVO(user.id,targetVO.id))
		return crow::response(403,generateError("Not authorized"));
	
	auto userIDs=store.getMembersOfVO(targetVO.id);
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(userIDs.size(), alloc);
	for(const std::string& userID : userIDs){
		User user=store.getUser(userID);
		rapidjson::Value userResult(rapidjson::kObjectType);
		userResult.AddMember("apiVersion", "v1alpha1", alloc);
		userResult.AddMember("kind", "User", alloc);
		rapidjson::Value userData(rapidjson::kObjectType);
		userData.AddMember("id", user.id, alloc);
		userData.AddMember("name", user.name, alloc);
		userData.AddMember("email", user.email, alloc);
		userResult.AddMember("metadata", userData, alloc);
		resultItems.PushBack(userResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);
	
	return crow::response(to_string(result));
}

crow::response listVOClusters(PersistentStore& store, const crow::request& req, const std::string& voID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list clusters owned by " << voID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	
	VO targetVO = store.getVO(voID);
	if(!targetVO)
		return crow::response(404,generateError("VO not found"));
	//anyone can list a VO's clusters?
	
	auto clusterIDs=store.clustersOwnedByVO(targetVO.id);
	
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(clusterIDs.size(), alloc);
	for(const std::string& clusterID : clusterIDs){
		Cluster cluster=store.getCluster(clusterID);
		rapidjson::Value clusterResult(rapidjson::kObjectType);
		clusterResult.AddMember("apiVersion", "v1alpha1", alloc);
		clusterResult.AddMember("kind", "Cluster", alloc);
		rapidjson::Value clusterData(rapidjson::kObjectType);
		clusterData.AddMember("id", cluster.id, alloc);
		clusterData.AddMember("name", cluster.name, alloc);
		clusterData.AddMember("owningVO", targetVO.name, alloc);
		clusterResult.AddMember("metadata", clusterData, alloc);
		resultItems.PushBack(clusterResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);

	return crow::response(to_string(result));
}
