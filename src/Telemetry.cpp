//
// Created by ssthapa on 11/3/22.
//

#include <crow.h>

#include "Telemetry.h"

#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/trace/noop.h"

//Samplers
#include "opentelemetry/sdk/trace/samplers/parent.h"
#include "opentelemetry/sdk/trace/samplers/trace_id_ratio.h"

#include "opentelemetry/trace/semantic_conventions.h"

// Needed for propagating span info through http headers
#include "opentelemetry/trace/context.h"
#include "opentelemetry/context/propagation/global_propagator.h"
#include "opentelemetry/context/propagation/text_map_propagator.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"

#include "Logging.h"

namespace trace     = opentelemetry::trace;
namespace nostd     = opentelemetry::nostd;
namespace sdktrace = opentelemetry::sdk::trace;
namespace otlp      = opentelemetry::exporter::otlp;
namespace resource = opentelemetry::sdk::resource;

// template class from opentel c++ example
template <typename T>
class HttpTextMapCarrier : public opentelemetry::context::propagation::TextMapCarrier
{
public:
	HttpTextMapCarrier<T>(T &headers) : headers_(headers) {}
	HttpTextMapCarrier() = default;
	virtual opentelemetry::nostd::string_view Get(
			opentelemetry::nostd::string_view key) const noexcept override
	{
		std::string key_to_compare = key.data();
		// Header's first letter seems to be  automatically capitaliazed by our test http-server, so
		// compare accordingly.
		if (key == opentelemetry::trace::propagation::kTraceParent)
		{
			key_to_compare = "Traceparent";
		}
		else if (key == opentelemetry::trace::propagation::kTraceState)
		{
			key_to_compare = "Tracestate";
		}
		auto it = headers_.find(key_to_compare);
		if (it != headers_.end())
		{
			return it->second;
		}
		return "";
	}

	virtual void Set(opentelemetry::nostd::string_view key,
	                 opentelemetry::nostd::string_view value) noexcept override
	{
		headers_.insert(std::pair<std::string, std::string>(std::string(key), std::string(value)));
	}

	T headers_;
};

void initializeTracer(const std::string& endpoint, const resource::ResourceAttributes& resources, bool disableTracing, bool disableSampling) {

	// Configure opentelemetry otlpExporter
	otlp::OtlpHttpExporterOptions opts;
	opts.url = endpoint;
	auto OLTPExporter  = otlp::OtlpHttpExporterFactory::Create(opts);

	// configure processor
	auto processor = sdktrace::SimpleSpanProcessorFactory::Create(std::move(OLTPExporter));
	sdktrace::BatchSpanProcessorOptions options{};
	auto batchProcessor = sdktrace::BatchSpanProcessorFactory::Create(std::move(OLTPExporter), options);

	auto resource = resource::Resource::Create(resources);
	std::shared_ptr<trace::TracerProvider> provider;
	if (disableTracing) {
		log_info("Telemetry disabled, using noop tracer");
		provider = std::shared_ptr<trace::TracerProvider>(new opentelemetry::trace::NoopTracerProvider());
	} else {
		//configure sampler
		if (disableSampling) {
			log_info("Telemetry sampling disabled, sending all traces to " << endpoint);
			auto alwaysOnSampler = std::unique_ptr<sdktrace::AlwaysOnSampler>(new sdktrace::AlwaysOnSampler);
			// configure trace provider
			provider = sdktrace::TracerProviderFactory::Create(std::move(processor), resource,
			                                                   std::move(alwaysOnSampler));
		} else {
			// configure sampler to sample based on the parent span and fall back on
			// sampling 50% if there's no parent span
			log_info("Telemetry enabled and sending " << samplingRatio*100 << "% of traces to " << endpoint);
			auto ratioSampler = std::shared_ptr<sdktrace::TraceIdRatioBasedSampler>
					(new sdktrace::TraceIdRatioBasedSampler(samplingRatio));
			auto parentSampler = std::unique_ptr<sdktrace::ParentBasedSampler>
					(new sdktrace::ParentBasedSampler(ratioSampler));
			provider = sdktrace::TracerProviderFactory::Create(std::move(processor), resource,
			                                                   std::move(parentSampler));
		}
	}

	// Set the global trace provider
	trace::Provider::SetTracerProvider(provider);


}

nostd::shared_ptr<trace::Tracer> getTracer(const std::string& tracerName)
{
	auto provider = trace::Provider::GetTracerProvider();
	return provider->GetTracer(tracerName);
}

void setWebSpanAttributes(std::map<std::string, std::string>& spanAttributes, const crow::request& req) {
	spanAttributes = {
			{trace::SemanticConventions::HTTP_METHOD,    crow::method_name(req.method)},
			{trace::SemanticConventions::HTTP_SCHEME,    "http"},
			{trace::SemanticConventions::HTTP_CLIENT_IP, req.remote_endpoint}};
	auto val = req.headers.find("Host");
	if (val != req.headers.end()) {
		std::string host = val->second;
		auto commaPos = host.find(':');
		if (commaPos != std::string::npos) {
			spanAttributes[trace::SemanticConventions::NET_HOST_NAME] = host.substr(0, commaPos);
			spanAttributes[trace::SemanticConventions::NET_HOST_PORT] = host.substr(commaPos);
		} else {
			spanAttributes[trace::SemanticConventions::NET_HOST_NAME] = host;
		}
	}
	val = req.headers.find("Content-Length");
	if (val != req.headers.end()) {
		spanAttributes[trace::SemanticConventions::HTTP_REQUEST_CONTENT_LENGTH] = val->second;
	}
}

trace::StartSpanOptions getWebSpanOptions(const crow::request& req) {
	trace::StartSpanOptions spanOptions;
	spanOptions.kind = trace::SpanKind::kServer;
	auto propagator = opentelemetry::context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
	auto current_ctx = opentelemetry::context::RuntimeContext::GetCurrent();
	std::map<std::string, std::string> headers;
	for (auto it=req.headers.begin(), end = req.headers.end(); it != end; ++it) {
		headers[it->first] = it->second;
	}
	const HttpTextMapCarrier <std::map<std::string, std::string>> carrier = headers;
	auto new_context = propagator->Extract(carrier, current_ctx);
	spanOptions.parent = opentelemetry::trace::GetSpan(new_context)->GetContext();
	return spanOptions;
}

void setInternalSpanAttributes(std::map<std::string, std::string>& spanAttributes) {
	// No-op for now
	static_assert(true, "NO OP");
}

trace::StartSpanOptions getInternalSpanOptions() {
	trace::StartSpanOptions spanOptions;
	spanOptions.kind = trace::SpanKind::kServer;
	return spanOptions;
}


void populateSpan(nostd::shared_ptr<trace::Span>& span, const crow::request& req) {
	span->SetAttribute("http.method", crow::method_name(req.method));
	span->SetAttribute("http.route", req.url);
	span->SetAttribute("http.flavor", "1.1");
	span->SetAttribute("http.scheme", "http");
	span->SetAttribute("http.target", req.url);
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

void setWebSpanError(nostd::shared_ptr<trace::Span>& span, const std::string& mesg, int errorCode) {
	span->SetStatus(trace::StatusCode::kError);
	span->SetAttribute("http.status_code", errorCode);
	span->SetAttribute("log.message", mesg);
}

void setSpanError(nostd::shared_ptr<trace::Span>& span, const std::string& mesg) {
	span->SetStatus(trace::StatusCode::kError);
	span->SetAttribute("log.message", mesg);
}
