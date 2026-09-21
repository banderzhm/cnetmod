module;

#include <cnetmod/config.hpp>

/**
 * @brief Internal PostgreSQL bindings for the provider-neutral repository factory.
 */
export module cnetmod.application.postgresql_orm;

#if defined(CNETMOD_HAS_PROTOCOL_POSTGRESQL) && defined(CNETMOD_HAS_ORM)
import std;
import cnetmod.application.orm_repository;
import cnetmod.application.postgresql;
import cnetmod.coro.task;
import cnetmod.orm.automatic_interceptors;
import cnetmod.orm.model_metadata;
import cnetmod.orm.session_gateway;
import cnetmod.protocol.postgresql;

export namespace cnetmod::application {

using postgresql_session_gateway = orm::session_gateway<
    postgresql::client, postgresql::pooled_connection>;

template <orm::Model T>
using postgresql_repository_handle = orm_repository_handle<
    T, postgresql_session_gateway>;

[[nodiscard]] auto make_postgresql_session_gateway(
    postgresql_service& service) -> postgresql_session_gateway;

template <orm::Model T>
[[nodiscard]] auto make_postgresql_repository_handle(
    postgresql_service& service,
    orm::automatic_interceptor_options interceptors = {})
    -> std::expected<postgresql_repository_handle<T>, std::error_code>;

    #include "postgresql_repository.inl"

} // namespace cnetmod::application
#endif
