module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.modbus;

import std;
import cnetmod.io.io_context;
import cnetmod.protocol.udp;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.executor.async_op;
import :udp_server;

namespace cnetmod::modbus {

udp_server::udp_server(io_context &context, data_store &store)
    : ctx_(context), handler_(store), running_(false) {}

auto udp_server::listen(std::string_view host, std::uint16_t port,
                        socket_options options) -> task<std::error_code> {
  host_ = host;
  port_ = port;
  const auto address = ip_address::from_string(host);
  if (!address)
    co_return address.error();
  auto socket_result = socket::create(address->is_v4() ? address_family::ipv4
                                                       : address_family::ipv6,
                                      socket_type::datagram);
  if (!socket_result)
    co_return socket_result.error();
  socket_ = std::move(*socket_result);
  options.reuse_address = true;
  options.non_blocking = true;
  if (const auto result = socket_.apply_options(options); !result)
    co_return result.error();
  if (const auto result = socket_.bind(endpoint{*address, port}); !result)
    co_return result.error();
  co_return std::error_code{};
}

auto udp_server::async_run() -> task<void> {
  running_ = true;
  while (running_ && socket_.is_open()) {
    std::vector<std::uint8_t> buffer(512);
    endpoint remote_endpoint;
    mutable_buffer receive_buffer{reinterpret_cast<std::byte *>(buffer.data()),
                                  buffer.size()};
    const auto received =
        co_await async_recvfrom(ctx_, socket_, receive_buffer, remote_endpoint);
    if (!received) {
      if (running_)
        co_await async_sleep(ctx_, std::chrono::milliseconds(10));
      continue;
    }
    buffer.resize(*received);
    request_count_.fetch_add(1, std::memory_order_relaxed);
    const auto parsed = modbus_response::parse(buffer);
    if (!parsed)
      continue;
    modbus_request request{
        .header = parsed->header,
        .func_code = parsed->func_code,
        .data = parsed->data,
    };
    const auto response = handler_.handle(request);
    const auto encoded = response.serialize();
    co_await async_sendto(
        ctx_, socket_,
        {reinterpret_cast<const std::byte *>(encoded.data()), encoded.size()},
        remote_endpoint);
  }
}

void udp_server::stop() {
  running_ = false;
  socket_.close();
}

auto udp_server::is_running() const -> bool { return running_; }
auto udp_server::get_host() const -> const std::string & { return host_; }
auto udp_server::get_port() const -> std::uint16_t { return port_; }
auto udp_server::get_request_count() const -> std::size_t {
  return request_count_.load();
}

} // namespace cnetmod::modbus
