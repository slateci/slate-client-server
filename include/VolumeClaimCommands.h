#ifndef SLATE_VOLUME_CLAIM_COMMANDS_H
#define SLATE_VOLUME_CLAIM_COMMANDS_H

#include "crow.h"
#include "Entities.h"
#include "PersistentStore.h"

///List persistent volume claims which currently exist
crow::response listVolumeClaims(PersistentStore& store, const crow::request& req);
///Fetch information about a persistent volume claim
///\param claimID the instance to query
crow::response fetchVolumeClaimInfo(PersistentStore& store, const crow::request& req, const std::string& claimID);
///Create a persistent volume claim
crow::response createVolumeClaim(PersistentStore& store, const crow::request& req);
///Destroy a persistent volume claim
///\param claimID the instance to delete
crow::response deleteVolumeClaim(PersistentStore& store, const crow::request& req, const std::string& claimID);

namespace internal{
	///Internal function which implements deletion of volume claims, 
	///assuming that all authentication, authorization, and validation of the 
	///command has already been performed
	///\param pvc the volume claim to delete
	///\param force whether to remove the volume claim from the persistent store 
	///             if deletion from the kubernetes cluster fails
	///\param reachable whether the cluster is reachable (if not, skip
	/// 			operations on the cluster)
	///\return a string describing the error which has occured, or an empty 
	///        string indicating success
	std::string deleteVolumeClaim(PersistentStore& store, const PersistentVolumeClaim& pvc, bool force, bool reachable = true);
}

#endif //SLATE_VOLUME_CLAIM_COMMANDS_H
