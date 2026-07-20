/// cnetmod.protocol.modbus:udp_server — Modbus UDP Server Implementation
/// Full-featured async Modbus UDP server

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.modbus:udp_server;

import std;
import :types;
import :data_store;
import :request_handler;
import cnetmod.io.io_context;
import cnetmod.protocol.udp;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.executor.async_op;

namespace cnetmod::modbus {

// =============================================================================
// Modbus UDP Server
// =============================================================================

export class udp_server {
public:
  udp_server(io_context &ctx, data_store &store);

  udp_server(const udp_server &) = delete;
  auto operator=(const udp_server &) -> udp_server & = delete;

  // ── Bind to address and port ──
  auto listen(std::string_view host, std::uint16_t port,
              socket_options opts = {.reuse_address = true,
                                     .non_blocking = true})
      -> task<std::error_code>;

  // ── Start receiving requests ──
  auto async_run() -> task<void>;

  // ── Stop server ──
  void stop();

  // ── Get server info ──
  auto is_running() const -> bool;
  auto get_host() const -> const std::string &;
  auto get_port() const -> std::uint16_t;
  auto get_request_count() const -> std::size_t;

private:
  io_context &ctx_;
  socket socket_;
  request_handler handler_;
  bool running_;
  std::string host_;
  std::uint16_t port_ = 502;
  std::atomic<std::size_t> request_count_{0};
};

} // namespace cnetmod::modbus
