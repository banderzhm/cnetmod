module cnetmod.application.mongodb;
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
import std;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.executor.pool;
import cnetmod.executor.async_op;
import cnetmod.application.task_supervisor;
import cnetmod.integration.mongodb_cloud;
    #include "mongodb_cluster_runtime.hpp"

namespace cnetmod::application {
namespace {
    using mongodb_application_detail::lifecycle_error;

    auto prepare_cluster(std::shared_ptr<mongodb_cluster_runtime> runtime, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        // Resolve SRV/TXT before creating pools. The owned blocking executor and
        // runtime stay alive until DNS/file work has returned to the owner loop.
        auto configuration = co_await mongodb::resolve_connection_string(runtime->io,
            runtime->blocking_pool, runtime->options.uri, runtime->options.srv_lookup);
        if (!configuration)
            co_return std::unexpected(lifecycle_error(configuration.error().code));
        if (cancellation.is_cancelled())
            co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        auto& pool = configuration->topology.per_server_pool;
        if (runtime->options.minimum_size)
            pool.minimum_size = *runtime->options.minimum_size;
        if (runtime->options.maximum_size)
            pool.maximum_size = *runtime->options.maximum_size;
        if (pool.maximum_size == 0 || pool.minimum_size > pool.maximum_size)
            co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        // Typed application overrides install custom credential callbacks or TLS
        // material; default AWS/OIDC providers fill only unconfigured providers.
        if (runtime->options.configure_connection)
        {
            auto configured = runtime->options.configure_connection(pool.connection);
            if (!configured)
                co_return std::unexpected(lifecycle_error(configured.error().code));
        }
        auto credentials = mongodb::cloud::configure_cloud_credentials(pool.connection,
            runtime->io, runtime->blocking_pool);
        if (!credentials)
            co_return std::unexpected(lifecycle_error(credentials.error().code));
        runtime->selection = configuration->selection;
        runtime->database = pool.connection.database;
        runtime->pool = std::make_shared<mongodb::topology_connection_pool>(runtime->io,
            std::move(configuration->topology));
        // Authenticated warm-up is required: a monitor hello alone cannot prove
        // that credentials work, even when the configured minimum is zero.
        auto warmed = co_await runtime->pool->warm_up(cancellation, runtime->selection);
        if (!warmed)
            co_return std::unexpected(lifecycle_error(warmed.error().code));
        co_return {};
    }

    auto check_cluster(std::shared_ptr<mongodb_cluster_runtime> runtime, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        auto checked = co_await runtime->pool->health_check(cancellation, runtime->selection);
        if (!checked)
            co_return std::unexpected(lifecycle_error(checked.error().code));
        co_return {};
    }
} // namespace

auto mongodb_service::start_cluster(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    if (cluster_->options.heartbeat_frequency < std::chrono::milliseconds{500})
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    auto prepared = co_await with_deadline(context.io, context.operation_deadline,
        prepare_cluster(cluster_, context.cancellation), context.cancellation);
    if (!prepared)
        co_return std::unexpected(prepared.error());
    // Late success must never admit a background monitor after cancellation.
    if (context.cancellation.is_cancelled() || context.operation_deadline.expired())
        co_return std::unexpected(std::make_error_code(context.cancellation.is_cancelled()
                ? std::errc::operation_canceled
                : std::errc::timed_out));
    maintenance_stop_ = std::stop_source{};
    auto supervised = context.supervisor.supervise(
        std::format("mongodb-maintenance:{}", instance_),
        [runtime = cluster_, stop = maintenance_stop_.get_token()](cancel_token&)
            -> task<std::expected<void, std::error_code>>
        {
            co_await runtime->pool->run_monitoring(stop, runtime->options.heartbeat_frequency);
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

auto mongodb_service::stop_cluster(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    // Close admission before joining the monitor and leases. Pending shutdown
    // retains the runtime, so callers may return leases and retry with a budget.
    cleanup_pending_ = true;
    maintenance_stop_.request_stop();
    if (cluster_->pool)
        co_await cluster_->pool->async_close();
    const auto name = std::format("mongodb-maintenance:{}", instance_);
    for (;;)
    {
        const auto task_state = context.supervisor.state(name);
        const bool monitoring = task_state && *task_state != supervised_task_state::stopped &&
            *task_state != supervised_task_state::failed;
        const auto statistics = cluster_->pool ? cluster_->pool->statistics() : mongodb::topology_connection_pool_statistics{};
        if (!monitoring && cluster_->active_operations == 0 && statistics.checked_out_connection_count == 0 && statistics.connecting_count == 0)
            break;
        if (context.cancellation.is_cancelled() || context.operation_deadline.expired())
            co_return std::unexpected(std::make_error_code(context.cancellation.is_cancelled()
                    ? std::errc::operation_canceled
                    : std::errc::timed_out));
        auto waited = co_await async_timer_wait(context.io,
            std::min(context.operation_deadline.remaining(),
                std::chrono::duration_cast<deadline::duration>(std::chrono::milliseconds{1})),
            context.cancellation);
        if (!waited)
            co_return std::unexpected(waited.error());
    }
    started_ = false;
    cleanup_pending_ = false;
    co_return {};
}

auto mongodb_service::probe_cluster(service_context& context) -> task<health_report>
{
    mongodb_operation_guard active{cluster_};
    auto checked = co_await with_deadline(context.io, context.operation_deadline,
        check_cluster(cluster_, context.cancellation), context.cancellation);
    co_return health_report{
        .status = checked ? service_health::up : service_health::down,
        .message = checked ? "mongodb topology PING succeeded" : "mongodb topology PING failed",
        .error = checked ? std::error_code{} : checked.error(),
    };
}
} // namespace cnetmod::application
#endif
