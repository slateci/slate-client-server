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
crow::response deleteCluster(PersistentStore& store, const crow::request& req, const std::string& clusterID);

#endif //SLATE_CLUSTER_COMMANDS_H