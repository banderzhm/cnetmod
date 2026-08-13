export module cnetmod.protocol.mysql:orm_xml_crud;

import std;
import :types;
import :connection_client;
import :format_sql;
import :orm_meta;
import :orm_mapper;
import :orm_mysql_result_adapter;
import :orm_crud;
import :orm_reflect;
import :orm_dynamic_sql;
import :orm_xml_mapper;
import cnetmod.coro.task;
import cnetmod.utils.flat_map;

namespace cnetmod::orm::mysql_detail {
using namespace cnetmod::mysql;
using namespace cnetmod::orm;

struct exec_result
{
    std::uint64_t affected_rows = 0;
    std::uint64_t last_insert_id = 0;
    std::string error_msg;

    auto ok() const noexcept -> bool
    {
        return error_msg.empty();
    }

    auto is_err() const noexcept -> bool
    {
        return !ok();
    }
};

class mapper_session
{
public:
    mapper_session(client& cli, mapper_registry& registry) noexcept;

    void set_sql_logging(bool enabled) noexcept;

    /// Use COM_STMT_PREPARE/COM_STMT_EXECUTE for XML `#{...}` parameters.
    /// Enabled by default so XML values never need text interpolation. It can
    /// be disabled only for compatibility with servers/proxies that reject
    /// the MySQL binary prepared-statement protocol.
    void set_native_prepared_statements(bool enabled) noexcept;
    [[nodiscard]] auto native_prepared_statements() const noexcept -> bool;

    auto last_generated_sql() const noexcept -> std::string_view;
    auto last_final_sql() const noexcept -> std::string_view;

    template <Model T>
    auto query(std::string_view statement_id, const param_context& ctx)
        -> task<orm_result<T>>
    {
        auto sql_result = build_sql(statement_id, ctx);
        if (!sql_result)
            co_return make_err<T>(sql_result.error());

        auto rs = co_await execute_built(*sql_result);
        if (rs.is_err())
            co_return make_err<T>(rs.error_msg);

        orm_result<T> result;
        result.data = mysql_map_result<T>(rs);
        result.affected_rows = rs.affected_rows;
        co_return result;
    }

    template <Model T>
    auto query(std::string_view statement_id, const T& model)
        -> task<orm_result<T>>
    {
        co_return co_await query<T>(statement_id, param_context::from_model(model));
    }

    template <Model T>
    auto query(std::string_view statement_id,
        cnetmod::flat_map<std::string, cnetmod::orm::param_value> params)
        -> task<orm_result<T>>
    {
        co_return co_await query<T>(statement_id,
            param_context::from_map(std::move(params)));
    }

    template <Model T, typename Map>
    requires std::ranges::input_range<Map> &&
        std::same_as<
            std::remove_const_t<
                typename std::ranges::range_value_t<Map>::first_type>,
            std::string> &&
        std::same_as<typename std::ranges::range_value_t<Map>::second_type,
            cnetmod::orm::param_value>
    auto query(std::string_view statement_id, Map&& params)
        -> task<orm_result<T>>
    {
        co_return co_await query<T>(
            statement_id, param_context::from_map(std::forward<Map>(params)));
    }

    template <typename... Ts>
    auto query_tuple(std::string_view statement_id, const param_context& ctx)
        -> task<orm_result<std::tuple<Ts...>>>
    {
        auto sql_result = build_sql(statement_id, ctx);
        if (!sql_result)
        {
            orm_result<std::tuple<Ts...>> result;
            result.error_msg = sql_result.error();
            co_return result;
        }

        auto rs = co_await execute_built(*sql_result);
        if (rs.is_err())
        {
            orm_result<std::tuple<Ts...>> result;
            result.error_msg = rs.error_msg;
            co_return result;
        }

        orm_result<std::tuple<Ts...>> result;
        result.data = mysql_map_result_to_tuples<Ts...>(rs);
        result.affected_rows = rs.affected_rows;
        co_return result;
    }

    template <typename... Ts>
    auto query_tuple(std::string_view statement_id,
        cnetmod::flat_map<std::string, cnetmod::orm::param_value> params)
        -> task<orm_result<std::tuple<Ts...>>>
    {
        co_return co_await query_tuple<Ts...>(
            statement_id, param_context::from_map(std::move(params)));
    }

    template <typename... Ts, typename Map>
    requires std::ranges::input_range<Map> &&
        std::same_as<
            std::remove_const_t<
                typename std::ranges::range_value_t<Map>::first_type>,
            std::string> &&
        std::same_as<typename std::ranges::range_value_t<Map>::second_type,
            cnetmod::orm::param_value>
    auto query_tuple(std::string_view statement_id, Map&& params)
        -> task<orm_result<std::tuple<Ts...>>>
    {
        co_return co_await query_tuple<Ts...>(
            statement_id, param_context::from_map(std::forward<Map>(params)));
    }

    auto execute(std::string_view statement_id, const param_context& ctx)
        -> task<exec_result>;

    template <Model T>
    auto execute(std::string_view statement_id, const T& model)
        -> task<exec_result>
    {
        co_return co_await execute(statement_id, param_context::from_model(model));
    }

    auto execute(std::string_view statement_id,
        cnetmod::flat_map<std::string, cnetmod::orm::param_value> params)
        -> task<exec_result>;

    template <typename Map>
    requires std::ranges::input_range<Map> &&
        std::same_as<
            std::remove_const_t<
                typename std::ranges::range_value_t<Map>::first_type>,
            std::string> &&
        std::same_as<typename std::ranges::range_value_t<Map>::second_type,
            cnetmod::orm::param_value>
    auto execute(std::string_view statement_id, Map&& params)
        -> task<exec_result>
    {
        co_return co_await execute(
            statement_id, param_context::from_map(std::forward<Map>(params)));
    }

    auto execute_query(std::string_view statement_id, const param_context& ctx)
        -> task<result_set>;

    // Execute a select using its XML resultMap, merge joined rows by <id>, and
    // eagerly execute association/collection nested selects when declared.
    // The return type is dynamic because XML property names cannot safely be
    // projected into arbitrary C++ members without an application binding.
    auto query_object_graph(std::string_view statement_id, const param_context& ctx)
        -> task<std::expected<std::vector<mapped_object>, std::string>>;

    /// Execute a resultMap-backed XML select and project its scalar root
    /// properties into a CNETMOD_MODEL DTO. Association/collection graphs
    /// remain available through query_object_graph(), because XML cannot
    /// infer arbitrary nested C++ member types safely.
    template <Model T>
    auto query_object_graph_as(std::string_view statement_id,
        const param_context& ctx) -> task<std::expected<std::vector<T>, std::string>>
    {
        auto graph = co_await query_object_graph(statement_id, ctx);
        if (!graph)
            co_return std::unexpected(graph.error());
        co_return from_mapped_objects<T>(*graph);
    }

    template <Model T>
    auto query_object_graph_as(std::string_view statement_id, const T& parameters)
        -> task<std::expected<std::vector<T>, std::string>>
    {
        co_return co_await query_object_graph_as<T>(statement_id,
            param_context::from_model(parameters));
    }

    auto execute_query(std::string_view statement_id,
        cnetmod::flat_map<std::string, cnetmod::orm::param_value> params)
        -> task<result_set>;

    template <typename Map>
    requires std::ranges::input_range<Map> &&
        std::same_as<
            std::remove_const_t<
                typename std::ranges::range_value_t<Map>::first_type>,
            std::string> &&
        std::same_as<typename std::ranges::range_value_t<Map>::second_type,
            cnetmod::orm::param_value>
    auto execute_query(std::string_view statement_id, Map&& params)
        -> task<result_set>
    {
        co_return co_await execute_query(
            statement_id, param_context::from_map(std::forward<Map>(params)));
    }

    auto underlying() noexcept -> client&;
    auto registry() noexcept -> mapper_registry&;

private:
    client& cli_;
    mapper_registry& registry_;
    bool log_sql_ = false;
    bool native_prepared_statements_ = true;
    std::string last_sql_;
    std::string last_final_sql_;

    auto build_sql(std::string_view statement_id, const param_context& ctx)
        -> std::expected<built_dynamic_sql, std::string>;
    auto execute_built(const built_dynamic_sql& built)
        -> task<cnetmod::mysql::result_set>;

    template <class T> static auto make_err(std::string msg) -> orm_result<T>
    {
        orm_result<T> result;
        result.error_msg = std::move(msg);
        return result;
    }

    static const fragment_map empty_fragments_;
};

} // namespace cnetmod::orm::mysql_detail

export namespace cnetmod::orm {
using mysql_mapper_execution_result = mysql_detail::exec_result;
using mysql_mapper_session = mysql_detail::mapper_session;
} // namespace cnetmod::orm
