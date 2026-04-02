export module cnetmod.protocol.mysql:orm_xml_crud;

import std;
import :types;
import :client;
import :format_sql;
import :orm_meta;
import :orm_mapper;
import :orm_crud;
import :orm_reflect;
import :orm_dynamic_sql;
import :orm_xml_mapper;
import cnetmod.coro.task;

#if defined(_MSC_VER)
#define CNETMOD_XML_PARAM_MAP std::unordered_map
#else
#define CNETMOD_XML_PARAM_MAP std::flat_map
#endif

namespace cnetmod::mysql::orm {

export struct exec_result {
    std::uint64_t affected_rows  = 0;
    std::uint64_t last_insert_id = 0;
    std::string   error_msg;

    auto ok()     const noexcept -> bool { return error_msg.empty(); }
    auto is_err() const noexcept -> bool { return !ok(); }
};

export class mapper_session {
public:
    mapper_session(client& cli, mapper_registry& registry) noexcept;

    void set_sql_logging(bool enabled) noexcept;

    auto last_generated_sql() const noexcept -> std::string_view;
    auto last_final_sql() const noexcept -> std::string_view;

    template <Model T>
    auto query(std::string_view statement_id,
               const param_context& ctx) -> task<orm_result<T>> {
        auto sql_result = build_sql(statement_id, ctx);
        if (!sql_result) co_return make_err<T>(sql_result.error());

        auto& [sql, params] = *sql_result;
        last_sql_ = sql;

        auto final_sql_result = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql_result) co_return make_err<T>("SQL formatting error");

        last_final_sql_ = *final_sql_result;

        auto rs = co_await cli_.execute(*final_sql_result);
        if (rs.is_err()) co_return make_err<T>(rs.error_msg);

        orm_result<T> result;
        result.data = from_result_set<T>(rs);
        result.affected_rows = rs.affected_rows;
        co_return result;
    }

    template <Model T>
    auto query(std::string_view statement_id,
               const T& model) -> task<orm_result<T>> {
        co_return co_await query<T>(statement_id, param_context::from_model(model));
    }

    template <Model T>
    auto query(std::string_view statement_id,
               CNETMOD_XML_PARAM_MAP<std::string, param_value> params) -> task<orm_result<T>> {
        co_return co_await query<T>(statement_id, param_context::from_map(std::move(params)));
    }

    template <typename... Ts>
    auto query_tuple(std::string_view statement_id,
                     const param_context& ctx) -> task<orm_result<std::tuple<Ts...>>> {
        auto sql_result = build_sql(statement_id, ctx);
        if (!sql_result) {
            orm_result<std::tuple<Ts...>> result;
            result.error_msg = sql_result.error();
            co_return result;
        }

        auto& [sql, params] = *sql_result;
        last_sql_ = sql;

        auto final_sql_result = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql_result) {
            orm_result<std::tuple<Ts...>> result;
            result.error_msg = "SQL formatting error";
            co_return result;
        }

        last_final_sql_ = *final_sql_result;

        auto rs = co_await cli_.execute(*final_sql_result);
        if (rs.is_err()) {
            orm_result<std::tuple<Ts...>> result;
            result.error_msg = rs.error_msg;
            co_return result;
        }

        orm_result<std::tuple<Ts...>> result;
        result.data = from_result_set_to_tuple<Ts...>(rs);
        result.affected_rows = rs.affected_rows;
        co_return result;
    }

    template <typename... Ts>
    auto query_tuple(std::string_view statement_id,
                     CNETMOD_XML_PARAM_MAP<std::string, param_value> params)
        -> task<orm_result<std::tuple<Ts...>>> {
        co_return co_await query_tuple<Ts...>(statement_id, param_context::from_map(std::move(params)));
    }

    auto execute(std::string_view statement_id,
                 const param_context& ctx) -> task<exec_result>;

    template <Model T>
    auto execute(std::string_view statement_id,
                 const T& model) -> task<exec_result> {
        co_return co_await execute(statement_id, param_context::from_model(model));
    }

    auto execute(std::string_view statement_id,
                 CNETMOD_XML_PARAM_MAP<std::string, param_value> params) -> task<exec_result>;

    auto execute_query(std::string_view statement_id,
                       const param_context& ctx) -> task<result_set>;

    auto execute_query(std::string_view statement_id,
                       CNETMOD_XML_PARAM_MAP<std::string, param_value> params) -> task<result_set>;

    auto underlying() noexcept -> client&;
    auto registry() noexcept -> mapper_registry&;

private:
    client& cli_;
    mapper_registry& registry_;
    bool log_sql_ = false;
    std::string last_sql_;
    std::string last_final_sql_;

    auto build_sql(std::string_view statement_id,
                   const param_context& ctx) -> std::expected<built_dynamic_sql, std::string>;

    template <class T>
    static auto make_err(std::string msg) -> orm_result<T> {
        orm_result<T> result;
        result.error_msg = std::move(msg);
        return result;
    }

    static const fragment_map empty_fragments_;
};

} // namespace cnetmod::mysql::orm
