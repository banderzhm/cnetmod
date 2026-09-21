module;

#include <cnetmod/config.hpp>

/// Managed PostgreSQL connection pool.
export module cnetmod.application.postgresql;

#ifdef CNETMOD_HAS_PROTOCOL_POSTGRESQL
import std;
import cnetmod.application.auto_configuration;
import cnetmod.application.configuration;
import cnetmod.application.managed_service;
import cnetmod.application.orm_repository;
import cnetmod.application.recovery_policy;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.postgresql;
#ifdef CNETMOD_HAS_ORM
import cnetmod.orm.automatic_interceptors;
import cnetmod.orm.repository;
import cnetmod.orm.session_gateway;
#endif

namespace cnetmod::application {

export class postgresql_service final : public managed_service
{
public:
    postgresql_service(io_context& io,
        postgresql::connection_pool_options options, std::string instance,
        service_requirement requirement, recovery_policy recovery);
    [[nodiscard]] auto pool() noexcept -> postgresql::connection_pool&;
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
    postgresql::connection_pool pool_;
    std::string instance_;
    service_requirement requirement_;
    recovery_policy recovery_;
    bool started_ = false;
};

export [[nodiscard]] auto auto_configure_postgresql(
    const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>;

#ifdef CNETMOD_HAS_ORM
/**
 * @brief Gateway used by provider-neutral repositories over PostgreSQL pools.
 */
export using postgresql_session_gateway = orm::session_gateway<
    postgresql::client, postgresql::pooled_connection>;

/**
 * @brief Application repository handle specialized for PostgreSQL.
 */
export template <orm::Model T>
using postgresql_repository_handle = orm_repository_handle<
    T, postgresql_session_gateway>;

/**
 * @brief Binds an ORM session gateway to a managed PostgreSQL pool.
 */
export [[nodiscard]] auto make_postgresql_session_gateway(
    postgresql_service& service) -> postgresql_session_gateway;

/**
 * @brief Creates a repository bound to one managed PostgreSQL pool instance.
 */
export template <orm::Model T>
[[nodiscard]] auto make_postgresql_repository_handle(
    postgresql_service& service,
    orm::automatic_interceptor_options interceptors = {})
    -> std::expected<postgresql_repository_handle<T>, std::error_code>;

#include "postgresql_repository.inl"
#endif

} // namespace cnetmod::application
#endif
