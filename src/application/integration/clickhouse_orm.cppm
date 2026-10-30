module;
#include <cnetmod/config.hpp>
export module cnetmod.application.clickhouse_orm;
#if defined(CNETMOD_HAS_PROTOCOL_CLICKHOUSE) && defined(CNETMOD_HAS_ORM)
import std;
import cnetmod.application.clickhouse;
import cnetmod.application.orm_repository;
import cnetmod.orm.automatic_interceptors;
import cnetmod.orm.model_metadata;
import cnetmod.protocol.clickhouse.orm;

export namespace cnetmod::application {
template <orm::Model T>
using clickhouse_repository_handle = orm_repository_handle<T, clickhouse::orm_session_gateway>;

[[nodiscard]] auto make_clickhouse_session_gateway(clickhouse_service&) -> clickhouse::orm_session_gateway;

template <orm::Model T>
[[nodiscard]] auto make_clickhouse_repository_handle(clickhouse_service& service,
    orm::automatic_interceptor_options policies = {})
    -> std::expected<clickhouse_repository_handle<T>, std::error_code>
{
    try
    {
        auto gateway = std::make_shared<clickhouse::orm_session_gateway>(make_clickhouse_session_gateway(service));
        return make_orm_repository_handle<T, clickhouse::orm_session_gateway>(std::move(gateway), std::move(policies));
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
    }
}
} // namespace cnetmod::application
#endif
