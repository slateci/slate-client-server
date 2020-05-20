#ifndef SLATE_USER_COMMANDS_H
#define SLATE_USER_COMMANDS_H

#include "crow.h"
#include "Entities.h"
#include "PersistentStore.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

crow::response listUsers(PersistentStore& store, const crow::request& req);
crow::response createUser(PersistentStore& store, const crow::request& req);
crow::response getUserInfo(PersistentStore& store, const crow::request& req, const std::string uID);
crow::response whoAreThey(PersistentStore& store, const crow::request& req);
crow::response updateUser(PersistentStore& store, const crow::request& req, const std::string uID);
crow::response deleteUser(PersistentStore& store, const crow::request& req, const std::string uID);
crow::response listUsergroups(PersistentStore& store, const crow::request& req, const std::string uID);
crow::response addUserToGroup(PersistentStore& store, const crow::request& req, 
                           const std::string uID, const std::string& groupID);
crow::response removeUserFromGroup(PersistentStore& store, const crow::request& req, 
                                const std::string uID, const std::string& groupID);
crow::response findUser(PersistentStore& store, const crow::request& req);
crow::response replaceUserToken(PersistentStore& store, const crow::request& req,
                                const std::string uID);
#endif //SLATE_USER_COMMANDS_H
