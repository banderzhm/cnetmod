module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mysql:pipeline;

import std;
import :types;
import :diagnostics;

namespace cnetmod::mysql {

// =============================================================================
// pipeline stage types (reference: Boost.MySQL pipeline — experimental)
// =============================================================================

export enum class stage_kind : std::uint8_t {
    execute,               // COM_QUERY
    prepare,               // COM_STMT_PREPARE
    close_statement,       // COM_STMT_CLOSE
    reset_connection,      // COM_RESET_CONNECTION
    set_character_set,     // SET NAMES ...
};

// =============================================================================
// pipeline_stage — Single pipeline step
// =============================================================================

struct pipeline_stage {
    stage_kind kind = stage_kind::execute;
    std::string sql;                         // execute / prepare / set_character_set
    std::uint32_t stmt_id = 0;              // close_statement
    std::vector<param_value> params;         // execute with prepared stmt (future)
};

// =============================================================================
// stage_response — Single pipeline step response
// =============================================================================
//
// Similar to variant: can contain statement / result_set / error

export class stage_response {
public:
    stage_response() noexcept = default;

    auto has_statement() const noexcept -> bool { return has_stmt_; }
    auto has_results()   const noexcept -> bool { return has_rs_; }
    auto has_error()     const noexcept -> bool { return !error_msg_.empty() || error_code_ != 0; }

    auto get_statement() const noexcept -> statement { return stmt_; }
    auto get_results()   const noexcept -> const result_set& { return rs_; }
    auto get_results()         noexcept -> result_set& { return rs_; }

    auto error_code() const noexcept -> std::uint16_t { return error_code_; }
    auto error_msg()  const noexcept -> std::string_view { return error_msg_; }
    auto diag()       const noexcept -> const diagnostics& { return diag_; }

    // Internal setter methods
    void set_statement(statement s) {
        stmt_ = s;
        has_stmt_ = true;
        has_rs_ = false;
    }
    void set_results(result_set rs) {
        rs_ = std::move(rs);
        has_rs_ = true;
        has_stmt_ = false;
    }
    void set_error(std::uint16_t code, std::string msg) {
        error_code_ = code;
        error_msg_ = std::move(msg);
        diag_.assign_server(error_msg_);
        has_stmt_ = false;
        has_rs_ = false;
    }
    void set_ok() {
        // Success with no return value (close_statement / reset_connection)
        has_stmt_ = false;
        has_rs_ = false;
        error_code_ = 0;
        error_msg_.clear();
    }

private:
    bool          has_stmt_ = false;
    bool          has_rs_   = false;
    statement     stmt_;
    result_set    rs_;
    std::uint16_t error_code_ = 0;
    std::string   error_msg_;
    diagnostics   diag_;
};

// =============================================================================
// pipeline_request — Batch request builder (reference: Boost.MySQL pipeline_request)
// =============================================================================

export class pipeline_request {
public:
    pipeline_request() = default;

    /// Add a COM_QUERY step
    auto add_execute(std::string sql) -> pipeline_request& {
        pipeline_stage s;
        s.kind = stage_kind::execute;
        s.sql = std::move(sql);
        stages_.push_back(std::move(s));
        return *this;
    }

    /// Add a COM_STMT_PREPARE step
    auto add_prepare(std::string sql) -> pipeline_request& {
        pipeline_stage s;
        s.kind = stage_kind::prepare;
        s.sql = std::move(sql);
        stages_.push_back(std::move(s));
        return *this;
    }

    /// Add a COM_STMT_CLOSE step
    auto add_close_statement(std::uint32_t stmt_id) -> pipeline_request& {
        pipeline_stage s;
        s.kind = stage_kind::close_statement;
        s.stmt_id = stmt_id;
        stages_.push_back(std::move(s));
        return *this;
    }

    /// Add a COM_RESET_CONNECTION step
    auto add_reset_connection() -> pipeline_request& {
        pipeline_stage s;
        s.kind = stage_kind::reset_connection;
        stages_.push_back(std::move(s));
        return *this;
    }

    /// Add a SET NAMES step
    auto add_set_character_set(std::string charset) -> pipeline_request& {
        pipeline_stage s;
        s.kind = stage_kind::set_character_set;
        s.sql = "SET NAMES " + charset;
        stages_.push_back(std::move(s));
        return *this;
    }

    auto stages() const noexcept -> const std::vector<pipeline_stage>& { return stages_; }
    auto size()   const noexcept -> std::size_t { return stages_.size(); }
    auto empty()  const noexcept -> bool { return stages_.empty(); }

    void clear() { stages_.clear(); }

private:
    std::vector<pipeline_stage> stages_;
};

} // namespace cnetmod::mysql
