#ifndef SLATE_CLUSTER_COMMANDS_H
#define SLATE_CLUSTER_COMMANDS_H

#include "crow.h"
#include "Entities.h"
#include "PersistentStore.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

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

#endif //SLATE_CLUSTER_COMMANDS_H
