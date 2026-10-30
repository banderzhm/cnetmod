module;
#include <cnetmod/config.hpp>
module cnetmod.application.clickhouse_orm;
#if defined(CNETMOD_HAS_PROTOCOL_CLICKHOUSE) && defined(CNETMOD_HAS_ORM)
import std;
import cnetmod.coro.task;
import cnetmod.orm.sql_dialect;
import cnetmod.protocol.clickhouse;
import cnetmod.protocol.clickhouse.orm;

namespace cnetmod::application {
auto make_clickhouse_session_gateway(clickhouse_service& service) -> clickhouse::orm_session_gateway
{
    // Resolve the loop-local pool only when the repository executes. Factories
    // are constructed before event loops run and must not bind one loop's pool.
    return {orm::sql_dialect::clickhouse,
        []() -> task<std::expected<void, std::string>>
        {
            co_return std::expected<void, std::string>{};
        },
        [&service]() -> task<std::expected<clickhouse::orm_connection, std::string>>
        {
            auto lease = co_await service.pool().acquire();
            if (!lease)
                co_return std::unexpected(lease.error().message);
            co_return clickhouse::orm_connection{std::move(*lease)};
        },
        [](clickhouse::orm_connection& lease) -> clickhouse::orm_client&
        {
            return lease.adapter;
        }};
}
} // namespace cnetmod::application
#endif
