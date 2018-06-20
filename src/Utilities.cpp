#include "Utilities.h"
 
const User authenticateUser(PersistentStore& store, const char* token){
	if(token==nullptr) //no token => no way of identifying a valid user
		return User{};
	return store.findUserByToken(token);
}

///Check whether a VO exists with the given name
///\param the name of the VO. May be NULL if missing. 
VO validateVO(char* name){
	if(name==nullptr)
		return VO{}; //no name => not a valid VO
	//TODO: actually look up VO instead of blindly accepting all names
	return(VO{name});
}

///Check whether a cluster exists with the given name
///\param the name of the cluster. May be NULL if missing. 
Cluster validateCluster(char* name){
	if(name==nullptr)
		return Cluster{}; //no name => not a valid VO
	//TODO: actually look up cluster instead of blindly accepting all names
	return(Cluster{name});
}

crow::json::wvalue generateError(const std::string& message){
	crow::json::wvalue err;
	err["kind"]="Error";
	err["message"]=message;
	return err;
}