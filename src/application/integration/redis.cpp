module cnetmod.application.redis;

#ifdef CNETMOD_HAS_PROTOCOL_REDIS
import std;
import cnetmod.json;
import cnetmod.application.task_supervisor;
import cnetmod.coro.timer;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;

namespace cnetmod::application {

namespace {

    auto verify_redis_shard(redis::sharded_connection_pool& pool,
        io_context& event_loop, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        auto connection = co_await pool.async_get_connection(
            event_loop, cancellation);
        if (!connection)
            co_return std::unexpected(connection.error());
        co_return {};
    }

    auto probe_redis_shard(redis::sharded_connection_pool& pool,
        io_context& event_loop, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        auto connection = co_await pool.async_get_connection(
            event_loop, cancellation);
        if (!connection)
            co_return std::unexpected(connection.error());
        co_return co_await connection->get().ping(cancellation);
    }

    auto connect_cluster_client(redis::cluster_client& client,
        const std::vector<redis::connect_options>& seeds,
        cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        std::error_code last_error =
            std::make_error_code(std::errc::host_unreachable);
        for (const auto& seed : seeds)
        {
            if (cancellation.is_cancelled())
                co_return std::unexpected(std::make_error_code(
                    std::errc::operation_canceled));
            auto connected = co_await client.connect(seed, cancellation);
            if (connected)
                co_return {};
            last_error = connected.error();
            client.close();
        }
        co_return std::unexpected(last_error);
    }

    auto close_cluster_client(redis::cluster_client& client) -> task<void>
    {
        client.close();
        co_return;
    }

    auto probe_cluster_client(redis::cluster_client& client,
        cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        std::vector<std::string> ping{"PING"};
        auto response = co_await client.cmd_for_key(std::move(ping),
            "{cnetmod-health}", cancellation);
        if (!response)
            co_return std::unexpected(response.error());
        if (redis::has_error(*response) ||
            redis::first_value(*response) != "PONG" ||
            client.slots().covered_slots() != 16384U)
            co_return std::unexpected(
                std::make_error_code(std::errc::protocol_error));
        co_return {};
    }

} // namespace

redis_service::redis_service(io_context& io, redis::pool_params options,
    std::string instance, service_requirement requirement,
    recovery_policy recovery, instrumentation::span_exporter spans)
    : io_(io), pool_(std::make_unique<redis::connection_pool>(io, std::move(options))), instance_(std::move(instance)), requirement_(requirement), recovery_(recovery), spans_(std::move(spans))
{
}

redis_service::redis_service(io_context& control,
    std::span<io_context* const> event_loops, redis::pool_params options,
    std::string instance, service_requirement requirement,
    recovery_policy recovery, instrumentation::span_exporter spans)
    : io_(control),
      sharded_pool_(std::make_unique<redis::sharded_connection_pool>(
          std::vector<io_context*>{event_loops.begin(), event_loops.end()},
          std::move(options))),
      event_loops_(event_loops.begin(), event_loops.end()),
      instance_(std::move(instance)), requirement_(requirement),
      recovery_(recovery), spans_(std::move(spans))
{
}

auto redis_service::pool() -> redis::connection_pool&
{
    if (!pool_)
        throw std::logic_error{
            "redis_service::pool() is unavailable in multi-loop mode; use make_template()"};
    return *pool_;
}

auto redis_service::make_template(redis::template_options options,
    instrumentation::trace_context parent) -> redis::redis_template
{
    if (pool_)
        return redis::redis_template{
            *pool_, std::move(options), std::move(parent), spans_, &io_};
    return redis::redis_template{
        *sharded_pool_, std::move(options), std::move(parent), spans_, &io_};
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
                if (pool_)
                    co_await pool_->async_run();
                else
                    co_await resume_on(io_, sharded_pool_->async_run());
                co_return {};
            },
            recovery_, requirement_ == service_requirement::required,
            [this]() noexcept
            {
                if (pool_)
                    pool_->request_stop();
                else
                    sharded_pool_->request_stop();
            });
        if (!supervised)
            co_return std::unexpected(supervised.error());
        started_ = true;
    }
    if (pool_)
    {
        auto connection = co_await pool_->async_get_connection(
            context.cancellation);
        if (!connection)
            co_return std::unexpected(connection.error());
    }
    else
    {
        auto verified = co_await resume_on(context.io,
            starts_on(*event_loops_.front(), verify_redis_shard(
                *sharded_pool_, *event_loops_.front(),
                context.cancellation)));
        if (!verified)
            co_return std::unexpected(verified.error());
    }
    co_return {};
}

auto redis_service::stop(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    if (started_)
    {
        if (pool_)
            co_await pool_->cancel();
        else
            co_await sharded_pool_->cancel();
    }
    while ((pool_ ? pool_->checked_out_count()
                  : sharded_pool_->checked_out_count()) != 0)
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
    std::expected<void, std::error_code> pong;
    if (pool_)
    {
        auto connection = co_await with_deadline(context.io,
            context.operation_deadline,
            pool_->async_get_connection(context.cancellation),
            context.cancellation);
        if (!connection)
            co_return health_report{.status = service_health::down,
                .message = "redis connection unavailable",
                .error = connection.error()};
        pong = co_await with_deadline(context.io,
            context.operation_deadline,
            connection->get().ping(context.cancellation),
            context.cancellation);
    }
    else
    {
        pong = co_await with_deadline(context.io,
            context.operation_deadline,
            resume_on(context.io, starts_on(*event_loops_.front(),
                probe_redis_shard(*sharded_pool_, *event_loops_.front(),
                    context.cancellation))),
            context.cancellation);
    }
    co_return health_report{
        .status = pong ? service_health::up : service_health::down,
        .message = pong ? "redis PING succeeded" : "redis PING failed",
        .error = pong ? std::error_code{} : pong.error(),
    };
}

redis_cluster_service::redis_cluster_service(io_context& io,
    std::vector<redis::connect_options> seeds, std::string instance,
    service_requirement requirement, recovery_policy recovery)
    : io_(io), event_loops_{&io}, seeds_(std::move(seeds)),
      instance_(std::move(instance)), requirement_(requirement),
      recovery_(recovery)
{
    clients_.push_back(std::make_unique<redis::cluster_client>(io));
}

redis_cluster_service::redis_cluster_service(io_context& control,
    std::span<io_context* const> event_loops,
    std::vector<redis::connect_options> seeds, std::string instance,
    service_requirement requirement, recovery_policy recovery)
    : io_(control), event_loops_(event_loops.begin(), event_loops.end()),
      seeds_(std::move(seeds)), instance_(std::move(instance)),
      requirement_(requirement), recovery_(recovery)
{
    clients_.reserve(event_loops_.size());
    for (auto* event_loop : event_loops_)
        clients_.push_back(
            std::make_unique<redis::cluster_client>(*event_loop));
}

auto redis_cluster_service::client() -> redis::cluster_client&
{
    if (auto* current = io_context::current())
        for (std::size_t index = 0; index < event_loops_.size(); ++index)
            if (event_loops_[index] == current)
                return *clients_[index];
    throw std::logic_error{
        "redis cluster client requested outside an owning event loop"};
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

    for (std::size_t index = 0; index < clients_.size(); ++index)
    {
        auto connected = co_await with_deadline(context.io,
            context.operation_deadline,
            resume_on(context.io, starts_on(*event_loops_[index],
                connect_cluster_client(*clients_[index], seeds_,
                    context.cancellation))),
            context.cancellation);
        if (!connected)
        {
            for (std::size_t opened = 0; opened < index; ++opened)
                co_await resume_on(context.io,
                    starts_on(*event_loops_[opened],
                        close_cluster_client(*clients_[opened])));
            co_return std::unexpected(connected.error());
        }
    }
    started_ = true;
    co_return {};
}

auto redis_cluster_service::stop(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    for (std::size_t index = 0; index < clients_.size(); ++index)
        co_await resume_on(context.io, starts_on(*event_loops_[index],
            close_cluster_client(*clients_[index])));
    started_ = false;
    co_return {};
}

auto redis_cluster_service::probe(service_context& context)
    -> task<health_report>
{
    if (!started_)
        co_return health_report{.status = service_health::down,
            .message = "redis cluster stopped"};
    std::size_t healthy_count = 0;
    std::error_code last_error;
    for (std::size_t index = 0; index < clients_.size(); ++index)
    {
        auto response = co_await with_deadline(context.io,
            context.operation_deadline,
            resume_on(context.io, starts_on(*event_loops_[index],
                probe_cluster_client(*clients_[index],
                    context.cancellation))),
            context.cancellation);
        if (response)
            ++healthy_count;
        else
            last_error = response.error();
    }
    const auto healthy = healthy_count == clients_.size();
    co_return health_report{
        .status = healthy ? service_health::up : service_health::down,
        .message = healthy ? "redis cluster PING succeeded"
                           : "redis cluster PING failed",
        .error = healthy ? std::error_code{} : last_error,
    };
}

auto auto_configure_redis(const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>
{
    if (!properties_are_known(configuration.properties, {"mode", "host", "port", "seeds", "username", "password", "database", "minimum_size", "maximum_size", "connect_timeout_ms", "pool_timeout_ms", "retry_interval_ms", "ping_interval_ms", "ping_timeout_ms", "tls", "tls_verify", "tls_ca_file", "tls_cert_file", "tls_key_file", "tls_sni"}))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    if (!integer_property_in_range(configuration.properties, "port", 1, 65535))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    const auto mode = cnetmod::json::value_or(
        configuration.properties, "mode", std::string{"standalone"});
    if (mode != "standalone" && mode != "cluster")
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    constexpr auto maximum_timeout_ms = std::int64_t{86'400'000};
    for (const auto name : {"connect_timeout_ms", "pool_timeout_ms",
             "retry_interval_ms", "ping_interval_ms", "ping_timeout_ms"})
    {
        if (!integer_property_in_range(configuration.properties, name, 1,
                maximum_timeout_ms))
            return std::unexpected(
                std::make_error_code(std::errc::invalid_argument));
    }
    if (mode == "cluster")
    {
        try
        {
            const auto& value = configuration.properties;
            if (value.contains("host") || value.contains("port") ||
                value.contains("minimum_size") ||
                value.contains("maximum_size") ||
                value.contains("connect_timeout_ms") ||
                value.contains("pool_timeout_ms") ||
                value.contains("retry_interval_ms") ||
                value.contains("ping_interval_ms") ||
                value.contains("ping_timeout_ms") ||
                cnetmod::json::value_or(value, "database", 0U) != 0U ||
                !value.contains("seeds") || !value["seeds"].is_array() ||
                value["seeds"].empty())
                return std::unexpected(
                    std::make_error_code(std::errc::invalid_argument));
            std::vector<redis::connect_options> seeds;
            seeds.reserve(value["seeds"].size());
            for (const auto& entry : value["seeds"].get_array())
            {
                if (!entry.is_object() ||
                    !properties_are_known(entry, {"host", "port"}) ||
                    !entry.contains("host") || !entry["host"].is_string() ||
                    entry["host"].get<std::string>().empty() ||
                    !integer_property_in_range(entry, "port", 1, 65535))
                    return std::unexpected(
                        std::make_error_code(std::errc::invalid_argument));
                redis::connect_options options;
                options.host = entry["host"].get<std::string>();
                options.port = cnetmod::json::value_or(entry, "port", 6379);
                options.username = cnetmod::json::value_or(value, "username", std::string{});
                options.password = cnetmod::json::value_or(value, "password", std::string{});
                options.db = 0;
                options.tls = cnetmod::json::value_or(value, "tls", false);
                options.tls_verify = cnetmod::json::value_or(value, "tls_verify", true);
                options.tls_ca_file = cnetmod::json::value_or(value, "tls_ca_file", std::string{});
                options.tls_cert_file = cnetmod::json::value_or(value, "tls_cert_file", std::string{});
                options.tls_key_file = cnetmod::json::value_or(value, "tls_key_file", std::string{});
                options.tls_sni = cnetmod::json::value_or(value, "tls_sni", std::string{});
                seeds.push_back(std::move(options));
            }
            std::shared_ptr<redis_cluster_service> service;
            if (context.event_loops.size() > 1)
                service = std::make_shared<redis_cluster_service>(context.io,
                    context.event_loops, std::move(seeds),
                    configuration.instance, configuration.requirement,
                    configuration.recovery);
            else
                service = std::make_shared<redis_cluster_service>(context.io,
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
        options.host = cnetmod::json::value_or(value, "host", options.host);
        options.port = cnetmod::json::value_or(value, "port", options.port);
        options.username = cnetmod::json::value_or(value, "username", options.username);
        options.password = cnetmod::json::value_or(value, "password", options.password);
        options.db = cnetmod::json::value_or(value, "database", options.db);
        options.initial_size = cnetmod::json::value_or(value, "minimum_size", options.initial_size);
        options.max_size = cnetmod::json::value_or(value, "maximum_size", options.max_size);
        options.tls = cnetmod::json::value_or(value, "tls", options.tls);
        options.tls_verify = cnetmod::json::value_or(value, "tls_verify", options.tls_verify);
        options.tls_ca_file = cnetmod::json::value_or(value, "tls_ca_file", options.tls_ca_file);
        options.tls_cert_file = cnetmod::json::value_or(value,
            "tls_cert_file", options.tls_cert_file);
        options.tls_key_file = cnetmod::json::value_or(value, "tls_key_file", options.tls_key_file);
        options.tls_sni = cnetmod::json::value_or(value, "tls_sni", options.tls_sni);
        const auto duration = [&value](std::string_view name,
                                  std::chrono::steady_clock::duration fallback)
        {
            if (!value.contains(name))
                return fallback;
            return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::milliseconds{value.at(name).as<std::int64_t>()});
        };
        options.connect_timeout = duration("connect_timeout_ms", options.connect_timeout);
        options.pool_timeout = duration("pool_timeout_ms", options.pool_timeout);
        options.retry_interval = duration("retry_interval_ms", options.retry_interval);
        options.ping_interval = duration("ping_interval_ms", options.ping_interval);
        options.ping_timeout = duration("ping_timeout_ms", options.ping_timeout);
    }
    catch (...)
    {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
    std::shared_ptr<redis_service> service;
    if (context.event_loops.size() > 1)
        service = std::make_shared<redis_service>(context.io,
            context.event_loops, std::move(options), configuration.instance,
            configuration.requirement, configuration.recovery,
            context.telemetry.spans());
    else
        service = std::make_shared<redis_service>(context.io,
            std::move(options), configuration.instance,
            configuration.requirement, configuration.recovery,
            context.telemetry.spans());
    return context.services.add_managed_named<redis_service>(
        configuration.instance, std::move(service));
}

} // namespace cnetmod::application
#endif
