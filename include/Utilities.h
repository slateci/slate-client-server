#ifndef SLATE_UTILITIES_H
#define SLATE_UTILITIES_H

#include "crow.h"

#include "Entities.h"
#include "PersistentStore.h"

///\param store the database in which to look up the user
///\param token the proffered authentication token. May be NULL if missing.
const User authenticateUser(PersistentStore& store, const char* token);
VO validateVO(char* name);
Cluster validateCluster(char* name);
crow::json::wvalue generateError(const std::string& message);

#endif //SLATE_UTILITIES_H