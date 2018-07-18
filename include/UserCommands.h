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
crow::response updateUser(PersistentStore& store, const crow::request& req, const std::string uID);
crow::response deleteUser(PersistentStore& store, const crow::request& req, const std::string uID);
crow::response listUserVOs(PersistentStore& store, const crow::request& req, const std::string uID);
crow::response addUserToVO(PersistentStore& store, const crow::request& req, 
						   const std::string uID, const std::string& voID);
crow::response removeUserFromVO(PersistentStore& store, const crow::request& req, 
								const std::string uID, const std::string& voID);
crow::response findUser(PersistentStore& store, const crow::request& req);

#endif //SLATE_USER_COMMANDS_H
