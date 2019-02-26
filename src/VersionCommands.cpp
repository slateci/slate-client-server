#include "server_version.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "Logging.h"
#include "ServerUtilities.h"

crow::response serverVersionInfo(){
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	result.AddMember("serverVersion", serverVersionString, alloc);
	rapidjson::Value apiVersions(rapidjson::kArrayType);
	rapidjson::Value currentAPI(rapidjson::kStringType);
	currentAPI.SetString("v1alpha3");
	apiVersions.PushBack(currentAPI,alloc);
	result.AddMember("supportedAPIVersions", apiVersions, alloc);
	return crow::response(to_string(result));
}
