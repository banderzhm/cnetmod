module;

#include <cnetmod/config.hpp>

/// Managed Redis pool with readiness and supervised maintenance.
export module cnetmod.application.redis;

#ifdef CNETMOD_HAS_PROTOCOL_REDIS
import std;
import cnetmod.application.auto_configuration;
import cnetmod.application.configuration;
import cnetmod.application.managed_service;
import cnetmod.application.recovery_policy;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.protocol.redis;
import cnetmod.instrumentation.tracing;

namespace cnetmod::application {

export class redis_service final : public managed_service
{
public:
    redis_service(io_context& io, redis::pool_params options,
        std::string instance, service_requirement requirement,
        recovery_policy recovery,
        instrumentation::span_exporter spans = {});
    redis_service(io_context& control,
        std::span<io_context* const> event_loops, redis::pool_params options,
        std::string instance, service_requirement requirement,
        recovery_policy recovery,
        instrumentation::span_exporter spans = {});

    [[nodiscard]] auto pool() -> redis::connection_pool&;
    /**
     * @brief Creates a namespaced Redis template using application telemetry.
     *
     * The explicit parent preserves coroutine trace context without thread-local
     * state. The returned facade borrows this service's lifecycle-owned pool.
     */
    [[nodiscard]] auto make_template(redis::template_options options = {},
        instrumentation::trace_context parent = {})
        -> redis::redis_template;
    [[nodiscard]] auto key() const -> service_key override;
    [[nodiscard]] auto requirement() const noexcept
        -> service_requirement override;
    [[nodiscard]] auto recovery() const noexcept -> recovery_policy override;
    [[nodiscard]] auto shutdown_required() const noexcept -> bool override;
    auto start(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    auto stop(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    /**
     * @brief Borrows and probes a command connection within one shared deadline.
     *
     * Health messages never include Redis response bodies or credentials.
     */
    auto probe(service_context& context) -> task<health_report> override;

private:
    io_context& io_;
    std::unique_ptr<redis::connection_pool> pool_;
    std::unique_ptr<redis::sharded_connection_pool> sharded_pool_;
    std::vector<io_context*> event_loops_;
    std::string instance_;
    service_requirement requirement_;
    recovery_policy recovery_;
    instrumentation::span_exporter spans_;
    bool started_ = false;
};

/**
 * @brief Managed Redis Cluster client with seed failover and slot health.
 */
export class redis_cluster_service final : public managed_service
{
public:
    /**
     * @brief Creates a supervised Redis Cluster service from ordered seeds.
     */
    redis_cluster_service(io_context& io,
        std::vector<redis::connect_options> seeds, std::string instance,
        service_requirement requirement, recovery_policy recovery);
    redis_cluster_service(io_context& control,
        std::span<io_context* const> event_loops,
        std::vector<redis::connect_options> seeds, std::string instance,
        service_requirement requirement, recovery_policy recovery);

    /**
     * @brief Returns the lifecycle-owned cluster client.
     */
    [[nodiscard]] auto client() -> redis::cluster_client&;
    [[nodiscard]] auto key() const -> service_key override;
    [[nodiscard]] auto requirement() const noexcept
        -> service_requirement override;
    [[nodiscard]] auto recovery() const noexcept -> recovery_policy override;
    [[nodiscard]] auto shutdown_required() const noexcept -> bool override;
    auto start(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    auto stop(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    auto probe(service_context& context) -> task<health_report> override;

private:
    io_context& io_;
    std::vector<io_context*> event_loops_;
    std::vector<std::unique_ptr<redis::cluster_client>> clients_;
    std::vector<redis::connect_options> seeds_;
    std::string instance_;
    service_requirement requirement_;
    recovery_policy recovery_;
    bool started_ = false;
};

export [[nodiscard]] auto auto_configure_redis(
    const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>;

} // namespace cnetmod::application
#endif
