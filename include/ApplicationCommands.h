#ifndef SLATE_APPLICATION_COMMANDS_H
#define SLATE_APPLICATION_COMMANDS_H

#include "crow.h"
#include "Entities.h"
#include "PersistentStore.h"

///List currently known applications
crow::response listApplications(PersistentStore& store, const crow::request& req);
///Obtain the configuration for an application
///\param appName the application whose configuration should be returned
crow::response fetchApplicationConfig(PersistentStore& store, const crow::request& req, const std::string& appName);
///Install and instance of an application
///\param appName the application to install
crow::response installApplication(PersistentStore& store, const crow::request& req, const std::string& appName);

#endif //SLATE_APPLICATION_COMMANDS_H
