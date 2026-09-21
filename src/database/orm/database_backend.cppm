/**
 * @brief Protocol-neutral database backend contract for ORM sessions.
 */
export module cnetmod.orm.database_backend;

import std;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.sql_query_data;

export namespace cnetmod::orm {

/**
 * @brief Adapts a protocol result into the provider-neutral query result.
 *
 * Protocol modules specialize this boundary for their wire result type so the
 * ORM does not depend on a concrete database protocol.
 */
template <class Result>
struct database_result_adapter
{
    static auto adapt(Result&& result) -> query_result
    {
        static_assert(std::same_as<std::remove_cvref_t<Result>, query_result>,
            "database client result needs a database_result_adapter specialization");
        return std::forward<Result>(result);
    }
};

/**
 * @brief Dispatches result conversion through the backend specialization.
 */
struct default_database_result_adapter
{
    template <class Result>
    static auto adapt(Result&& result) -> query_result
    {
        return database_result_adapter<std::remove_cvref_t<Result>>::adapt(
            std::forward<Result>(result));
    }
};

/**
 * @brief Controls potentially sensitive SQL telemetry.
 *
 * Query text is excluded by default because it can contain credentials or
 * personal data.
 */
struct sql_observation_options
{
    bool capture_query_text = false;
    std::size_t max_query_bytes = 2048;
};

/**
 * @brief Defines the asynchronous protocol client required by an ORM session.
 */
template <class Client>
concept asynchronous_database_client = requires(Client& client,
    std::string_view sql,
    parameterized_query statement,
    isolation_level isolation) {
    client.query(sql);
    client.execute(sql);
    client.execute(std::move(statement));
};

} // namespace cnetmod::orm
