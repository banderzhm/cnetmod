module cnetmod.protocol.mysql;

import std;
import :types;
import :client;
import :format_sql;
import :orm_dynamic_sql;
import :orm_xml_crud;
import :orm_xml_mapper;
import cnetmod.coro.task;
import cnetmod.core.log;

#if defined(_MSC_VER)
#define CNETMOD_XML_PARAM_MAP std::unordered_map
#else
#define CNETMOD_XML_PARAM_MAP std::flat_map
#endif

namespace cnetmod::mysql::orm {

const fragment_map mapper_session::empty_fragments_{};

mapper_session::mapper_session(client& cli, mapper_registry& registry) noexcept
    : cli_(cli), registry_(registry) {}

void mapper_session::set_sql_logging(bool enabled) noexcept {
    log_sql_ = enabled;
}

auto mapper_session::last_generated_sql() const noexcept -> std::string_view {
    return last_sql_;
}

auto mapper_session::last_final_sql() const noexcept -> std::string_view {
    return last_final_sql_;
}

auto mapper_session::execute(std::string_view statement_id,
                             const param_context& ctx) -> task<exec_result> {
    auto sql_result = build_sql(statement_id, ctx);
    if (!sql_result) {
        exec_result result;
        result.error_msg = sql_result.error();
        co_return result;
    }

    auto& [sql, params] = *sql_result;
    last_sql_ = sql;

    auto final_sql_result = format_sql(cli_.current_format_opts(), sql, params);
    if (!final_sql_result) {
        exec_result result;
        result.error_msg = "SQL formatting error";
        co_return result;
    }

    last_final_sql_ = *final_sql_result;

    if (log_sql_ && final_sql_result->size() < 500) {
        logger::detail::write_log_no_src(logger::level::debug,
            std::format("[SQL] Generated: {}", sql));
        logger::detail::write_log_no_src(logger::level::debug,
            std::format("[SQL] Final: {}", *final_sql_result));
    }

    auto rs = co_await cli_.execute(*final_sql_result);

    exec_result result;
    result.affected_rows  = rs.affected_rows;
    result.last_insert_id = rs.last_insert_id;
    result.error_msg      = rs.error_msg;
    co_return result;
}

auto mapper_session::execute(std::string_view statement_id,
                             CNETMOD_XML_PARAM_MAP<std::string, param_value> params)
    -> task<exec_result> {
    co_return co_await execute(statement_id, param_context::from_map(std::move(params)));
}

auto mapper_session::execute_query(std::string_view statement_id,
                                   const param_context& ctx) -> task<result_set> {
    auto sql_result = build_sql(statement_id, ctx);
    if (!sql_result) {
        result_set result;
        result.error_msg = sql_result.error();
        co_return result;
    }

    auto& [sql, params] = *sql_result;
    last_sql_ = sql;

    auto final_sql_result = format_sql(cli_.current_format_opts(), sql, params);
    if (!final_sql_result) {
        result_set result;
        result.error_msg = "SQL formatting error";
        co_return result;
    }

    last_final_sql_ = *final_sql_result;

    if (log_sql_ && final_sql_result->size() < 500) {
        logger::detail::write_log_no_src(logger::level::debug,
            std::format("[SQL] Generated: {}", sql));
        logger::detail::write_log_no_src(logger::level::debug,
            std::format("[SQL] Final: {}", *final_sql_result));
    }

    co_return co_await cli_.execute(*final_sql_result);
}

auto mapper_session::execute_query(std::string_view statement_id,
                                   CNETMOD_XML_PARAM_MAP<std::string, param_value> params)
    -> task<result_set> {
    co_return co_await execute_query(statement_id, param_context::from_map(std::move(params)));
}

auto mapper_session::underlying() noexcept -> client& {
    return cli_;
}

auto mapper_session::registry() noexcept -> mapper_registry& {
    return registry_;
}

auto mapper_session::build_sql(std::string_view statement_id,
                               const param_context& ctx)
    -> std::expected<built_dynamic_sql, std::string> {
    auto* stmt = registry_.find_statement(statement_id);
    if (!stmt) return std::unexpected("statement not found: " + std::string(statement_id));

    auto ns = registry_.get_namespace(statement_id);
    auto* fragments = registry_.get_fragments(ns);
    if (!fragments) fragments = &empty_fragments_;

    dynamic_sql_processor processor(cli_.current_format_opts());
    return processor.process(*stmt, ctx, *fragments);
}

} // namespace cnetmod::mysql::orm
