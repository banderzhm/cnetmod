module cnetmod.observability.http;

import std;
import cnetmod.instrumentation.error;
import cnetmod.instrumentation.metric;
import cnetmod.instrumentation.operation_scope;
import cnetmod.instrumentation.operation_result;

namespace cnetmod::observability {
namespace {

    /**
     * @brief Records one terminal duration without requiring a trace identity.
     */
    class http_duration_scope
    {
    public:
        http_duration_scope(const instrumentation::metric_sink& sink, http::http_method method)
            : sink_(sink), method_(method)
        {
            if (sink_)
                started_ = std::chrono::steady_clock::now();
        }

        ~http_duration_scope()
        {
            finish(0, {}, "abandoned");
        }

        http_duration_scope(const http_duration_scope&) = delete;
        auto operator=(const http_duration_scope&) -> http_duration_scope& = delete;

        void finish(int status, std::error_code error, std::string_view failure = {}) noexcept
        {
            if (!started_)
                return;
            const auto elapsed = std::chrono::steady_clock::now() - *started_;
            started_.reset();
            try
            {
                instrumentation::metric_measurement metric{
                    .name = "http.client.request.duration",
                    .value = std::chrono::duration<double>(elapsed).count(),
                    .kind = instrumentation::metric_kind::histogram,
                    .unit = "s",
                    .attributes = {{"http.request.method", std::string{http::method_to_string(method_)}}},
                    .explicit_bounds = {0.005, 0.01, 0.025, 0.05, 0.075, 0.1, 0.25, 0.5, 0.75, 1, 2.5, 5, 7.5, 10}};
                if (status != 0)
                    metric.attributes.emplace_back("http.response.status_code", std::to_string(status));
                if (!failure.empty())
                    metric.attributes.emplace_back("error.type", failure);
                else if (error)
                {
                    const auto outcome = instrumentation::classify_error(error);
                    metric.attributes.emplace_back("error.type",
                        outcome.status == instrumentation::operation_status::timeout         ? "timeout"
                            : outcome.status == instrumentation::operation_status::cancelled ? "cancelled"
                                                                                             : error.category().name());
                }
                else if (status >= 400)
                    metric.attributes.emplace_back("error.type", std::to_string(status));
                sink_(std::move(metric));
            }
            catch (...)
            {
                // Metrics must not replace an HTTP response or exception.
            }
        }

    private:
        const instrumentation::metric_sink& sink_;
        http::http_method method_;
        std::optional<std::chrono::steady_clock::time_point> started_;
    };

    auto begin_http_span(const http::request& request,
        const http::tracing::trace_context& parent)
        -> http::tracing::active_span
    {
        return http::tracing::start_client_span(parent,
            std::string{http::method_to_string(request.method())});
    }

    void complete_http_operation(instrumentation::operation_scope& operation,
        const std::expected<http::response, std::error_code>& result) noexcept
    {
        if (result)
        {
            operation.annotate([&]
                {
                    return std::vector<std::pair<std::string, std::string>>{
                        {"http.response.status_code", std::to_string(result->status_code())}};
                });
        }
        auto outcome = instrumentation::operation_result{};
        if (!result)
        {
            outcome = instrumentation::classify_error(result.error());
        }
        else if (result->status_code() >= 400)
            outcome.status = instrumentation::operation_status::error;
        operation.complete(outcome);
    }

} // namespace

instrumented_http_client::instrumented_http_client(http::client& client,
    http::tracing::span_exporter exporter, instrumentation::metric_sink metrics) noexcept
    : client_(&client), exporter_(std::move(exporter)), metrics_(std::move(metrics))
{
}

auto instrumented_http_client::send(const http::request& request,
    const http::tracing::trace_context& parent)
    -> task<std::expected<http::response, std::error_code>>
{
    if (!exporter_ && !metrics_)
        return client_->send(request);
    try
    {
        return send_observed(request, exporter_ ? parent : http::tracing::trace_context{}, nullptr);
    }
    catch (...)
    {
        return client_->send(request);
    }
}

auto instrumented_http_client::send(const http::request& request,
    const http::tracing::trace_context& parent, cancel_token& cancellation)
    -> task<std::expected<http::response, std::error_code>>
{
    if (!exporter_ && !metrics_)
        return client_->send(request, cancellation);
    try
    {
        return send_observed(request, exporter_ ? parent : http::tracing::trace_context{}, &cancellation);
    }
    catch (...)
    {
        return client_->send(request, cancellation);
    }
}

auto instrumented_http_client::send_observed(const http::request& request,
    http::tracing::trace_context parent, cancel_token* cancellation)
    -> task<std::expected<http::response, std::error_code>>
{
    http_duration_scope duration{metrics_, request.method()};
    auto operation = instrumentation::operation_scope::start(exporter_, [&]
        {
            return begin_http_span(request, parent);
        });
    operation.annotate([&]
        {
            return std::vector<std::pair<std::string, std::string>>{
                {"http.request.method", std::string{http::method_to_string(request.method())}}};
        });
    std::optional<http::request> propagated;
    if (operation.context())
    {
        try
        {
            propagated.emplace(request);
            http::tracing::inject(*propagated, *operation.context());
        }
        catch (...)
        {
            propagated.reset();
        }
    }
    const auto& outgoing = propagated ? *propagated : request;
    try
    {
        auto result = cancellation ? co_await client_->send(outgoing, *cancellation)
                                   : co_await client_->send(outgoing);
        duration.finish(result ? result->status_code() : 0, result ? std::error_code{} : result.error());
        complete_http_operation(operation, result);
        co_return result;
    }
    catch (const std::system_error& error)
    {
        const auto code = error.code();
        duration.finish(0, code);
        operation.complete(instrumentation::classify_error(code));
        throw;
    }
    catch (...)
    {
        duration.finish(0, {}, "exception");
        operation.complete({instrumentation::operation_status::error, {}});
        throw;
    }
}

} // namespace cnetmod::observability
