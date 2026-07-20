/// cnetmod.protocol.modbus:tcp_server — Modbus TCP Server Implementation
/// Full-featured async Modbus TCP server

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.modbus:tcp_server;

import std;
import :types;
import :data_store;
import :request_handler;
import cnetmod.io.io_context;
import cnetmod.protocol.tcp;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.executor.async_op;

namespace cnetmod::modbus {

// =============================================================================
// Modbus TCP Server
// =============================================================================

export class tcp_server {
public:
  tcp_server(io_context &ctx, data_store &store);

  tcp_server(const tcp_server &) = delete;
  auto operator=(const tcp_server &) -> tcp_server & = delete;

  // ── Listen on address and port ──
  auto listen(std::string_view host, std::uint16_t port,
              socket_options opts = {.reuse_address = true,
                                     .non_blocking = true})
      -> task<std::error_code>;

  // ── Start accepting connections ──
  auto async_run() -> task<void>;

  // ── Stop server ──
  void stop();

  // ── Get server info ──
  auto is_running() const -> bool;
  auto get_host() const -> const std::string &;
  auto get_port() const -> std::uint16_t;
  auto get_client_count() const -> std::size_t;

private:
  io_context &ctx_;
  socket acceptor_;
  request_handler handler_;
  bool running_;
  std::string host_;
  std::uint16_t port_ = 502;
  std::atomic<std::size_t> client_count_{0};

  auto read_exact(socket &client_socket, mutable_buffer buf)
      -> task<std::expected<void, std::error_code>>;

  // ── Handle individual client connection ──
  auto handle_client(socket client_socket) -> task<void>;
};

} // namespace cnetmod::modbus
