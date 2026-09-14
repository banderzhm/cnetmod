module cnetmod.observability.grpc_server;

#ifdef CNETMOD_HAS_PROTOCOL_GRPC
import std;
import cnetmod.instrumentation.operation_scope;
import cnetmod.instrumentation.operation_result;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.tracing;

namespace cnetmod::observability {
namespace {

    /** @brief Keeps one RPC duration measurement alive across route suspension. */
    class grpc_server_duration_scope
    {
    public:
        grpc_server_duration_scope(const instrumentation::metric_sink& sink,
            std::string_view service, std::string_view method)
            : sink_(sink), service_(service), method_(method)
        {
            if (sink_)
                started_ = std::chrono::steady_clock::now();
        }

        ~grpc_server_duration_scope()
        {
            finish(grpc::status_code::unknown);
        }

        grpc_server_duration_scope(const grpc_server_duration_scope&) = delete;
        auto operator=(const grpc_server_duration_scope&) -> grpc_server_duration_scope& = delete;

        void finish(grpc::status_code status) noexcept
        {
            if (!started_)
                return;
            const auto elapsed = std::chrono::steady_clock::now() - *started_;
            started_.reset();
            try
            {
                sink_({
                    .name = "rpc.server.duration",
                    .value = std::chrono::duration<double>(elapsed).count(),
                    .kind = instrumentation::metric_kind::histogram,
                    .unit = "s",
                    .attributes = {{"rpc.system", "grpc"},
                        {"rpc.service", std::string{service_}},
                        {"rpc.method", std::string{method_}},
                        {"rpc.grpc.status_code", std::to_string(static_cast<int>(status))}},
                    .explicit_bounds = {0.005, 0.01, 0.025, 0.05, 0.075, 0.1,
                        0.25, 0.5, 0.75, 1, 2.5, 5, 7.5, 10},
                });
            }
            catch (...)
            {
                // Metrics are observational and never replace a gRPC response.
            }
        }

    private:
        const instrumentation::metric_sink& sink_;
        std::string_view service_;
        std::string_view method_;
        std::optional<std::chrono::steady_clock::time_point> started_;
    };

    auto status_from_response(const http::response& response) noexcept -> grpc::status_code
    {
        try
        {
            const auto found = response.trailers().find("grpc-status");
            if (found == response.trailers().end())
                return response.status_code() >= 500 ? grpc::status_code::internal
                                                     : grpc::status_code::unknown;
            int parsed{};
            const auto text = std::string_view{found->second};
            const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
                parsed < static_cast<int>(grpc::status_code::ok) ||
                parsed > static_cast<int>(grpc::status_code::unauthenticated))
                return grpc::status_code::unknown;
            return static_cast<grpc::status_code>(parsed);
        }
        catch (...)
        {
            return grpc::status_code::unknown;
        }
    }

    auto classify(grpc::status_code status) noexcept -> instrumentation::operation_status
    {
        switch (status)
        {
        case grpc::status_code::ok:
            return instrumentation::operation_status::success;
        case grpc::status_code::cancelled:
            return instrumentation::operation_status::cancelled;
        case grpc::status_code::deadline_exceeded:
            return instrumentation::operation_status::timeout;
        default:
            return instrumentation::operation_status::error;
        }
    }

    auto server_parent(const http::request_context& request)
        -> instrumentation::trace_context
    {
        if (const auto current = http::tracing::context_from(request))
            return *current;
        if (const auto extracted = instrumentation::parse_traceparent(
                request.get_header("traceparent"), request.get_header("tracestate")))
            return *extracted;
        return instrumentation::new_root_context();
    }

    auto observe_grpc_server(http::handler_fn handler,
        instrumentation::span_exporter spans, instrumentation::metric_sink metrics,
        http::request_context& request) -> task<void>
    {
        const auto parsed = grpc::parse_service_path(request.path());
        const auto service = parsed ? parsed->first : std::string{"_unknown"};
        const auto method = parsed ? parsed->second : std::string{"_unknown"};
        grpc_server_duration_scope duration{metrics, service, method};
        auto operation = instrumentation::operation_scope::start(spans, [&]
            {
                return instrumentation::start_server_span(server_parent(request),
                    std::format("grpc.server {}/{}", service, method),
                    {{"rpc.system", "grpc"}, {"rpc.service", service},
                        {"rpc.method", method}});
            });
        try
        {
            co_await handler(request);
            const auto status = status_from_response(request.resp());
            duration.finish(status);
            operation.annotate([&]
                {
                    return std::vector<std::pair<std::string, std::string>>{
                        {"rpc.grpc.status_code", std::to_string(static_cast<int>(status))}};
                });
            operation.complete({classify(status), {}});
        }
        catch (...)
        {
            duration.finish(grpc::status_code::unknown);
            operation.complete({instrumentation::operation_status::error, {}});
            throw;
        }
    }

} // namespace

auto grpc_server_handler(http::handler_fn handler,
    instrumentation::span_exporter spans, instrumentation::metric_sink metrics) -> http::handler_fn
{
    if (!handler || (!spans && !metrics))
        return handler;
    return [handler = std::move(handler), spans = std::move(spans), metrics = std::move(metrics)](
               http::request_context& request) -> task<void>
    {
        return observe_grpc_server(handler, spans, metrics, request);
    };
}

} // namespace cnetmod::observability
#endif
