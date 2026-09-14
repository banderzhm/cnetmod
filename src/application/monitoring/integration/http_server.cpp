module cnetmod.observability.http_server;

import std;
import cnetmod.instrumentation.error;
import cnetmod.instrumentation.operation_result;
import cnetmod.instrumentation.metric;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod::observability {
namespace {

    /**
     * @brief Owns one server measurement across coroutine suspension.
     */
    class server_duration_scope
    {
    public:
        server_duration_scope(const instrumentation::metric_sink& sink, http::request_context& request)
            : sink_(sink), request_(request), started_(std::chrono::steady_clock::now())
        {
        }

        server_duration_scope(const server_duration_scope&) = delete;
        auto operator=(const server_duration_scope&) -> server_duration_scope& = delete;

        ~server_duration_scope()
        {
            finish("abandoned");
        }

        void finish(std::string_view failure = {}) noexcept
        {
            if (finished_)
                return;
            finished_ = true;
            const auto elapsed = std::chrono::steady_clock::now() - started_;
            try
            {
                constexpr std::array<std::string_view, 9> methods{
                    "GET", "HEAD", "POST", "PUT", "DELETE", "CONNECT", "OPTIONS", "TRACE", "PATCH"};
                const auto method = std::ranges::find(methods, request_.method()) != methods.end()
                    ? request_.method()
                    : std::string_view{"_OTHER"};
                instrumentation::metric_measurement measurement{
                    .name = "http.server.request.duration",
                    .value = std::chrono::duration<double>(elapsed).count(),
                    .kind = instrumentation::metric_kind::histogram,
                    .unit = "s",
                    .attributes = {{"http.request.method", std::string{method}}},
                    .explicit_bounds = {0.005, 0.01, 0.025, 0.05, 0.075, 0.1, 0.25, 0.5, 0.75, 1, 2.5, 5, 7.5, 10}};
                if (!failure.empty())
                    measurement.attributes.emplace_back("error.type", failure);
                else
                {
                    const int status = request_.resp().status_code();
                    measurement.attributes.emplace_back("http.response.status_code", std::to_string(status));
                    if (status >= 500)
                        measurement.attributes.emplace_back("error.type", std::to_string(status));
                }
                sink_(std::move(measurement));
            }
            catch (...)
            {
                // Telemetry failures never replace handler results or exceptions.
            }
        }

    private:
        const instrumentation::metric_sink& sink_;
        http::request_context& request_;
        std::chrono::steady_clock::time_point started_;
        bool finished_{};
    };

} // namespace

auto server_metrics(instrumentation::metric_sink sink) -> http::middleware_fn
{
    if (!sink)
        return {};
    return [sink = std::move(sink)](http::request_context& request, http::next_fn next) -> task<void>
    {
        server_duration_scope measurement{sink, request};
        try
        {
            co_await next();
        }
        catch (const std::system_error& error)
        {
            const auto code = error.code();
            const auto outcome = instrumentation::classify_error(code);
            measurement.finish(outcome.status == instrumentation::operation_status::cancelled ? "cancelled"
                    : outcome.status == instrumentation::operation_status::timeout            ? "timeout"
                                                                                              : code.category().name());
            throw;
        }
        catch (...)
        {
            measurement.finish("exception");
            throw;
        }
        measurement.finish();
    };
}

} // namespace cnetmod::observability
