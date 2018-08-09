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

#endif //SLATE_SECRET_COMMANDS_H