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
	
	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	std::vector<crow::json::wvalue> resultItems;
	resultItems.reserve(vos.size());
	for(const VO& vo : vos){
		crow::json::wvalue voResult;
		voResult=vo.id;
		resultItems.push_back(std::move(voResult));
	}
	result["items"]=std::move(resultItems);
	return result;
}

crow::response createVO(PersistentStore& store, const crow::request& req){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to create a VO");
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//TODO: Are all users allowed to create/register VOs?
	//TODO: What other information is required to register a VO?
	
	//unpack the target user info
	crow::json::rvalue body;
	try{
		body = crow::json::load(req.body);
	}catch(std::runtime_error& err){
		return crow::response(400,generateError("Invalid JSON in request body"));
	}
	if(!body)
		return crow::response(400,generateError("Invalid JSON in request body"));
	if(!body.has("metadata"))
		return crow::response(400,generateError("Missing user metadata in request"));
	if(body["metadata"].t()!=crow::json::type::Object)
		return crow::response(400,generateError("Incorrect type for configuration"));
	
	if(!body["metadata"].has("name"))
		return crow::response(400,generateError("Missing VO name in request"));
	if(body["metadata"]["name"].t()!=crow::json::type::String)
		return crow::response(400,generateError("Incorrect type for VO name"));
	
	VO vo;
	vo.id=idGenerator.generateVOID();
	vo.name=body["metadata"]["name"].s();
	vo.valid=true;
	
	if(vo.name.find('/')!=std::string::npos)
		return crow::response(400,generateError("VO names may not contain slashes"));
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
		kubernetes::kubectl_create_namespace(cluster.name, cluster.id, vo.name);
	}

	crow::json::wvalue result;
	result["apiVersion"]="v1alpha1";
	result["kind"]="VO";
	crow::json::wvalue metadata;
	metadata["id"]=vo.id;
	metadata["name"]=vo.name;
	result["metadata"]=std::move(metadata);
	return crow::response(result);
}

crow::response deleteVO(PersistentStore& store, const crow::request& req, const std::string& voID){
	const User user=authenticateUser(store, req.url_params.get("token"));
	log_info(user << " requested to delete " << voID);
	if(!user)
		return crow::response(403,generateError("Not authorized"));
	//TODO: Which users are allowed to delete VOs?
	
	//TODO: deal with running instances owned by the VO?
	//TODO: what about any clusters owned by the VO?
	//TODO: remove VO at kubernetes level?
	
	log_info("Deleting " << voID);
	bool deleted=store.removeVO(voID);
	
	if(!deleted)
		return crow::response(500,generateError("VO deletion failed"));
	return(crow::response(200));
}
