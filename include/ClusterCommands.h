#ifndef SLATE_CLUSTER_COMMANDS_H
#define SLATE_CLUSTER_COMMANDS_H

#include "crow.h"
#include "Entities.h"
#include "PersistentStore.h"

///List currently known clusters
crow::response listClusters(PersistentStore& store, const crow::request& req);
///Register a new cluster
crow::response createCluster(PersistentStore& store, const crow::request& req);
///Get a cluster's information
///\param clusterID the cluster to look up
crow::response getClusterInfo(PersistentStore& store, const crow::request& req,
                              const std::string clusterID);
///Delete a cluster
///\param clusterID the cluster to destroy
crow::response deleteCluster(PersistentStore& store, const crow::request& req, 
                             const std::string& clusterID);
///Update a cluster's information
///\param clusterID the cluster to update
crow::response updateCluster(PersistentStore& store, const crow::request& req, 
                             const std::string& clusterID);
///List groups authorized to use a cluster
///\param clusterID the cluster to check
crow::response listClusterAllowedgroups(PersistentStore& store, const crow::request& req, 
                                        const std::string& clusterID);
///Check whether a group has access to a cluster
///\param clusterID the cluster to which to check access
///\param groupID the Group for which to check access
crow::response checkGroupClusterAccess(PersistentStore& store, const crow::request& req, 
									   const std::string& clusterID, const std::string& groupID);
///Give a Group access to a cluster
///\param clusterID the cluster to which to give access
///\param groupID the Group for which to grant access
crow::response grantGroupClusterAccess(PersistentStore& store, const crow::request& req, 
                                    const std::string& clusterID, const std::string& groupID);
///Take away a Group's access to a cluster
///\param clusterID the cluster to which to remove access
///\param groupID the Group for which to revoke access
crow::response revokeGroupClusterAccess(PersistentStore& store, const crow::request& req, 
                                     const std::string& clusterID, const std::string& groupID);

///List applications which a Group may use on a given cluster
///\param clusterID the cluster for which to check
///\param groupID the Group for which to check
crow::response listClusterGroupAllowedApplications(PersistentStore& store, 
                                                const crow::request& req, 
                                                const std::string& clusterID, 
                                                const std::string& groupID);

///Grant a Group permission to use a particular application on a given cluster
///\param clusterID the cluster on which to allow use of the application
///\param groupID the Group to which to grant permission
///\param applicationName the application for which to grant permission
crow::response allowGroupUseOfApplication(PersistentStore& store, const crow::request& req, 
                                       const std::string& clusterID, const std::string& groupID,
                                       const std::string& applicationName);

///Revoke a Group's permission to use a particular application on a given cluster
///\param clusterID the cluster on which to deny use of the application
///\param groupID the Group from which to revoke permission
///\param applicationName the application for which to revoke permission
crow::response denyGroupUseOfApplication(PersistentStore& store, const crow::request& req, 
                                      const std::string& clusterID, const std::string& groupID,
                                      const std::string& applicationName);


///Determine whether a particular cluster can be reached with kubectl
///\param clusterID the cluster to attempt to contact
crow::response pingCluster(PersistentStore& store, const crow::request& req,
                           const std::string& clusterID);

///Fetch the S3 credential assigned for the given cluster to use for storing 
///monitoring data. If no credential is assigned, attempts to assign one. 
///\param clusterID the cluster for which to get or assign the credential
crow::response getClusterMonitoringCredential(PersistentStore& store, 
                                              const crow::request& req,
                                              const std::string& clusterID);

///Revoke the S3 credential assigned for the given cluster to use for storing 
///monitoring data. The credential is both unassigned form the cluster and 
///marked revoked to prevent it being assigned to any other cluster. 
///\param clusterID the cluster for which to get or assign the credential
crow::response removeClusterMonitoringCredential(PersistentStore& store, 
                                                 const crow::request& req,
                                                 const std::string& clusterID);

crow::response verifyCluster(PersistentStore& store, const crow::request& req,
                             const std::string& clusterID);

namespace internal{
	///Internal function which implements deletion of clusters, 
	///assuming that all authentication, authorization, and validation of the 
	///command has already been performed
	///\param cluster the cluster to delete
	///\param force whether to remove the cluster from the persistent store 
	///             even if contacting it with kubectl fails
	///\return a string describing the error which has occured, or an empty 
	///        string indicating success
	std::string deleteCluster(PersistentStore& store, const Cluster& cluster, bool force);
}

#endif //SLATE_CLUSTER_COMMANDS_H
