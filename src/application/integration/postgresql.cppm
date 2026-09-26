module;

#include <cnetmod/config.hpp>

/// Managed PostgreSQL connection pool.
export module cnetmod.application.postgresql;

#ifdef CNETMOD_HAS_PROTOCOL_POSTGRESQL
import std;
import cnetmod.application.auto_configuration;
import cnetmod.application.configuration;
import cnetmod.application.managed_service;
import cnetmod.application.recovery_policy;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.postgresql;

namespace cnetmod::application {

export class postgresql_service final : public managed_service
{
public:
    ~postgresql_service() override;
    postgresql_service(io_context& io,
        postgresql::connection_pool_options options, std::string instance,
        service_requirement requirement, recovery_policy recovery);
    postgresql_service(std::span<io_context* const> event_loops,
        postgresql::connection_pool_options options, std::string instance,
        service_requirement requirement, recovery_policy recovery);
    [[nodiscard]] auto pool() -> postgresql::connection_pool&;
    [[nodiscard]] auto key() const -> service_key override;
    [[nodiscard]] auto requirement() const noexcept
        -> service_requirement override;
    [[nodiscard]] auto recovery() const noexcept -> recovery_policy override;
    auto start(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    auto stop(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    auto probe(service_context& context) -> task<health_report> override;

private:
    struct pool_shard;
    std::vector<std::unique_ptr<pool_shard>> pools_;
    std::string instance_;
    service_requirement requirement_;
    recovery_policy recovery_;
    bool started_ = false;
};

export [[nodiscard]] auto auto_configure_postgresql(
    const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>;

} // namespace cnetmod::application
#endif
