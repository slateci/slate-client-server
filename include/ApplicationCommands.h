#ifndef SLATE_APPLICATION_COMMANDS_H
#define SLATE_APPLICATION_COMMANDS_H

#include "crow.h"
#include "Entities.h"
#include "PersistentStore.h"

///List currently known applications
crow::response listApplications(PersistentStore& store, const crow::request& req);
///Register a new application
crow::response createApplication(PersistentStore& store, const crow::request& req);
///Obtain the configuration for an application
///\param appID the application whose configuration should be returned
crow::response fetchApplicationConfig(PersistentStore& store, const crow::request& req, const std::string& appID);
///Install and instance of an application
///\param appID the application to install
crow::response installApplication(PersistentStore& store, const crow::request& req, const std::string& appID);
///Change an existing application
///\param appID the application to alter
crow::response updateApplication(PersistentStore& store, const crow::request& req, const std::string& appID);
///Destroy an application
///\param appID the application to remove
crow::response deleteApplication(PersistentStore& store, const crow::request& req, const std::string& appIS);

#endif //SLATE_APPLICATION_COMMANDS_H