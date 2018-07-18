#include "VOCommands.h"

#include <boost/lexical_cast.hpp>

#include "Logging.h"
#include "Utilities.h"
#include "KubeInterface.h"

crow::response listVOs(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to list VOs");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//All users are allowed to list VOs
	
	std::vector<VO> vos=store.listVOs();

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value resultItems(rapidjson::kArrayType);
	resultItems.Reserve(vos.size(), alloc);
	for (const VO& vo : vos){
		rapidjson::Value voResult(rapidjson::kObjectType);
		voResult.AddMember("id", rapidjson::StringRef(vo.id.c_str()), alloc);
		voResult.AddMember("name", rapidjson::StringRef(vo.name.c_str()), alloc);
		resultItems.PushBack(voResult, alloc);
	}
	result.AddMember("items", resultItems, alloc);

	rapidjson::StringBuffer resultBuffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(resultBuffer);
	result.Accept(writer);
	
	return crow::response(resultBuffer.GetString());
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
		return crow::response(409,generateError("VO name is already in use"));
	
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
	
	//Create a namespace on every cluster
	auto cluster_names = store.listClusters();
	for (auto& cluster : cluster_names) {
		log_info("Creating namespace for cluster " << cluster.name);
		kubernetes::kubectl_create_namespace(*store.configPathForCluster(cluster.id), vo);
	}

	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	
	result.AddMember("apiVersion", "v1alpha1", alloc);
	result.AddMember("kind", "VO", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("id", rapidjson::StringRef(vo.id.c_str()), alloc);
	metadata.AddMember("name", rapidjson::StringRef(vo.name.c_str()), alloc);
	result.AddMember("metadata", metadata, alloc);

	rapidjson::StringBuffer resultBuffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(resultBuffer);
	result.Accept(writer);
	
	return crow::response(resultBuffer.GetString());
}

crow::response deleteVO(PersistentStore& store, const crow::request& req, const std::string& voID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to delete " << voID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//TODO: Which users are allowed to delete VOs?
	
	//TODO: deal with running instances owned by the VO?
	//TODO: what about any clusters owned by the VO?
	
	VO targetVO = store.getVO(voID);
	
	if(!targetVO)
		return crow::response(404,generateError("VO not found"));
	
	log_info("Deleting " << targetVO);
	bool deleted = store.removeVO(targetVO.id);

	if (!deleted)
		return crow::response(500, generateError("VO deletion failed"));
	
	// Remove VO namespace on each cluster
	auto cluster_names = store.listClusters();
	for (auto& cluster : cluster_names) {
		kubernetes::kubectl_delete_namespace(*store.configPathForCluster(cluster.id), targetVO);
	}
	
	return(crow::response(200));
}
