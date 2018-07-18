#ifndef SLATE_VOCOMMANDS_H
#define SLATE_VOCOMMANDS_H

#include "crow.h"
#include "Entities.h"
#include "PersistentStore.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

///List currently VOs which exist
crow::response listVOs(PersistentStore& store, const crow::request& req);
///Register a new VO
crow::response createVO(PersistentStore& store, const crow::request& req);
///Delete a VO
///\param voID the VO to destroy
crow::response deleteVO(PersistentStore& store, const crow::request& req, const std::string& voID);

#endif //SLATE_VOCOMMANDS_H
