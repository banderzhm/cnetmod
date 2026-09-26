module cnetmod.application.runtime;

import std;
import cnetmod.coro.cancel;
import cnetmod.protocol.http.middleware.compress;

namespace cnetmod::application {

application_runtime::application_runtime(io_context& io, thread_pool& cpu_pool,
    task_supervisor& supervisor, observability::telemetry_hub& telemetry,
    std::stop_token cancellation,
    const application_configuration& configuration) noexcept
    : io_(io), cpu_pool_(cpu_pool), supervisor_(supervisor), telemetry_(telemetry), cancellation_(cancellation), configuration_(configuration), executor_(io, cpu_pool), files_(io), rest_(io, telemetry), json_(io, cpu_pool)
{
}

auto application_runtime::spawn_managed(std::string name,
    supervised_task operation, recovery_policy recovery, bool required,
    std::function<void()> stop_request, deadline recovery_deadline)
    -> std::expected<void, std::error_code>
{
    if (stop_requested())
        return std::unexpected(
            std::make_error_code(std::errc::operation_canceled));
    return supervisor_.supervise(std::move(name), std::move(operation),
        recovery, required, std::move(stop_request), recovery_deadline);
}

auto application_runtime::executor() noexcept -> execution_context&
{
    return executor_;
}

auto application_runtime::files() noexcept -> async_file_template&
{
    return files_;
}

auto application_runtime::rest() noexcept -> rest_template&
{
    return rest_;
}

auto application_runtime::json() noexcept -> json_template&
{
    return json_;
}

auto application_runtime::configuration() const noexcept
    -> const application_configuration&
{
    return configuration_;
}

auto application_runtime::schedule_on_cpu() noexcept -> pool_post_awaitable
{
    return pool_post_awaitable{cpu_pool_};
}

auto application_runtime::resume_to_event_loop() noexcept -> post_awaitable
{
    return post_awaitable{io_};
}

auto application_runtime::compression(compress_options options)
    -> http::middleware_fn
{
    if (!options.measurements)
        options.measurements = telemetry_.measurements();
    options.dispatch = [this](compression_operation operation,
                           cancel_token& cancellation)
        -> task<std::expected<std::string, std::error_code>>
    {
        if (cancellation.is_cancelled() || stop_requested())
            co_return std::unexpected(
                std::make_error_code(std::errc::operation_canceled));
        try
        {
            auto result = co_await offload(std::move(operation));
            if (cancellation.is_cancelled() || stop_requested())
                co_return std::unexpected(
                    std::make_error_code(std::errc::operation_canceled));
            co_return result;
        }
        catch (const std::system_error& error)
        {
            co_return std::unexpected(error.code());
        }
        catch (const std::bad_alloc&)
        {
            co_return std::unexpected(
                std::make_error_code(std::errc::not_enough_memory));
        }
        catch (...)
        {
            co_return std::unexpected(
                std::make_error_code(std::errc::io_error));
        }
    };
    return cnetmod::compress(std::move(options));
}

auto application_runtime::stop_requested() const noexcept -> bool
{
    return cancellation_.stop_requested();
}

auto application_runtime::cancellation() const noexcept -> std::stop_token
{
    return cancellation_;
}

auto application_runtime::tasks() noexcept -> task_supervisor&
{
    return supervisor_;
}

auto application_runtime::telemetry() noexcept
    -> observability::telemetry_hub&
{
    return telemetry_;
}

} // namespace cnetmod::application
