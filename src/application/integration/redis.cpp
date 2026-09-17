module cnetmod.application.redis;

#ifdef CNETMOD_HAS_PROTOCOL_REDIS
import std;
import cnetmod.application.task_supervisor;
import cnetmod.coro.timer;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;

namespace cnetmod::application {

redis_service::redis_service(io_context& io, redis::pool_params options,
    std::string instance, service_requirement requirement,
    recovery_policy recovery, instrumentation::span_exporter spans)
    : io_(io), pool_(io, std::move(options)), instance_(std::move(instance)), requirement_(requirement), recovery_(recovery), spans_(std::move(spans))
{
}

auto redis_service::pool() noexcept -> redis::connection_pool&
{
    return pool_;
}

auto redis_service::make_template(redis::template_options options,
    instrumentation::trace_context parent) -> redis::redis_template
{
    return redis::redis_template{
        pool_, std::move(options), std::move(parent), spans_};
}

auto redis_service::key() const -> service_key
{
    return {"redis", instance_};
}

auto redis_service::requirement() const noexcept -> service_requirement
{
    return requirement_;
}

auto redis_service::recovery() const noexcept -> recovery_policy
{
    return recovery_;
}

auto redis_service::shutdown_required() const noexcept -> bool
{
    return started_;
}

auto redis_service::start(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    if (context.cancellation.is_cancelled())
        co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
    if (context.operation_deadline.expired())
        co_return std::unexpected(std::make_error_code(std::errc::timed_out));
    if (!started_)
    {
        auto supervised = context.supervisor.supervise(
            std::format("redis-pool:{}", instance_),
            [this](cancel_token&) -> task<std::expected<void, std::error_code>>
            {
                co_await pool_.async_run();
                co_return {};
            },
            recovery_, requirement_ == service_requirement::required,
            [this]() noexcept
            {
                pool_.request_stop();
            });
        if (!supervised)
            co_return std::unexpected(supervised.error());
        started_ = true;
    }
    auto connection = co_await pool_.async_get_connection(
        context.cancellation);
    if (!connection)
        co_return std::unexpected(connection.error());
    co_return {};
}

auto redis_service::stop(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    if (started_)
        co_await pool_.cancel();
    while (pool_.checked_out_count() != 0)
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
            co_return std::unexpected(waited.error());
    }
    started_ = false;
    co_return {};
}

auto redis_service::probe(service_context& context) -> task<health_report>
{
    if (!started_)
        co_return health_report{.status = service_health::down, .message = "redis pool stopped"};
    auto connection = co_await with_deadline(context.io, context.operation_deadline,
        pool_.async_get_connection(context.cancellation), context.cancellation);
    if (!connection)
        co_return health_report{.status = service_health::down,
            .message = "redis connection unavailable",
            .error = connection.error()};
    auto pong = co_await with_deadline(context.io, context.operation_deadline,
        connection->get().ping(context.cancellation), context.cancellation);
    co_return health_report{
        .status = pong ? service_health::up : service_health::down,
        .message = pong ? "redis PING succeeded" : "redis PING failed",
        .error = pong ? std::error_code{} : pong.error(),
    };
}

redis_cluster_service::redis_cluster_service(io_context& io,
    std::vector<redis::connect_options> seeds, std::string instance,
    service_requirement requirement, recovery_policy recovery)
    : io_(io), client_(io), seeds_(std::move(seeds)), instance_(std::move(instance)), requirement_(requirement), recovery_(recovery)
{
}

auto redis_cluster_service::client() noexcept -> redis::cluster_client&
{
    return client_;
}

auto redis_cluster_service::key() const -> service_key
{
    return {"redis", instance_};
}

auto redis_cluster_service::requirement() const noexcept
    -> service_requirement
{
    return requirement_;
}

auto redis_cluster_service::recovery() const noexcept -> recovery_policy
{
    return recovery_;
}

auto redis_cluster_service::shutdown_required() const noexcept -> bool
{
    return started_;
}

auto redis_cluster_service::start(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    if (context.cancellation.is_cancelled())
        co_return std::unexpected(
            std::make_error_code(std::errc::operation_canceled));
    if (context.operation_deadline.expired())
        co_return std::unexpected(
            std::make_error_code(std::errc::timed_out));
    if (seeds_.empty())
        co_return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));

    std::error_code last_error =
        std::make_error_code(std::errc::host_unreachable);
    for (const auto& seed : seeds_)
    {
        if (context.cancellation.is_cancelled())
            co_return std::unexpected(
                std::make_error_code(std::errc::operation_canceled));
        auto connected = co_await with_deadline(io_, context.operation_deadline,
            client_.connect(seed, context.cancellation), context.cancellation);
        if (connected)
        {
            started_ = true;
            co_return {};
        }
        last_error = connected.error();
        client_.close();
        if (last_error == std::errc::timed_out ||
            last_error == std::errc::operation_canceled)
            break;
    }
    co_return std::unexpected(last_error);
}

auto redis_cluster_service::stop(service_context&)
    -> task<std::expected<void, std::error_code>>
{
    client_.close();
    started_ = false;
    co_return {};
}

auto redis_cluster_service::probe(service_context& context)
    -> task<health_report>
{
    if (!started_)
        co_return health_report{.status = service_health::down,
            .message = "redis cluster stopped"};
    std::vector<std::string> ping{"PING"};
    auto response = co_await with_deadline(io_, context.operation_deadline,
        client_.cmd_for_key(std::move(ping), "{cnetmod-health}",
            context.cancellation),
        context.cancellation);
    const auto healthy = response && !redis::has_error(*response) &&
        redis::first_value(*response) == "PONG" &&
        client_.slots().covered_slots() == 16384U;
    co_return health_report{
        .status = healthy ? service_health::up : service_health::down,
        .message = healthy ? "redis cluster PING succeeded"
                           : "redis cluster PING failed",
        .error = response ? std::error_code{} : response.error(),
    };
}

auto auto_configure_redis(const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>
{
    if (!properties_are_known(configuration.properties, {"mode", "host", "port", "seeds", "username", "password", "database", "minimum_size", "maximum_size", "tls", "tls_verify", "tls_ca_file", "tls_cert_file", "tls_key_file", "tls_sni"}))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    if (!integer_property_in_range(configuration.properties, "port", 1, 65535))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    const auto mode = configuration.properties.value("mode", std::string{"standalone"});
    if (mode != "standalone" && mode != "cluster")
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    if (mode == "cluster")
    {
        try
        {
            const auto& value = configuration.properties;
            if (value.contains("host") || value.contains("port") ||
                value.contains("minimum_size") ||
                value.contains("maximum_size") ||
                value.value("database", 0U) != 0U ||
                !value.contains("seeds") || !value["seeds"].is_array() ||
                value["seeds"].empty())
                return std::unexpected(
                    std::make_error_code(std::errc::invalid_argument));
            std::vector<redis::connect_options> seeds;
            seeds.reserve(value["seeds"].size());
            for (const auto& entry : value["seeds"])
            {
                if (!entry.is_object() ||
                    !properties_are_known(entry, {"host", "port"}) ||
                    !entry.contains("host") || !entry["host"].is_string() ||
                    entry["host"].get_ref<const std::string&>().empty() ||
                    !integer_property_in_range(entry, "port", 1, 65535))
                    return std::unexpected(
                        std::make_error_code(std::errc::invalid_argument));
                redis::connect_options options;
                options.host = entry["host"].get<std::string>();
                options.port = entry.value("port", 6379);
                options.username = value.value("username", std::string{});
                options.password = value.value("password", std::string{});
                options.db = 0;
                options.tls = value.value("tls", false);
                options.tls_verify = value.value("tls_verify", true);
                options.tls_ca_file = value.value("tls_ca_file", std::string{});
                options.tls_cert_file = value.value("tls_cert_file", std::string{});
                options.tls_key_file = value.value("tls_key_file", std::string{});
                options.tls_sni = value.value("tls_sni", std::string{});
                seeds.push_back(std::move(options));
            }
            auto service = std::make_shared<redis_cluster_service>(context.io,
                std::move(seeds), configuration.instance,
                configuration.requirement, configuration.recovery);
            return context.services.add_managed_named<redis_cluster_service>(
                configuration.instance, std::move(service));
        }
        catch (...)
        {
            return std::unexpected(
                std::make_error_code(std::errc::invalid_argument));
        }
    }
    redis::pool_params options;
    if (configuration.properties.contains("seeds"))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    if (!pool_size_properties_are_valid(configuration.properties, options.initial_size, options.max_size))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    try
    {
        const auto& value = configuration.properties;
        options.host = value.value("host", options.host);
        options.port = value.value("port", options.port);
        options.username = value.value("username", options.username);
        options.password = value.value("password", options.password);
        options.db = value.value("database", options.db);
        options.initial_size = value.value("minimum_size", options.initial_size);
        options.max_size = value.value("maximum_size", options.max_size);
        options.tls = value.value("tls", options.tls);
        options.tls_verify = value.value("tls_verify", options.tls_verify);
        options.tls_ca_file = value.value("tls_ca_file", options.tls_ca_file);
        options.tls_cert_file = value.value(
            "tls_cert_file", options.tls_cert_file);
        options.tls_key_file = value.value("tls_key_file", options.tls_key_file);
        options.tls_sni = value.value("tls_sni", options.tls_sni);
    }
    catch (...)
    {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
    auto service = std::make_shared<redis_service>(context.io,
        std::move(options), configuration.instance,
        configuration.requirement, configuration.recovery,
        context.telemetry.spans());
    return context.services.add_managed_named<redis_service>(
        configuration.instance, std::move(service));
}

} // namespace cnetmod::application
#endif
