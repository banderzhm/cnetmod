export module cnetmod.orm.database_session;

import std;
import cnetmod.orm.sql_query_data;
import cnetmod.orm.sql_parameters;
import cnetmod.coro.task;

export namespace cnetmod::orm {

template <class Client>
concept asynchronous_database_client = requires(Client& client,
    std::string_view sql, parameterized_query statement,
    isolation_level isolation) {
    { client.query(sql) } -> std::same_as<task<query_result>>;
    { client.execute(sql) } -> std::same_as<task<query_result>>;
    { client.execute(std::move(statement)) } -> std::same_as<task<query_result>>;
    { client.current_format_opts() } -> std::convertible_to<const sql_format_options&>;
};

/// Protocol-independent session used by ORM repositories and generated mappers.
/// Higher-level model modules depend on this contract, never a wire protocol.
template <asynchronous_database_client Client>
class database_session
{
public:
    explicit database_session(Client& client) noexcept : client_(&client) {}

    [[nodiscard]] auto underlying() noexcept -> Client&
    {
        return *client_;
    }

    auto query(std::string_view sql) -> task<query_result>
    {
        co_return co_await client_->query(sql);
    }

    auto execute(std::string_view sql) -> task<query_result>
    {
        co_return co_await client_->execute(sql);
    }

    auto execute(parameterized_query query) -> task<query_result>
    {
        co_return co_await client_->execute(std::move(query));
    }

    template <typename Function>
    requires std::invocable<Function> && requires(Function function) {
        { function() } -> std::same_as<task<void>>;
    }
    auto transaction(Function&& function) -> task<query_result>
    {
        co_return co_await client_->transaction(std::forward<Function>(function));
    }

    template <typename Function>
    requires std::invocable<Function> && requires(Function function) {
        { function() } -> std::same_as<task<void>>;
    }
    auto transaction(Function&& function, isolation_level isolation)
        -> task<query_result>
    {
        co_return co_await client_->transaction(std::forward<Function>(function), isolation);
    }

private:
    Client* client_;
};

} // namespace cnetmod::orm
