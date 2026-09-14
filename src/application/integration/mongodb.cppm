module;

#include <cnetmod/config.hpp>

/// Managed MongoDB connection pool and maintenance task.
export module cnetmod.application.mongodb;

#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
import std;
import cnetmod.application.auto_configuration;
import cnetmod.application.configuration;
import cnetmod.application.managed_service;
import cnetmod.application.recovery_policy;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.mongodb;

namespace cnetmod::application {

export class mongodb_service final : public managed_service
{
public:
    mongodb_service(io_context& io, mongodb::connection_pool_options options,
        std::string instance, service_requirement requirement,
        recovery_policy recovery);
    /**
     * Returns the pool for the current service run. Do not retain this
     * reference across stop and restart; a successful restart replaces it.
     */
    [[nodiscard]] auto pool() noexcept -> mongodb::connection_pool&;
    [[nodiscard]] auto key() const -> service_key override;
    [[nodiscard]] auto requirement() const noexcept
        -> service_requirement override;
    [[nodiscard]] auto recovery() const noexcept -> recovery_policy override;
    [[nodiscard]] auto cleanup_required() const noexcept -> bool override;
    auto start(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    auto stop(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    auto probe(service_context& context) -> task<health_report> override;

private:
    auto start_runtime(service_context& context)
        -> task<std::expected<void, std::error_code>>;
    mongodb::connection_pool_options options_;
    std::shared_ptr<mongodb::connection_pool> pool_;
    std::stop_source maintenance_stop_;
    std::string instance_;
    service_requirement requirement_;
    recovery_policy recovery_;
    bool started_ = false;
    bool cleanup_pending_ = false;
    bool rebuild_pool_ = false;
};

export [[nodiscard]] auto auto_configure_mongodb(
    const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>;

} // namespace cnetmod::application
#endif
