module;

#include <cnetmod/config.hpp>
#include <cstring>

export module cnetmod.protocol.mysql:connection_client;

import std;
import :types;
import :protocol;
import :auth;
import :wire_deserialization;
import :wire_serialization;
import :format_sql;
import :diagnostics;
import :pipeline;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.dns;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cnetmod::mysql {

// =============================================================================
// mysql::client — Async MySQL client
// =============================================================================

export class client {
public:
  explicit client(io_context &ctx) noexcept;

  // ── Connection ────────────────────────────────────────────────

  auto connect(connect_options opts = {}) -> task<result_set>;

  // ── COM_QUERY ────────────────────────────────────────────

  auto query(std::string_view sql) -> task<result_set>;

  /// execute is an alias for query
  auto execute(std::string_view sql) -> task<result_set>;

  /// execute with_params — Client-side SQL formatting then execute (COM_QUERY)
  auto execute(with_params_t wp) -> task<result_set>;

  /// Get current connection's format options
  auto current_format_opts() const noexcept -> const format_options &;

  // ── Multi-function: start_execution ─────────────────

  auto start_execution(std::string_view sql, execution_state &st) -> task<void>;

  auto start_execution(with_params_t wp, execution_state &st) -> task<void>;

  // ── Multi-function: read_some_rows ──────────────────

  auto read_some_rows(execution_state &st) -> task<std::vector<row>>;

  // ── Multi-function: read_resultset_head ─────────────

  auto read_resultset_head(execution_state &st) -> task<void>;

  // ── run_pipeline — Batch execute multiple commands (experimental)────

  auto run_pipeline(const pipeline_request &req,
                    std::vector<stage_response> &responses) -> task<void>;

  // ── COM_STMT_PREPARE ─────────────────────────────────

  auto prepare(std::string_view sql)
      -> task<std::expected<statement, std::string>>;

  // ── COM_STMT_EXECUTE ─────────────────────────────────────

  auto execute_stmt(const statement &stmt,
                    std::span<const param_value> params = {})
      -> task<result_set>;

  // ── COM_STMT_CLOSE ───────────────────────────────────────

  auto close_stmt(const statement &stmt) -> task<void>;

  // ── COM_PING ─────────────────────────────────────────────

  auto ping() -> task<result_set>;

  // ── COM_RESET_CONNECTION ─────────────────────────────────

  auto reset_connection() -> task<result_set>;

  // ── COM_QUIT ─────────────────────────────────────────────

  auto quit() -> task<void>;

  auto is_open() const noexcept -> bool;

  auto reconnect() -> task<result_set>;

  [[nodiscard]] auto last_error() const noexcept -> std::error_code;

  [[nodiscard]] auto secure_channel() const noexcept -> bool;

  // ── Transaction ──────────────────────────────────────────────

  /// Execute a function within a transaction (auto commit/rollback)
  template <typename Func>
    requires std::invocable<Func> && requires(Func f) {
      { f() } -> std::same_as<task<void>>;
    }
  auto transaction(Func &&func) -> task<result_set> {
    result_set err_rs;
    if (!connected_) {
      err_rs.error_msg = "not connected";
      co_return err_rs;
    }

    // Start transaction
    auto start_rs = co_await execute("START TRANSACTION");
    if (start_rs.is_err())
      co_return start_rs;

    // Execute function and handle result
    bool success = false;
    std::string error_msg;

    try {
      co_await func();
      success = true;
    } catch (const std::exception &e) {
      error_msg = std::string("transaction failed: ") + e.what();
    } catch (...) {
      error_msg = "transaction failed: unknown exception";
    }

    // Commit or rollback based on success
    if (success) {
      auto commit_rs = co_await execute("COMMIT");
      co_return commit_rs;
    } else {
      auto rollback_rs = co_await execute("ROLLBACK");
      (void)rollback_rs;
      err_rs.error_msg = std::move(error_msg);
      co_return err_rs;
    }
  }

  /// Execute a function within a transaction with specific isolation level
  template <typename Func>
    requires std::invocable<Func> && requires(Func f) {
      { f() } -> std::same_as<task<void>>;
    }
  auto transaction(Func &&func, isolation_level level) -> task<result_set> {
    result_set err_rs;
    if (!connected_) {
      err_rs.error_msg = "not connected";
      co_return err_rs;
    }

    // Set isolation level
    std::string sql = "SET TRANSACTION ISOLATION LEVEL ";
    sql.append(isolation_level_to_str(level));
    auto set_rs = co_await execute(sql);
    if (set_rs.is_err())
      co_return set_rs;

    // Start transaction
    auto start_rs = co_await execute("START TRANSACTION");
    if (start_rs.is_err())
      co_return start_rs;

    // Execute function and handle result
    bool success = false;
    std::string error_msg;

    try {
      co_await func();
      success = true;
    } catch (const std::exception &e) {
      error_msg = std::string("transaction failed: ") + e.what();
    } catch (...) {
      error_msg = "transaction failed: unknown exception";
    }

    // Commit or rollback based on success
    if (success) {
      auto commit_rs = co_await execute("COMMIT");
      co_return commit_rs;
    } else {
      auto rollback_rs = co_await execute("ROLLBACK");
      (void)rollback_rs;
      err_rs.error_msg = std::move(error_msg);
      co_return err_rs;
    }
  }

private:
  // ── Transport layer ──────────────────────────────────────────────

  void mark_disconnected(std::error_code ec) noexcept;

  auto do_write(const_buffer buf)
      -> task<std::expected<std::size_t, std::error_code>>;

  auto do_read(mutable_buffer buf)
      -> task<std::expected<std::size_t, std::error_code>>;

  // ── Read exactly N bytes ─────────────────────────────────────

  auto read_exact(std::uint8_t *dst, std::size_t n) -> task<bool>;

  // ── MySQL packet read/write ────────────────────────────────────────

  auto read_packet() -> task<std::vector<std::uint8_t>>;

  auto write_packet(std::span<const std::uint8_t> data, std::uint8_t seqnum)
      -> task<bool>;

  // ── Authentication response handling
  // ────────────────────────────────────────

  auto handle_auth_response(std::string_view password) -> task<result_set>;

  // ── read_resultset_head_impl (internal, for streaming)───────

  auto read_resultset_head_impl(execution_state &st,
                                [[maybe_unused]] bool binary) -> task<void>;

  // ── Result set reading (text / binary)─────────────────────

  auto read_result_set(bool binary) -> task<result_set>;

  // ── Members ────────────────────────────────────────────────

  io_context &ctx_;
  socket sock_;
  bool connected_ = false;
  bool secure_channel_ = false;
  std::error_code last_io_ec_{};
  std::uint8_t seq_ = 0;
  std::uint32_t server_caps_ = 0;
  std::uint32_t client_caps_ = 0;
  std::string password_;
  std::string auth_plugin_;
  std::vector<std::uint8_t> auth_scramble_;
  format_options format_opts_;
  connect_options last_opts_;

  // Read buffer
  std::array<std::uint8_t, 8192> rbuf_{};
  std::size_t rbuf_pos_ = 0;
  std::size_t rbuf_len_ = 0;

#ifdef CNETMOD_HAS_SSL
  std::unique_ptr<ssl_context> ssl_ctx_;
  std::unique_ptr<ssl_stream> ssl_;
#endif
};

} // namespace cnetmod::mysql
