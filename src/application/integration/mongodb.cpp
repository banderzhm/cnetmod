module cnetmod.application.mongodb;

#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
import std;
import cnetmod.json;
import cnetmod.application.task_supervisor;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;

namespace cnetmod::application {

namespace mongodb_application_detail {
    auto lifecycle_error(mongodb::error_code code) noexcept -> std::error_code;
}

namespace {
    using mongodb_application_detail::lifecycle_error;

    auto warm_pool(mongodb::connection_pool& pool, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        auto result = co_await pool.warm_up(cancellation);
        if (!result)
            co_return std::unexpected(lifecycle_error(result.error().code));
        co_return {};
    }

    auto probe_pool(mongodb::connection_pool& pool, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        auto result = co_await pool.health_check(cancellation);
        if (!result)
            co_return std::unexpected(lifecycle_error(result.error().code));
        co_return {};
    }
} // namespace

mongodb_service::mongodb_service(io_context& io,
    mongodb::connection_pool_options options, std::string instance,
    service_requirement requirement, recovery_policy recovery)
    : options_(std::move(options)), pool_(std::make_shared<mongodb::connection_pool>(io, options_)), instance_(std::move(instance)), requirement_(requirement), recovery_(recovery)
{
}

auto mongodb_service::pool() -> mongodb::connection_pool&
{
    if (cluster_)
        throw std::logic_error("URI MongoDB service uses topology_pool(), acquire() or command()");
    return *pool_;
}

auto mongodb_service::key() const -> service_key
{
    return {"mongodb", instance_};
}

auto mongodb_service::requirement() const noexcept -> service_requirement
{
    return requirement_;
}

auto mongodb_service::recovery() const noexcept -> recovery_policy
{
    return recovery_;
}

auto mongodb_service::cleanup_required() const noexcept -> bool
{
    return cleanup_pending_;
}

auto mongodb_service::start(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    if (context.cancellation.is_cancelled())
        co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
    if (context.operation_deadline.expired())
        co_return std::unexpected(std::make_error_code(std::errc::timed_out));
    if (cleanup_pending_)
        co_return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
    if (started_)
        co_return {};
    std::expected<void, std::error_code> outcome;
    std::exception_ptr failure;
    try
    {
        outcome = co_await start_runtime(context);
    }
    catch (...)
    {
        failure = std::current_exception();
    }
    if (failure || !outcome)
    {
        /**
         * Startup owns partial resources even before maintenance registration.
         * Retain the original failure if cleanup cannot finish; the pending
         * state prevents another start until the caller retries cleanup.
         */
        cleanup_pending_ = true;
        try
        {
            (void)co_await stop(context);
        }
        catch (...)
        {
            // Cleanup remains pending; do not replace the startup failure.
        }
    }
    if (failure)
        std::rethrow_exception(failure);
    co_return outcome;
}

auto mongodb_service::start_runtime(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    if (cluster_)
        co_return co_await start_cluster(context);
    if (rebuild_pool_)
    {
        auto replacement = std::make_shared<mongodb::connection_pool>(pool_->context(), options_);
        pool_ = std::move(replacement);
        rebuild_pool_ = false;
    }
    auto warmed = co_await with_deadline(context.io, context.operation_deadline,
        warm_pool(*pool_, context.cancellation), context.cancellation);
    if (!warmed)
        co_return std::unexpected(warmed.error());
    if (context.cancellation.is_cancelled() || context.operation_deadline.expired())
    {
        /**
         * A successful late handshake still owns resources, but must not admit
         * background work after startup cancellation. Preserve the admission
         * failure while attempting cleanup with the original remaining budget.
         */
        const auto failure = std::make_error_code(context.cancellation.is_cancelled()
                ? std::errc::operation_canceled
                : std::errc::timed_out);
        co_return std::unexpected(failure);
    }
    maintenance_stop_ = std::stop_source{};
    auto supervised = context.supervisor.supervise(
        std::format("mongodb-maintenance:{}", instance_),
        [pool = pool_, stop = maintenance_stop_.get_token()](cancel_token&) -> task<std::expected<void, std::error_code>>
        {
            co_await pool->run_maintenance(stop);
            co_return {};
        },
        recovery_, requirement_ == service_requirement::required,
        [stop = maintenance_stop_]() mutable noexcept
        {
            stop.request_stop();
        });
    if (!supervised)
        co_return std::unexpected(supervised.error());
    started_ = true;
    co_return {};
}

auto mongodb_service::stop(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    if (cluster_)
        co_return co_await stop_cluster(context);
    cleanup_pending_ = true;
    maintenance_stop_.request_stop();
    co_await pool_->async_close();
    const auto maintenance_name = std::format("mongodb-maintenance:{}", instance_);
    auto maintenance_pending = [&]() noexcept
    {
        const auto state = context.supervisor.state(maintenance_name);
        return state && *state != supervised_task_state::stopped && *state != supervised_task_state::failed;
    };
    /**
     * Closing admission does not release borrower-owned transports. Keep the
     * service registered until leases return, or report an unfinished cleanup
     * that the lifecycle coordinator can retry with a fresh deadline.
     */
    while (pool_->checked_out_count() != 0 || pool_->connecting_count() != 0 || maintenance_pending())
    {
        if (context.cancellation.is_cancelled())
            co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        if (context.operation_deadline.expired())
            co_return std::unexpected(std::make_error_code(std::errc::timed_out));
        const auto waited = co_await async_timer_wait(context.io,
            std::min(context.operation_deadline.remaining(),
                std::chrono::duration_cast<deadline::duration>(std::chrono::milliseconds{1})),
            context.cancellation);
        if (!waited)
        {
            if (context.cancellation.is_cancelled())
                co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
            co_return std::unexpected(waited.error());
        }
    }
    started_ = false;
    cleanup_pending_ = false;
    rebuild_pool_ = true;
    co_return {};
}

auto mongodb_service::probe(service_context& context) -> task<health_report>
{
    if (cleanup_pending_)
        co_return health_report{
            .status = service_health::stopping,
            .message = "mongodb cleanup pending",
        };
    /**
     * Reject expired health work before it claims a pool lease or sends a ping.
     * Cancellation is local to this probe and does not retire healthy slots.
     */
    if (context.cancellation.is_cancelled() || context.operation_deadline.expired())
        co_return health_report{
            .status = service_health::down,
            .message = "mongodb health check not admitted",
            .error = std::make_error_code(context.cancellation.is_cancelled()
                    ? std::errc::operation_canceled
                    : std::errc::timed_out),
        };
    if (!started_)
        co_return health_report{.status = service_health::down, .message = "mongodb pool stopped"};
    if (cluster_)
        co_return co_await probe_cluster(context);
    auto result = co_await with_deadline(context.io, context.operation_deadline,
        probe_pool(*pool_, context.cancellation), context.cancellation);
    co_return health_report{
        .status = result ? service_health::up : service_health::down,
        .message = result ? "mongodb PING succeeded" : "mongodb PING failed",
        .error = result ? std::error_code{} : result.error(),
    };
}

} // namespace cnetmod::application
#endif
