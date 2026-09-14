module cnetmod.protocol.http.middleware.tracing;

import std;
import cnetmod.instrumentation.error;
import cnetmod.coro.task;
import cnetmod.instrumentation.tracing;
import cnetmod.instrumentation.operation_result;

namespace cnetmod::http::tracing {
namespace {
    void report_span(const tracing_options& options, const trace_context& context,
        std::string_view method, std::string_view path, int status,
        std::chrono::steady_clock::duration elapsed, bool has_remote_parent,
        std::string_view parent_span_id,
        std::chrono::system_clock::time_point started_at,
        instrumentation::operation_result result) noexcept
    {
        if (!options.on_end || (context.flags & 1U) == 0)
            return;
        try
        {
            options.on_end(completed_span{
                .context = context,
                .method = std::string(method),
                .path = std::string(path),
                .status_code = status,
                .elapsed = elapsed,
                .has_remote_parent = has_remote_parent,
                .failed = result.status == instrumentation::operation_status::error ||
                    result.status == instrumentation::operation_status::timeout ||
                    result.status == instrumentation::operation_status::abandoned,
                .parent_span_id = std::string(parent_span_id),
                .started_at = started_at,
                .ended_at = std::chrono::system_clock::now(),
                .kind = span_kind::server,
                .result = result,
            });
        }
        catch (...)
        {
            // Instrumentation must never fail the user request.
        }
    }

    /**
     * @brief Reports exactly one terminal outcome for an active server span.
     *
     * Borrowed request/context state belongs to the enclosing coroutine. Its
     * destruction reports abandonment rather than inventing a successful HTTP
     * status when an unfinished handler is destroyed.
     */
    class server_span_scope
    {
    public:
        server_span_scope(const tracing_options& options, request_context& request,
            const trace_context& context, const std::optional<trace_context>& parent) noexcept
            : options_(options), request_(request), context_(context), parent_(parent) {}

        ~server_span_scope()
        {
            complete({instrumentation::operation_status::abandoned, {}}, 0);
        }

        server_span_scope(const server_span_scope&) = delete;
        auto operator=(const server_span_scope&) -> server_span_scope& = delete;

        void complete(instrumentation::operation_result result, int status) noexcept
        {
            if (std::exchange(completed_, true))
                return;
            report_span(options_, context_, request_.method(), request_.path(), status,
                std::chrono::steady_clock::now() - started_, parent_.has_value(),
                parent_ ? parent_->span_id : std::string_view{}, started_at_, result);
        }

    private:
        const tracing_options& options_;
        request_context& request_;
        const trace_context& context_;
        const std::optional<trace_context>& parent_;
        std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
        std::chrono::system_clock::time_point started_at_ = std::chrono::system_clock::now();
        bool completed_ = false;
    };

    auto run_traced_request(const tracing_options& options, request_context& request,
        next_fn next) -> task<void>
    {
        std::optional<trace_context> parent;
        std::optional<trace_context> context;
        try
        {
            const auto incoming_state = options.accept_tracestate
                ? request.get_header("tracestate")
                : std::string_view{};
            parent = parse_traceparent(request.get_header("traceparent"), incoming_state);
            context = parent ? child_context(*parent) : new_root_context();
            options.on_end.sample(*context, parent.has_value());
            request.set_trace_context(context->trace_id, context->span_id, context->flags,
                context->tracestate);
        }
        catch (...)
        {
            context.reset();
        }
        if (!context)
        {
            co_await next();
            co_return;
        }
        if (options.emit_response_traceparent)
        {
            try
            {
                request.resp().set_header("traceparent", format_traceparent(*context));
            }
            catch (...)
            {
                // Response propagation is optional and must not reject the route.
            }
        }

        server_span_scope scope{options, request, *context, parent};
        try
        {
            co_await next();
        }
        catch (const std::system_error& error)
        {
            const auto code = error.code();
            scope.complete(instrumentation::classify_error(code), 0);
            throw;
        }
        catch (...)
        {
            scope.complete({instrumentation::operation_status::error, {}}, 0);
            throw;
        }
        const auto status = request.resp().status_code();
        scope.complete({status >= 500 ? instrumentation::operation_status::error
                                      : instrumentation::operation_status::success,
                           {}},
            status);
    }

} // namespace

void inject(request& destination, const trace_context& context)
{
    const auto header = format_traceparent(context);
    if (header.empty())
        return;
    destination.set_header("traceparent", header);
    if (context.tracestate.empty())
        destination.remove_header("tracestate");
    else
        destination.set_header("tracestate", context.tracestate);
}

auto context_from(const request_context& request) -> std::optional<trace_context>
{
    trace_context context{
        .trace_id = std::string(request.trace_id()),
        .span_id = std::string(request.trace_span_id()),
        .flags = request.trace_flags(),
        .tracestate = std::string(request.trace_state()),
    };
    return instrumentation::valid_trace_context(context) ? std::optional{std::move(context)} : std::nullopt;
}

auto tracing_middleware(tracing_options options) -> middleware_fn
{
    if (!options.on_end)
        return {};
    return [options = std::move(options)](
               request_context& request, next_fn next) -> task<void>
    {
        return run_traced_request(options, request, std::move(next));
    };
}

} // namespace cnetmod::http::tracing
