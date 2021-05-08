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
///Obtain the README for an application
///\param appName the application whose documentation should be returned
crow::response fetchApplicationDocumentation(PersistentStore& store, const crow::request& req, const std::string& appName);
///Install an instance of an application
///\param appName the application to install
crow::response installApplication(PersistentStore& store, const crow::request& req, const std::string& appName);
///List all the chart versions available for an application
///\param appName the application to search for
crow::response fetchApplicationVesions(PersistentStore& store, const crow::request& req, const std::string& appName);
///Install an instance of an application from outside the catalog
crow::response installAdHocApplication(PersistentStore& store, const crow::request& req);
///Update the application catalog
crow::response updateCatalog(PersistentStore& store, const crow::request& req);

namespace internal{
	///Construct the additional set of values which should be injected into the helm template
	///when installing an application. 
	///\param cluster the cluster on which the application is to be installed
	std::string assembleExtraHelmValues(const PersistentStore& store, const Cluster& cluster, const ApplicationInstance& instance);
}

#endif //SLATE_APPLICATION_COMMANDS_H
