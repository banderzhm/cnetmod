module cnetmod.application.postgresql_orm;

#if defined(CNETMOD_HAS_PROTOCOL_POSTGRESQL) && defined(CNETMOD_HAS_ORM)
import std;
import cnetmod.application.postgresql;
import cnetmod.coro.task;
import cnetmod.orm.sql_dialect;
import cnetmod.protocol.postgresql;

namespace cnetmod::application {

auto make_postgresql_session_gateway(postgresql_service& service)
    -> postgresql_session_gateway
{
    return postgresql_session_gateway{
        orm::sql_dialect::postgresql,
        []() -> task<std::expected<void, std::string>>
        {
            co_return std::expected<void, std::string>{};
        },
        [&service]()
            -> task<std::expected<postgresql::pooled_connection, std::string>>
        {
            auto connection = co_await service.pool().acquire();
            if (!connection)
                co_return std::unexpected(connection.error().message());
            co_return std::move(*connection);
        },
        [](postgresql::pooled_connection& connection) -> postgresql::client&
        {
            return connection.get();
        }};
}

} // namespace cnetmod::application
#endif
