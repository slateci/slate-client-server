#ifndef SLATE_SECRET_COMMANDS_H
#define SLATE_SECRET_COMMANDS_H

#include "crow.h"
#include "Entities.h"
#include "PersistentStore.h"

///List installed secrets
crow::response listSecrets(PersistentStore& store, const crow::request& req);

///Install a secret
crow::response createSecret(PersistentStore& store, const crow::request& req);

///Destroy a secret
crow::response deleteSecret(PersistentStore& store, const crow::request& req,
                            const std::string& secretID);

///Fetch the contents of a secret
crow::response getSecret(PersistentStore& store, const crow::request& req,
                         const std::string& secretID);

namespace internal{
	///Internal function which implements deletion of secrets, 
	///assuming that all authentication, authorization, and validation of the 
	///command has already been performed
	///\param secret the secret to delete
	///\param force whether to remove the secret from the persistent store 
	///             if deletion from the kubernetes cluster fails
	///\return a string describing the error which has occured, or an empty 
	///        string indicating success
	std::string deleteSecret(PersistentStore& store, const Secret& secret, bool force);
}

#endif //SLATE_SECRET_COMMANDS_H