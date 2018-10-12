#ifndef SLATE_CLUSTER_COMMANDS_H
#define SLATE_CLUSTER_COMMANDS_H

#include "crow.h"
#include "Entities.h"
#include "PersistentStore.h"

///List currently known clusters
crow::response listClusters(PersistentStore& store, const crow::request& req);
///Register a new cluster
crow::response createCluster(PersistentStore& store, const crow::request& req);
///Delete a cluster
///\param clusterID the cluster to destroy
crow::response deleteCluster(PersistentStore& store, const crow::request& req, 
                             const std::string& clusterID);
///Update a cluster's information
///\param clusterID the cluster to update
crow::response updateCluster(PersistentStore& store, const crow::request& req, 
                             const std::string& clusterID);
///List VOs authorized to use a cluster
///\param clusterID the cluster to check
crow::response listClusterAllowedVOs(PersistentStore& store, const crow::request& req, 
                                     const std::string& clusterID);
///Give a VO access to a cluster
///\param clusterID the cluster to which to give access
///\param voID the VO for which to grant access
crow::response grantVOClusterAccess(PersistentStore& store, const crow::request& req, 
                                    const std::string& clusterID, const std::string& voID);
///Take away a VO's access to a cluster
///\param clusterID the cluster to which to remove access
///\param voID the VO for which to revoke access
crow::response revokeVOClusterAccess(PersistentStore& store, const crow::request& req, 
                                     const std::string& clusterID, const std::string& voID);

///List applications which a VO may use on a given cluster
///\param clusterID the cluster for which to check
///\param voID the VO for which to check
crow::response listClusterVOAllowedApplications(PersistentStore& store, 
                                                const crow::request& req, 
                                                const std::string& clusterID, 
                                                const std::string& voID);

///Grant a VO permission to use a particular application on a given cluster
///\param clusterID the cluster on which to allow use of the application
///\param voID the VO to which to grant permission
///\param applicationName the application for which to grant permission
crow::response allowVOUseOfApplication(PersistentStore& store, const crow::request& req, 
                                       const std::string& clusterID, const std::string& voID,
                                       const std::string& applicationName);

///Revoke a VO's permission to use a particular application on a given cluster
///\param clusterID the cluster on which to deny use of the application
///\param voID the VO from which to revoke permission
///\param applicationName the application for which to revoke permission
crow::response denyVOUseOfApplication(PersistentStore& store, const crow::request& req, 
                                      const std::string& clusterID, const std::string& voID,
                                      const std::string& applicationName);


crow::response verifyCluster(PersistentStore& store, const crow::request& req,
                             const std::string& clusterID);

#endif //SLATE_CLUSTER_COMMANDS_H
