#ifndef SLATE_VOCOMMANDS_H
#define SLATE_VOCOMMANDS_H

#include "crow.h"
#include "Entities.h"
#include "PersistentStore.h"

///List currently VOs which exist
crow::response listVOs(PersistentStore& store, const crow::request& req);
///Register a new VO
crow::response createVO(PersistentStore& store, const crow::request& req);
///Get a VO's information
///\param voID the VO to look up
crow::response getVOInfo(PersistentStore& store, const crow::request& req, const std::string& voID);
///Change a VO's information
///\param voID the VO to update
crow::response updateVO(PersistentStore& store, const crow::request& req, const std::string& voID);
///Delete a VO
///\param voID the VO to destroy
crow::response deleteVO(PersistentStore& store, const crow::request& req, const std::string& voID);
///List the users who belong to a VO
///\param voID the VO to list
crow::response listVOMembers(PersistentStore& store, const crow::request& req, const std::string& voID);
///List the clusters owned by a VO
///\param voID the VO to list
crow::response listVOClusters(PersistentStore& store, const crow::request& req, const std::string& voID);

#endif //SLATE_VOCOMMANDS_H
