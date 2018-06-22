#include "VOCommands.h"

#include "Logging.h"
#include "Utilities.h"

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
	
	log_info("Creating VO " << vo);
	bool created=store.addVO(vo);
	if(!created)
		return crow::response(500,generateError("VO creation failed"));
	
	//Make the creating user an initial member of the VO
	bool added=store.addUserToVO(user.id, vo.id);
	if(!added){
		//TODO: possible problem: If we get here, we may end up with a valid VO
		//but with no members and not return its ID either
		return crow::response(500,generateError("Failed to add user to new VO"));
	}
	
	//TODO: create the VO at the kubernetes level
	//Create a namespace on every cluster(?)
	
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
