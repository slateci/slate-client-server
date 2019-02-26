#ifndef SLATE_GroupCOMMANDS_H
#define SLATE_GroupCOMMANDS_H

#include "crow.h"
#include "Entities.h"
#include "PersistentStore.h"

///List currently groups which exist
crow::response listGroups(PersistentStore& store, const crow::request& req);
///Register a new group
crow::response createGroup(PersistentStore& store, const crow::request& req);
///Get a Group's information
///\param groupID the Group to look up
crow::response getGroupInfo(PersistentStore& store, const crow::request& req, const std::string& groupID);
///Change a Group's information
///\param groupID the Group to update
crow::response updateGroup(PersistentStore& store, const crow::request& req, const std::string& groupID);
///Delete a group
///\param groupID the Group to destroy
crow::response deleteGroup(PersistentStore& store, const crow::request& req, const std::string& groupID);
///List the users who belong to a group
///\param groupID the Group to list
crow::response listGroupMembers(PersistentStore& store, const crow::request& req, const std::string& groupID);
///List the clusters owned by a group
///\param groupID the Group to list
crow::response listGroupClusters(PersistentStore& store, const crow::request& req, const std::string& groupID);

#endif //SLATE_GroupCOMMANDS_H
