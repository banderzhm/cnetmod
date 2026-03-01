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
import cnetmod.core.log;

namespace cnetmod::mysql::orm {

// =============================================================================
// exec_result — Result for non-query operations (INSERT/UPDATE/DELETE)
// =============================================================================

export struct exec_result {
    std::uint64_t affected_rows  = 0;
    std::uint64_t last_insert_id = 0;
    std::string   error_msg;

    auto ok()     const noexcept -> bool { return error_msg.empty(); }
    auto is_err() const noexcept -> bool { return !ok(); }
};

// =============================================================================
// mapper_session — Mapper-aware session for executing XML-mapped queries
// =============================================================================

export class mapper_session {
public:
    mapper_session(client& cli, mapper_registry& registry) noexcept
        : cli_(cli), registry_(registry) {}

    /// Enable/disable SQL logging
    void set_sql_logging(bool enabled) noexcept { log_sql_ = enabled; }

    /// Get the last generated SQL (before parameter substitution)
    auto last_generated_sql() const noexcept -> std::string_view { return last_sql_; }

    /// Get the last final SQL (after parameter substitution)
    auto last_final_sql() const noexcept -> std::string_view { return last_final_sql_; }

    /// Execute a SELECT mapper statement, return typed results
    template <Model T>
    auto query(std::string_view statement_id,
               const param_context& ctx) -> task<orm_result<T>> {
        auto sql_result = build_sql(statement_id, ctx);
        if (!sql_result) co_return make_err<T>(sql_result.error());

        auto& [sql, params] = *sql_result;
        last_sql_ = sql;

        // Execute via format_sql
        auto final_sql_result = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql_result) co_return make_err<T>("SQL formatting error");

        last_final_sql_ = *final_sql_result;

        // Optional: Log SQL
        if (log_sql_ && final_sql_result->size() < 500) {
            logger::detail::write_log_no_src(logger::level::debug,
                std::format("[SQL] Generated: {}", sql));
            logger::detail::write_log_no_src(logger::level::debug,
                std::format("[SQL] Final: {}", *final_sql_result));
        }

        auto rs = co_await cli_.execute(*final_sql_result);
        if (rs.is_err()) co_return make_err<T>(rs.error_msg);

        orm_result<T> r;
        r.data = from_result_set<T>(rs);
        r.affected_rows = rs.affected_rows;
        co_return r;
    }

    /// Execute a SELECT with model as parameter source
    template <Model T>
    auto query(std::string_view statement_id,
               const T& model) -> task<orm_result<T>> {
        co_return co_await query<T>(statement_id, param_context::from_model(model));
    }

    /// Execute a SELECT with explicit param map
    template <Model T>
    auto query(std::string_view statement_id,
               std::unordered_map<std::string, param_value> params) -> task<orm_result<T>> {
        co_return co_await query<T>(statement_id, param_context::from_map(std::move(params)));
    }

    /// Execute INSERT/UPDATE/DELETE mapper statement
    auto execute(std::string_view statement_id,
                 const param_context& ctx) -> task<exec_result> {
        auto sql_result = build_sql(statement_id, ctx);
        if (!sql_result) {
            exec_result r;
            r.error_msg = sql_result.error();
            co_return r;
        }

        auto& [sql, params] = *sql_result;
        last_sql_ = sql;

        auto final_sql_result = format_sql(cli_.current_format_opts(), sql, params);
        if (!final_sql_result) {
            exec_result r;
            r.error_msg = "SQL formatting error";
            co_return r;
        }

        last_final_sql_ = *final_sql_result;

        // Optional: Log SQL
        if (log_sql_ && final_sql_result->size() < 500) {
            logger::detail::write_log_no_src(logger::level::debug,
                std::format("[SQL] Generated: {}", sql));
            logger::detail::write_log_no_src(logger::level::debug,
                std::format("[SQL] Final: {}", *final_sql_result));
        }

        auto rs = co_await cli_.execute(*final_sql_result);

        exec_result r;
        r.affected_rows  = rs.affected_rows;
        r.last_insert_id = rs.last_insert_id;
        r.error_msg      = rs.error_msg;
        co_return r;
    }

    /// Execute with model as parameter source
    template <Model T>
    auto execute(std::string_view statement_id,
                 const T& model) -> task<exec_result> {
        co_return co_await execute(statement_id, param_context::from_model(model));
    }

    /// Execute with explicit param map
    auto execute(std::string_view statement_id,
                 std::unordered_map<std::string, param_value> params) -> task<exec_result> {
        co_return co_await execute(statement_id, param_context::from_map(std::move(params)));
    }

    /// Access underlying client
    auto underlying() noexcept -> client& { return cli_; }

    /// Access underlying registry
    auto registry() noexcept -> mapper_registry& { return registry_; }

private:
    client& cli_;
    mapper_registry& registry_;
    bool log_sql_ = false;
    std::string last_sql_;
    std::string last_final_sql_;

    auto build_sql(std::string_view statement_id,
                   const param_context& ctx) -> std::expected<built_dynamic_sql, std::string> {
        auto* stmt = registry_.find_statement(statement_id);
        if (!stmt) return std::unexpected("statement not found: " + std::string(statement_id));

        auto ns = registry_.get_namespace(statement_id);
        auto* fragments = registry_.get_fragments(ns);
        if (!fragments) fragments = &empty_fragments_;

        dynamic_sql_processor processor(cli_.current_format_opts());
        auto result = processor.process(*stmt, ctx, *fragments);

        return result;
    }

    template <class T>
    static auto make_err(std::string msg) -> orm_result<T> {
        orm_result<T> r;
        r.error_msg = std::move(msg);
        return r;
    }

    inline static const fragment_map empty_fragments_;
};

} // namespace cnetmod::mysql::orm