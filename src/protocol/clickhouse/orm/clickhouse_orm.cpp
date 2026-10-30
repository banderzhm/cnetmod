module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.clickhouse.orm;
#ifdef CNETMOD_HAS_ORM
import std;
import cnetmod.protocol.clickhouse;
import cnetmod.database.sql_parameters;
import cnetmod.database.sql_query_data;
    #include "statement_binding.hpp"

namespace cnetmod::clickhouse {
orm_client::orm_client(client& native, orm_value_decoder decoder) : client_(&native), decoder_(std::move(decoder)) {}

auto orm_client::native() noexcept -> client&
{
    return *client_;
}

auto orm_client::query(std::string_view sql) -> task<database::query_result>
{
    co_return orm_detail::adapt_result(co_await client_->query(std::string(sql)), decoder_);
}

auto orm_client::execute(std::string_view sql) -> task<database::query_result>
{
    return execute(database::parameterized_query{std::string(sql), {}});
}

auto orm_client::execute(database::parameterized_query sql) -> task<database::query_result>
{
    auto bound = orm_detail::bind_statement(std::move(sql));
    if (!bound)
    {
        database::query_result failure;
        failure.error_msg = bound.error();
        co_return failure;
    }
    co_return orm_detail::adapt_result(co_await client_->query(std::move(bound->sql), std::move(bound->request)), decoder_);
}

auto make_session_gateway(connection_pool& pool) -> orm_session_gateway
{
    return {orm::sql_dialect::clickhouse,
        []() -> task<std::expected<void, std::string>>
        {
            co_return std::expected<void, std::string>{};
        },
        [&pool]() -> task<std::expected<orm_connection, std::string>>
        {
            auto connection = co_await pool.acquire();
            if (!connection)
                co_return std::unexpected(connection.error().message);
            co_return orm_connection{std::move(*connection)};
        },
        [](orm_connection& connection) -> orm_client&
        {
            return connection.adapter;
        }};
}
} // namespace cnetmod::clickhouse
#endif
