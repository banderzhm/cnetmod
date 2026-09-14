/**
 * @brief Optional HTTP server measurement middleware without tracing dependencies.
 */
export module cnetmod.observability.http_server;

import std;
import cnetmod.instrumentation.metric;
import cnetmod.protocol.http;

namespace cnetmod::observability {

/**
 * @brief Observes handler duration and outcome independently of tracing.
 * @return An empty middleware when the sink is disabled; do not install it.
 *
 * Sink failures are isolated. Handler exceptions propagate unchanged. Raw
 * paths, query strings, request headers and bodies are never metric labels.
 */
export [[nodiscard]] auto server_metrics(instrumentation::metric_sink sink)
    -> http::middleware_fn;

} // namespace cnetmod::observability
