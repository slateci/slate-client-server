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
namespace sdktrace = opentelemetry::sdk::trace;
namespace otlp      = opentelemetry::exporter::otlp;

///Initialize opentelemetry tracing for application
///\param endpoint url to opentelemetry server endpoint
///\param resources settings used to initalize opentelemetry tracing
inline  void initializeTracer(const std::string& endpoint, const opentelemetry::sdk::resource::ResourceAttributes& resources) {

	// Configure opentelemetry otlpExporter
	opentelemetry::exporter::otlp::OtlpHttpExporterOptions opts;
	opts.url = endpoint;
	auto OLTPExporter  = otlp::OtlpHttpExporterFactory::Create(opts);

	// configure processor
	auto processor = sdktrace::SimpleSpanProcessorFactory::Create(std::move(OLTPExporter));
	sdktrace::BatchSpanProcessorOptions options{};
	auto batchProcessor = sdktrace::BatchSpanProcessorFactory::Create(std::move(OLTPExporter), options);

	//configure sampler
	auto alwaysOnSampler = std::unique_ptr<sdktrace::AlwaysOnSampler>
			(new sdktrace::AlwaysOnSampler);

	// configure trace provider
	auto resource = opentelemetry::sdk::resource::Resource::Create(resources);
	std::shared_ptr<opentelemetry::trace::TracerProvider> provider =
			sdktrace::TracerProviderFactory::Create(std::move(processor), resource, std::move(alwaysOnSampler));

	// Set the global trace provider
	trace::Provider::SetTracerProvider(provider);

}

///Retrieve a tracer to use
///\param tracerName name for tracer to retrieve
///\return A shared ptr to a tracer that can be used to generate spans
inline  opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> getTracer(const std::string& tracerName = "SlateAPIServer")
{
	auto provider = opentelemetry::trace::Provider::GetTracerProvider();
	return provider->GetTracer(tracerName);
}

///Set attributes for a span based on a crow request
///\param span span to populate with attributes
///\param req crow request
inline  void populateSpan(nostd::shared_ptr<opentelemetry::trace::Span>& span, const crow::request& req) {
	span->SetAttribute("http.method", crow::method_name(req.method));
	span->SetAttribute("http.flavor", "1.1");
	span->SetAttribute("http.scheme", "http");
	span->SetAttribute("http.target", req.url);
	span->SetAttribute("http.scheme", req.remote_endpoint);
	span->SetAttribute("http.client_ip", req.remote_endpoint);
	span->SetAttribute("net.app.protocol.name", "http");
	// get host/port
	auto val = req.headers.find("Host");
	if (val != req.headers.end()) {
		std::string host = val->second;
		auto commaPos = host.find(':');
		if (commaPos != std::string::npos) {
			span->SetAttribute("net.host.name", host.substr(0, commaPos));
			span->SetAttribute("net.host.port", host.substr(commaPos));
		} else {
			span->SetAttribute("net.host.name", host);
		}
	}
	val = req.headers.find("User-Agent");
	if (val != req.headers.end())
		span->SetAttribute("http.user_agent", val->second);
	val = req.headers.find("Content-Length");
	if (val != req.headers.end())
		span->SetAttribute("http.request_content_length", val->second);
}

///Set error for a given web related span and add error message
///\param span span to set
///\param mesg error message to add to span
///\param errorCode http error code for span
inline  void setWebSpanError(nostd::shared_ptr<opentelemetry::trace::Span>& span, const std::string& mesg, int errorCode) {
	span->SetStatus(opentelemetry::trace::StatusCode::kError);
	span->SetAttribute("http.status_code", errorCode);
	span->SetAttribute("log.message", mesg);
}

///Set error for a given web related span and add error message
///\param span span to set
///\param mesg error message to add to span
///\param errorCode http error code for span
inline void setSpanError(nostd::shared_ptr<opentelemetry::trace::Span>& span, const std::string& mesg) {
	span->SetStatus(opentelemetry::trace::StatusCode::kError);
	span->SetAttribute("log.message", mesg);
}

#endif //SLATE_CLIENT_SERVER_TELEMETRY_H