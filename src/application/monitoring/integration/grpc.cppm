/**
 * @brief Optional trace and metric decoration for outbound gRPC calls.
 *
 * The adapter lives at the Application boundary.  The protocol client remains
 * usable without OpenTelemetry and empty sinks select its original task
 * directly, before a timestamp, trace context, metadata copy, or wrapper frame
 * is created.
 */
export module cnetmod.observability.grpc;

#ifdef CNETMOD_HAS_PROTOCOL_GRPC
import std;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.instrumentation.metric;
import cnetmod.instrumentation.tracing;
import cnetmod.protocol.grpc;

namespace cnetmod::observability {

/**
 * @brief Decorates every outbound gRPC call shape with independent signals.
 *
 * Parent context is explicit because gRPC calls may outlive the HTTP handler
 * that initiated them.  The wrapped client and any cancellation token must
 * outlive the returned task.
 */
export class instrumented_grpc_client
{
public:
    instrumented_grpc_client(grpc::client& client,
        instrumentation::span_exporter spans = {},
        instrumentation::metric_sink metrics = {}) noexcept;

    [[nodiscard]] inline auto unary(const grpc::unary_request& request,
        const instrumentation::trace_context& parent = {})
        -> task<std::expected<grpc::unary_response, grpc::status>>
    {
        if (!spans_ && !metrics_)
            return client_->unary(request);
        try
        {
            return unary_observed(request, parent, nullptr);
        }
        catch (...)
        {
            return client_->unary(request);
        }
    }

    [[nodiscard]] inline auto unary(grpc::unary_request&& request,
        const instrumentation::trace_context& parent = {})
        -> task<std::expected<grpc::unary_response, grpc::status>>
    {
        if (!spans_ && !metrics_)
            return client_->unary(std::move(request));
        try
        {
            return unary_observed(std::move(request), parent, nullptr);
        }
        catch (...)
        {
            return client_->unary(std::move(request));
        }
    }

    [[nodiscard]] inline auto unary(const grpc::unary_request& request,
        const instrumentation::trace_context& parent, cancel_token& cancellation)
        -> task<std::expected<grpc::unary_response, grpc::status>>
    {
        if (!spans_ && !metrics_)
            return client_->unary(request, cancellation);
        try
        {
            return unary_observed(request, parent, &cancellation);
        }
        catch (...)
        {
            return client_->unary(request, cancellation);
        }
    }

    [[nodiscard]] inline auto unary(grpc::unary_request&& request,
        const instrumentation::trace_context& parent, cancel_token& cancellation)
        -> task<std::expected<grpc::unary_response, grpc::status>>
    {
        if (!spans_ && !metrics_)
            return client_->unary(std::move(request), cancellation);
        try
        {
            return unary_observed(std::move(request), parent, &cancellation);
        }
        catch (...)
        {
            return client_->unary(std::move(request), cancellation);
        }
    }

    [[nodiscard]] inline auto client_streaming(const grpc::streaming_request& request,
        const instrumentation::trace_context& parent = {})
        -> task<std::expected<grpc::unary_response, grpc::status>>
    {
        if (!spans_ && !metrics_)
            return client_->client_streaming(request);
        try
        {
            return client_streaming_observed(request, parent);
        }
        catch (...)
        {
            return client_->client_streaming(request);
        }
    }

    [[nodiscard]] inline auto client_streaming(grpc::streaming_request&& request,
        const instrumentation::trace_context& parent = {})
        -> task<std::expected<grpc::unary_response, grpc::status>>
    {
        if (!spans_ && !metrics_)
            return client_->client_streaming(std::move(request));
        try
        {
            return client_streaming_observed(std::move(request), parent);
        }
        catch (...)
        {
            return client_->client_streaming(std::move(request));
        }
    }

    [[nodiscard]] inline auto server_streaming(const grpc::unary_request& request,
        const instrumentation::trace_context& parent = {})
        -> task<std::expected<grpc::streaming_response, grpc::status>>
    {
        if (!spans_ && !metrics_)
            return client_->server_streaming(request);
        try
        {
            return server_streaming_observed(request, parent);
        }
        catch (...)
        {
            return client_->server_streaming(request);
        }
    }

    [[nodiscard]] inline auto server_streaming(grpc::unary_request&& request,
        const instrumentation::trace_context& parent = {})
        -> task<std::expected<grpc::streaming_response, grpc::status>>
    {
        if (!spans_ && !metrics_)
            return client_->server_streaming(std::move(request));
        try
        {
            return server_streaming_observed(std::move(request), parent);
        }
        catch (...)
        {
            return client_->server_streaming(std::move(request));
        }
    }

    [[nodiscard]] inline auto bidi_streaming(const grpc::streaming_request& request,
        const instrumentation::trace_context& parent = {})
        -> task<std::expected<grpc::streaming_response, grpc::status>>
    {
        if (!spans_ && !metrics_)
            return client_->bidi_streaming(request);
        try
        {
            return bidi_streaming_observed(request, parent);
        }
        catch (...)
        {
            return client_->bidi_streaming(request);
        }
    }

    [[nodiscard]] inline auto bidi_streaming(grpc::streaming_request&& request,
        const instrumentation::trace_context& parent = {})
        -> task<std::expected<grpc::streaming_response, grpc::status>>
    {
        if (!spans_ && !metrics_)
            return client_->bidi_streaming(std::move(request));
        try
        {
            return bidi_streaming_observed(std::move(request), parent);
        }
        catch (...)
        {
            return client_->bidi_streaming(std::move(request));
        }
    }

private:
    [[nodiscard]] auto unary_observed(grpc::unary_request request,
        instrumentation::trace_context parent, cancel_token* cancellation)
        -> task<std::expected<grpc::unary_response, grpc::status>>;
    [[nodiscard]] auto client_streaming_observed(grpc::streaming_request request,
        instrumentation::trace_context parent)
        -> task<std::expected<grpc::unary_response, grpc::status>>;
    [[nodiscard]] auto server_streaming_observed(grpc::unary_request request,
        instrumentation::trace_context parent)
        -> task<std::expected<grpc::streaming_response, grpc::status>>;
    [[nodiscard]] auto bidi_streaming_observed(grpc::streaming_request request,
        instrumentation::trace_context parent)
        -> task<std::expected<grpc::streaming_response, grpc::status>>;

    grpc::client* client_;
    instrumentation::span_exporter spans_;
    instrumentation::metric_sink metrics_;
};

} // namespace cnetmod::observability
#endif
