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
import cnetmod.application.service_registry;
import cnetmod.io.io_context;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.protocol.mysql;

namespace cnetmod::application {

export class mysql_service final : public managed_service
{
public:
    mysql_service(io_context& io, mysql::pool_params options,
        std::string instance, service_requirement requirement,
        recovery_policy recovery);
    mysql_service(io_context& control, std::span<io_context* const> event_loops,
        mysql::pool_params options, std::string instance,
        service_requirement requirement, recovery_policy recovery);
    [[nodiscard]] auto pool() -> mysql::connection_pool&;
    [[nodiscard]] auto acquire()
        -> task<std::expected<mysql::pooled_connection, std::error_code>>;
    [[nodiscard]] auto acquire(cancel_token& cancellation)
        -> task<std::expected<mysql::pooled_connection, std::error_code>>;
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
    std::unique_ptr<mysql::connection_pool> pool_;
    std::unique_ptr<mysql::sharded_connection_pool> sharded_pool_;
    std::vector<io_context*> event_loops_;
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
