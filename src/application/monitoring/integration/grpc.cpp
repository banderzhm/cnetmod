module cnetmod.observability.grpc;

#ifdef CNETMOD_HAS_PROTOCOL_GRPC
import std;
import cnetmod.instrumentation.operation_scope;
import cnetmod.instrumentation.operation_result;

namespace cnetmod::observability {
namespace {

    /** @brief Names the four RPC call shapes without recording request payloads. */
    auto call_kind_name(std::string_view kind) noexcept -> std::string_view
    {
        return kind;
    }

    /**
     * @brief Writes the active span identity without creating another child span.
     *
     * grpc::inject_trace_context intentionally derives a child for the protocol
     * convenience overload.  An exported client span is itself that child, so
     * this adapter injects its identity directly to keep the remote parent and
     * the locally exported span identical.
     */
    void inject_active_context(grpc::metadata& headers,
        const instrumentation::trace_context& context) noexcept
    {
        try
        {
            std::erase_if(headers, [](const auto& entry)
                {
                    return entry.first == "traceparent" || entry.first == "tracestate";
                });
            const auto traceparent = instrumentation::format_traceparent(context);
            if (!traceparent.empty())
                headers.emplace("traceparent", traceparent);
            if (!context.tracestate.empty())
                headers.emplace("tracestate", context.tracestate);
        }
        catch (...)
        {
            // Propagation is optional and must not change the outgoing RPC.
        }
    }

    /** @brief Converts gRPC terminal status to SDK-neutral observation status. */
    auto classify(grpc::status_code code) noexcept -> instrumentation::operation_status
    {
        switch (code)
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

    class grpc_duration_scope
    {
    public:
        grpc_duration_scope(const instrumentation::metric_sink& sink,
            std::string_view kind, std::string_view service, std::string_view method)
            : sink_(sink), kind_(kind)
        {
            if (sink_)
            {
                // The request is moved into the raw client before the terminal
                // metric is emitted, so metric metadata must own these values.
                service_.assign(service);
                method_.assign(method);
                started_ = std::chrono::steady_clock::now();
            }
        }

        ~grpc_duration_scope()
        {
            finish(grpc::status_code::unknown);
        }

        grpc_duration_scope(const grpc_duration_scope&) = delete;
        auto operator=(const grpc_duration_scope&) -> grpc_duration_scope& = delete;

        void finish(grpc::status_code code) noexcept
        {
            if (!started_)
                return;
            const auto elapsed = std::chrono::steady_clock::now() - *started_;
            started_.reset();
            try
            {
                sink_({
                    .name = "rpc.client.duration",
                    .value = std::chrono::duration<double>(elapsed).count(),
                    .kind = instrumentation::metric_kind::histogram,
                    .unit = "s",
                    .attributes = {{"rpc.system", "grpc"},
                        {"rpc.service", std::string{service_}},
                        {"rpc.method", std::string{method_}},
                        {"rpc.grpc.call.type", std::string{kind_}},
                        {"rpc.grpc.status_code", std::to_string(static_cast<int>(code))}},
                    .explicit_bounds = {0.005, 0.01, 0.025, 0.05, 0.075, 0.1,
                        0.25, 0.5, 0.75, 1, 2.5, 5, 7.5, 10},
                });
            }
            catch (...)
            {
                // Metrics must not alter an RPC result or exception.
            }
        }

    private:
        const instrumentation::metric_sink& sink_;
        std::string_view kind_;
        std::string service_;
        std::string method_;
        std::optional<std::chrono::steady_clock::time_point> started_;
    };

    auto begin_operation(const instrumentation::span_exporter& spans,
        const instrumentation::trace_context& parent, std::string_view kind,
        std::string_view service, std::string_view method)
        -> instrumentation::operation_scope
    {
        return instrumentation::operation_scope::start(spans, [&]
            {
                return instrumentation::start_client_span(parent,
                    std::format("grpc.{} {}/{}", call_kind_name(kind), service, method),
                    {{"rpc.system", "grpc"}, {"rpc.service", std::string{service}},
                        {"rpc.method", std::string{method}},
                        {"rpc.grpc.call.type", std::string{kind}}});
            });
    }

    template <typename Response>
    void finish_operation(instrumentation::operation_scope& operation,
        grpc_duration_scope& duration,
        const std::expected<Response, grpc::status>& response) noexcept
    {
        const auto code = response ? grpc::status_code::ok : response.error().code;
        duration.finish(code);
        operation.annotate([&]
            {
                return std::vector<std::pair<std::string, std::string>>{
                    {"rpc.grpc.status_code", std::to_string(static_cast<int>(code))}};
            });
        operation.complete({classify(code), {}});
    }

} // namespace

instrumented_grpc_client::instrumented_grpc_client(grpc::client& client,
    instrumentation::span_exporter spans, instrumentation::metric_sink metrics) noexcept
    : client_(&client), spans_(std::move(spans)), metrics_(std::move(metrics))
{
}

auto instrumented_grpc_client::unary_observed(grpc::unary_request request,
    instrumentation::trace_context parent, cancel_token* cancellation)
    -> task<std::expected<grpc::unary_response, grpc::status>>
{
    grpc_duration_scope duration{metrics_, "unary", request.service, request.method};
    auto operation = begin_operation(spans_, parent, "unary", request.service, request.method);
    if (const auto* context = operation.context())
        inject_active_context(request.headers, *context);
    try
    {
        auto result = cancellation ? co_await client_->unary(std::move(request), *cancellation)
                                   : co_await client_->unary(std::move(request));
        finish_operation(operation, duration, result);
        co_return result;
    }
    catch (...)
    {
        duration.finish(grpc::status_code::unknown);
        operation.complete({instrumentation::operation_status::error, {}});
        throw;
    }
}

auto instrumented_grpc_client::client_streaming_observed(grpc::streaming_request request,
    instrumentation::trace_context parent)
    -> task<std::expected<grpc::unary_response, grpc::status>>
{
    grpc_duration_scope duration{metrics_, "client_streaming", request.service, request.method};
    auto operation = begin_operation(spans_, parent, "client_streaming", request.service, request.method);
    if (const auto* context = operation.context())
        inject_active_context(request.headers, *context);
    try
    {
        auto result = co_await client_->client_streaming(std::move(request));
        finish_operation(operation, duration, result);
        co_return result;
    }
    catch (...)
    {
        duration.finish(grpc::status_code::unknown);
        operation.complete({instrumentation::operation_status::error, {}});
        throw;
    }
}

auto instrumented_grpc_client::server_streaming_observed(grpc::unary_request request,
    instrumentation::trace_context parent)
    -> task<std::expected<grpc::streaming_response, grpc::status>>
{
    grpc_duration_scope duration{metrics_, "server_streaming", request.service, request.method};
    auto operation = begin_operation(spans_, parent, "server_streaming", request.service, request.method);
    if (const auto* context = operation.context())
        inject_active_context(request.headers, *context);
    try
    {
        auto result = co_await client_->server_streaming(std::move(request));
        finish_operation(operation, duration, result);
        co_return result;
    }
    catch (...)
    {
        duration.finish(grpc::status_code::unknown);
        operation.complete({instrumentation::operation_status::error, {}});
        throw;
    }
}

auto instrumented_grpc_client::bidi_streaming_observed(grpc::streaming_request request,
    instrumentation::trace_context parent)
    -> task<std::expected<grpc::streaming_response, grpc::status>>
{
    grpc_duration_scope duration{metrics_, "bidi_streaming", request.service, request.method};
    auto operation = begin_operation(spans_, parent, "bidi_streaming", request.service, request.method);
    if (const auto* context = operation.context())
        inject_active_context(request.headers, *context);
    try
    {
        auto result = co_await client_->bidi_streaming(std::move(request));
        finish_operation(operation, duration, result);
        co_return result;
    }
    catch (...)
    {
        duration.finish(grpc::status_code::unknown);
        operation.complete({instrumentation::operation_status::error, {}});
        throw;
    }
}

} // namespace cnetmod::observability
#endif
