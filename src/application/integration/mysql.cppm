module;

#include <cnetmod/config.hpp>

/// Managed MySQL pool with readiness and supervised maintenance.
export module cnetmod.application.mysql;

#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
import std;
import cnetmod.application.auto_configuration;
import cnetmod.application.configuration;
import cnetmod.application.managed_service;
import cnetmod.application.recovery_policy;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.protocol.mysql;

namespace cnetmod::application {

export class mysql_service final : public managed_service
{
public:
    mysql_service(io_context& io, mysql::pool_params options,
        std::string instance, service_requirement requirement,
        recovery_policy recovery);
    [[nodiscard]] auto pool() noexcept -> mysql::connection_pool&;
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
    mysql::connection_pool pool_;
    std::string instance_;
    service_requirement requirement_;
    recovery_policy recovery_;
    bool started_ = false;
};

export [[nodiscard]] auto auto_configure_mysql(
    const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>;

} // namespace cnetmod::application
#endif
