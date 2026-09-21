module;

#include <cnetmod/config.hpp>

/**
 * @brief Internal MySQL bindings for the provider-neutral repository factory.
 */
export module cnetmod.application.mysql_orm;

#if defined(CNETMOD_HAS_PROTOCOL_MYSQL) && defined(CNETMOD_HAS_ORM)
import std;
import cnetmod.application.auto_configuration;
import cnetmod.application.configuration;
import cnetmod.application.mysql;
import cnetmod.application.orm_repository;
import cnetmod.application.service_registry;
import cnetmod.coro.task;
import cnetmod.orm.automatic_interceptors;
import cnetmod.orm.model_metadata;
import cnetmod.orm.repository;
import cnetmod.orm.session_gateway;
import cnetmod.orm.sharding.shard_catalog;
import cnetmod.orm.sharding.session_gateway;
import cnetmod.protocol.mysql;

export namespace cnetmod::application {

using mysql_sharded_session_gateway =
    orm::sharded_session_gateway<mysql::client, mysql::pooled_connection>;

using mysql_session_gateway = orm::session_gateway<mysql::client,
    mysql::pooled_connection, orm::mysql_database_session>;

template <orm::Model T>
using mysql_repository_handle = orm_repository_handle<T,
    mysql_session_gateway, orm::mysql_stream_strategy>;

[[nodiscard]] auto make_mysql_session_gateway(mysql_service& service)
    -> mysql_session_gateway;

template <orm::Model T>
[[nodiscard]] auto make_mysql_repository_handle(mysql_service& service,
    orm::automatic_interceptor_options interceptors = {})
    -> std::expected<mysql_repository_handle<T>, std::error_code>;

[[nodiscard]] auto make_mysql_sharded_session_gateway(
    service_registry& services, std::shared_ptr<const orm::shard_catalog> catalog)
    -> std::expected<mysql_sharded_session_gateway, std::error_code>;

[[nodiscard]] auto auto_configure_mysql_sharding(
    const orm_sharding_configuration& configuration,
    service_registry& services) -> std::expected<void, std::error_code>;

    #include "mysql_repository.inl"

} // namespace cnetmod::application
#endif
