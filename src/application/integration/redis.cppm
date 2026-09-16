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

namespace cnetmod::application {

export class redis_service final : public managed_service
{
public:
    redis_service(io_context& io, redis::pool_params options,
        std::string instance, service_requirement requirement,
        recovery_policy recovery);

    [[nodiscard]] auto pool() noexcept -> redis::connection_pool&;
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
    redis::connection_pool pool_;
    std::string instance_;
    service_requirement requirement_;
    recovery_policy recovery_;
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

    /**
     * @brief Returns the lifecycle-owned cluster client.
     */
    [[nodiscard]] auto client() noexcept -> redis::cluster_client&;
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
    redis::cluster_client client_;
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
