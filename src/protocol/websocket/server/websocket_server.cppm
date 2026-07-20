module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.websocket:server;

import std; // server API declarations
import :types;
import :frame;
import :handshake;
import :connection;
import cnetmod.protocol.http;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.executor.async_op;
import cnetmod.executor.pool;
import cnetmod.protocol.tcp;

#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cnetmod::ws {
// =============================================================================
// Route Segment Parsing
// =============================================================================

namespace detail {
enum class seg_kind { exact, param, wildcard };

struct seg {
  seg_kind kind;
  std::string value;
};

auto parse_ws_pattern(std::string_view pattern) -> std::vector<seg>;
auto split_ws_path(std::string_view path) -> std::vector<std::string_view>;

struct ws_route_params {
  std::unordered_map<std::string, std::string> named;
  std::string wildcard;

  [[nodiscard]] auto get(std::string_view key) const noexcept
      -> std::string_view;
};

auto try_ws_match(const std::vector<seg> &segs,
                  const std::vector<std::string_view> &parts,
                  ws_route_params &out) -> bool;
} // namespace detail

// =============================================================================
// ws_context — WebSocket Connection Context
// =============================================================================

export class ws_context {
public:
  ws_context(connection &conn, std::string path, http::header_map headers,
             std::string query, detail::ws_route_params params);

  [[nodiscard]] auto path() const noexcept -> std::string_view;
  [[nodiscard]] auto query_string() const noexcept -> std::string_view;
  [[nodiscard]] auto headers() const noexcept -> const http::header_map &;

  [[nodiscard]] auto get_header(std::string_view key) const -> std::string_view;

  [[nodiscard]] auto param(std::string_view name) const noexcept
      -> std::string_view;

  // --- Message Send/Receive ---

  auto send_text(std::string_view text)
      -> task<std::expected<void, std::error_code>>;

  auto send_binary(std::span<const std::byte> data)
      -> task<std::expected<void, std::error_code>>;

  auto recv() -> task<std::expected<ws_message, std::error_code>>;

  auto close(std::uint16_t code = close_code::normal,
             std::string_view reason = "")
      -> task<std::expected<void, std::error_code>>;

  [[nodiscard]] auto is_open() const noexcept -> bool;
  [[nodiscard]] auto raw_connection() noexcept -> connection &;

private:
  connection &conn_;
  std::string path_;
  http::header_map headers_;
  std::string query_;
  detail::ws_route_params params_;
};

export using ws_handler_fn = std::function<task<void>(ws_context &)>;

// =============================================================================
// ws::server
// =============================================================================

export class server {
public:
  /// Single-threaded mode
  explicit server(io_context &ctx);

  /// Multi-core mode: accept on sctx.accept_io(), connections dispatched to
  /// worker io_context
  explicit server(server_context &sctx);

#ifdef CNETMOD_HAS_SSL
  void set_ssl_context(ssl_context &ssl_ctx);
#endif

  auto listen(std::string_view host, std::uint16_t port,
              socket_options opts = {.reuse_address = true})
      -> std::expected<void, std::error_code>;

  void on(std::string_view pattern, ws_handler_fn handler);

  auto run() -> task<void>;

  void stop();

private:
  struct route_entry {
    std::vector<detail::seg> segments;
    ws_handler_fn handler;
  };

  auto handle_connection(socket client, io_context &io) -> task<void>;

  io_context &ctx_;
  server_context *sctx_ = nullptr; // Multi-core mode when non-null
  std::unique_ptr<tcp::acceptor> acc_;
  std::vector<route_entry> routes_;
  bool running_ = false;
#ifdef CNETMOD_HAS_SSL
  ssl_context *ssl_ctx_ = nullptr;
#endif
};
} // namespace cnetmod::ws
