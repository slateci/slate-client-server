#ifndef SLATE_APPLICATION_INSTANCE_COMMANDS_H
#define SLATE_APPLICATION_INSTANCE_COMMANDS_H

#include "crow.h"
#include "Entities.h"
#include "PersistentStore.h"

///List application instances which currently exist
crow::response listApplicationInstances(PersistentStore& store, const crow::request& req);
///Destroy an instance of an application
///\param instanceID the instance to delete
crow::response fetchApplicationInstanceInfo(PersistentStore& store, const crow::request& req, const std::string& instanceID);
///Destroy an instance of an application
///\param instanceID the instance to delete
crow::response deleteApplicationInstance(PersistentStore& store, const crow::request& req, const std::string& instanceID);

#endif //SLATE_APPLICATION_INSTANCE_COMMANDS_H