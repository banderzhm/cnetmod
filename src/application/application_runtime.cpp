module cnetmod.application.runtime;

namespace cnetmod::application {

application_runtime::application_runtime(io_context& io, thread_pool& cpu_pool,
    task_supervisor& supervisor, observability::telemetry_hub& telemetry,
    std::stop_token cancellation) noexcept
    : io_(io), cpu_pool_(cpu_pool), supervisor_(supervisor), telemetry_(telemetry), cancellation_(cancellation)
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
