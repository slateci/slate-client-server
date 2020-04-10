#ifndef SLATE_MONITORING_CREDENTIAL_COMMANDS_H
#define SLATE_MONITORING_CREDENTIAL_COMMANDS_H

#include "crow.h"
#include "Entities.h"
#include "PersistentStore.h"

///List all currently tracked monitoring credentials
crow::response listMonitoringCredentials(PersistentStore& store, const crow::request& req);

///Insert a new monitoring credential, which will be available for allocation
crow::response addMonitoringCredential(PersistentStore& store, const crow::request& req);

///Mark a credential revoked, removing it from use by any cluster to which is 
///currently assigned and preventing it from being assigned to any other cluster
crow::response revokeMonitoringCredential(PersistentStore& store, const crow::request& req,
                                          const std::string& credentialID);

///Remove a credential record, which must currently be in the revoked state
crow::response deleteMonitoringCredential(PersistentStore& store, const crow::request& req,
                                          const std::string& credentialID);

#endif //SLATE_MONITORING_CREDENTIAL_COMMANDS_H