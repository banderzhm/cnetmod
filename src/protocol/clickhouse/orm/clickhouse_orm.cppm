module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.clickhouse.orm;
#ifdef CNETMOD_HAS_ORM
import std;
import cnetmod.protocol.clickhouse;
import cnetmod.database.sql_parameters;
import cnetmod.database.sql_query_data;
import cnetmod.coro.task;
import cnetmod.orm.database_session;
import cnetmod.orm.session_gateway;
import cnetmod.orm.sql_dialect;

export namespace cnetmod::clickhouse {
using orm_value_decoder = std::function<std::optional<database::field_value>(const value&, std::string_view)>;

class orm_client
{
public:
    explicit orm_client(client&, orm_value_decoder = {});
    auto query(std::string_view) -> task<database::query_result>;
    auto execute(std::string_view) -> task<database::query_result>;
    auto execute(database::parameterized_query) -> task<database::query_result>;
    [[nodiscard]] auto native() noexcept -> client&;

private:
    client* client_;
    orm_value_decoder decoder_;
};

using orm_session = orm::database_session<orm_client>;

struct orm_connection
{
    pooled_connection lease;
    orm_client adapter;

    explicit orm_connection(pooled_connection value) : lease(std::move(value)), adapter(lease.get()) {}

    orm_connection(orm_connection&&) noexcept = default;
    auto operator=(orm_connection&&) noexcept -> orm_connection& = default;
};

using orm_session_gateway = orm::session_gateway<orm_client, orm_connection>;
[[nodiscard]] auto make_session_gateway(connection_pool&) -> orm_session_gateway;
} // namespace cnetmod::clickhouse
#endif
