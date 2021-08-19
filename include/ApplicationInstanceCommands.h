#ifndef SLATE_APPLICATION_INSTANCE_COMMANDS_H
#define SLATE_APPLICATION_INSTANCE_COMMANDS_H

#include "crow.h"
#include "Entities.h"
#include "PersistentStore.h"

///List application instances which currently exist
crow::response listApplicationInstances(PersistentStore& store, const crow::request& req);
///Destroy an instance of an application
///\param instanceID the instance to query
crow::response fetchApplicationInstanceInfo(PersistentStore& store, const crow::request& req, const std::string& instanceID);
///Stop and restart an instance of an application
///\param instanceID the instance to restart
crow::response restartApplicationInstance(PersistentStore& store, const crow::request& req, const std::string& instanceID);
///Stop and restart an instance of an application applying an updated configuration file
///\param instanceID the instance to restart
crow::response updateApplicationInstance(PersistentStore& store, const crow::request& req, const std::string& instanceID);
///Destroy an instance of an application
///\param instanceID the instance to delete
crow::response deleteApplicationInstance(PersistentStore& store, const crow::request& req, const std::string& instanceID);
///Fetch logs for an instance of an application
///\param instanceID the instance for which to get logs
crow::response getApplicationInstanceLogs(PersistentStore& store, 
                                          const crow::request& req, 
                                          const std::string& instanceID);
///Get the current number of replicas in an instance
crow::response getApplicationInstanceScale(PersistentStore& store, const crow::request& req, const std::string& instanceID);
///Scale a given instance to N replicas
crow::response scaleApplicationInstance(PersistentStore& store, const crow::request& req, const std::string& instanceID);

namespace internal{
	///Internal function which implements deletion of application instances, 
	///assuming that all authentication, authorization, and validation of the 
	///command has already been performed
	///\param instance the instance to delete
	///\param force whether to remove the instance from the persistent store 
	///             if deletion from the kubernetes cluster fails
	///\return a string describing the error which has occured, or an empty 
	///        string indicating success
	std::string deleteApplicationInstance(PersistentStore& store, const ApplicationInstance& instance, bool force);
	std::string deleteApplicationInstanceFromStore(PersistentStore& store, const ApplicationInstance& instance, bool force);
}

#endif //SLATE_APPLICATION_INSTANCE_COMMANDS_H