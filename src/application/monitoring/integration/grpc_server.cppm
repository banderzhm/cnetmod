/**
 * @brief Optional RPC-semantic observation for gRPC server HTTP handlers.
 */
export module cnetmod.observability.grpc_server;

#ifdef CNETMOD_HAS_PROTOCOL_GRPC
import std;
import cnetmod.instrumentation.metric;
import cnetmod.instrumentation.tracing;
import cnetmod.protocol.grpc;
import cnetmod.protocol.http;

namespace cnetmod::observability {

/**
 * @brief Adds gRPC server spans and duration metrics around a raw router handler.
 *
 * Empty sinks return @p handler itself, so an application with telemetry
 * disabled does not install another middleware coroutine or parse RPC metadata.
 */
export [[nodiscard]] auto grpc_server_handler(http::handler_fn handler,
    instrumentation::span_exporter spans = {},
    instrumentation::metric_sink metrics = {}) -> http::handler_fn;

} // namespace cnetmod::observability
#endif
