//
// Created by ssthapa on 10/27/22.
//

#ifndef SLATE_CLIENT_SERVER_TELEMETRY_H
#define SLATE_CLIENT_SERVER_TELEMETRY_H
#pragma once

#include <crow.h>

#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/provider.h"

#include "opentelemetry/context/propagation/global_propagator.h"
#include "opentelemetry/context/propagation/text_map_propagator.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"


namespace trace     = opentelemetry::trace;
namespace nostd     = opentelemetry::nostd;
namespace resource = opentelemetry::sdk::resource;

// how many traces to sample and send
const double samplingRatio = 0.5;

///Initialize opentelemetry tracing for application
///\param endpoint url to opentelemetry server endpoint
///\param resources settings used to initalize opentelemetry tracing
///\param disableTracing use no-op tracer
///\param disableSampling don't sample traces
void initializeTracer(const std::string &endpoint, const resource::ResourceAttributes &resources,
		      bool disableTracing = false, bool disableSampling = false);

///Retrieve a tracer to use
///\param tracerName name for tracer to retrieve
///\return A shared ptr to a tracer that can be used to generate spans
nostd::shared_ptr<trace::Tracer> getTracer(const std::string& tracerName = "SlateAPIServer");

///Get options to use when creating a span for web facing functions
///\param req crow request
///\return Options to use to create a web span
trace::StartSpanOptions getWebSpanOptions(const crow::request& req);

///Get attributes to use when creating a span for internal functions
///\param spanAttributes map with attributes
///\return Attributes to use to create a web span
void setInternalSpanAttributes(std::map<std::string, std::string>& spanAttributes);

///Get options to use when creating a span for internal functions
///\return Options to use to create a web span
trace::StartSpanOptions getInternalSpanOptions();

///Get attributes to use when creating a span for web facing functions
///\param spanAttributes map with attributes
///\param req crow request
///\return Attributes to use to create a web span
void setWebSpanAttributes(std::map<std::string, std::string>& spanAttributes, const crow::request& req);

///Set attributes for a span based on a crow request
///\param span span to populate with attributes
///\param req crow request
void populateSpan(nostd::shared_ptr<trace::Span>& span, const crow::request& req);

///Set error for a given web related span and add error message
///\param span span to set
///\param mesg error message to add to span
///\param errorCode http error code for span
void setWebSpanError(nostd::shared_ptr<trace::Span>& span, const std::string& mesg, int errorCode);

///Set error for a given web related span and add error message
///\param span span to set
///\param mesg error message to add to span
///\param errorCode http error code for span
void setSpanError(nostd::shared_ptr<trace::Span>& span, const std::string& mesg);


#endif //SLATE_CLIENT_SERVER_TELEMETRY_H
