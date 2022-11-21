#include "server_version.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"

#include "ServerUtilities.h"
#include "Telemetry.h"


crow::response serverVersionInfo(){
	auto tracer = getTracer();
	auto span = tracer->StartSpan("/version");
	auto scope = tracer->WithActiveSpan(span);
	rapidjson::Document result(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = result.GetAllocator();
	result.AddMember("serverVersion", serverVersionString, alloc);
	rapidjson::Value apiVersions(rapidjson::kArrayType);
	rapidjson::Value currentAPI(rapidjson::kStringType);
	currentAPI.SetString("v1alpha3");
	apiVersions.PushBack(currentAPI,alloc);
	result.AddMember("supportedAPIVersions", apiVersions, alloc);
	span->End();
	return crow::response(to_string(result));
}
