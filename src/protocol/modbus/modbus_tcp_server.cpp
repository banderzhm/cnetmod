module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.modbus;

import std;
import cnetmod.io.io_context;
import cnetmod.protocol.tcp;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.executor.async_op;
import :tcp_server;

namespace cnetmod::modbus {

tcp_server::tcp_server(io_context &context, data_store &store)
    : ctx_(context), handler_(store), running_(false) {}

auto tcp_server::listen(std::string_view host, std::uint16_t port,
                        socket_options options) -> task<std::error_code> {
  host_ = host;
  port_ = port;
  const auto address = ip_address::from_string(host);
  if (!address)
    co_return address.error();
  auto socket_result = socket::create(address->is_v4() ? address_family::ipv4
                                                       : address_family::ipv6,
                                      socket_type::stream);
  if (!socket_result)
    co_return socket_result.error();
  acceptor_ = std::move(*socket_result);
  options.reuse_address = true;
  options.non_blocking = true;
  if (const auto result = acceptor_.apply_options(options); !result)
    co_return result.error();
  if (const auto result = acceptor_.bind(endpoint{*address, port}); !result)
    co_return result.error();
  if (const auto result = acceptor_.listen(128); !result)
    co_return result.error();
  co_return std::error_code{};
}

auto tcp_server::async_run() -> task<void> {
  running_ = true;
  while (running_) {
    auto accepted = co_await async_accept(ctx_, acceptor_);
    if (!accepted) {
      if (running_)
        co_await async_sleep(ctx_, std::chrono::milliseconds(100));
      continue;
    }
    spawn(ctx_, handle_client(std::move(*accepted)));
  }
}

void tcp_server::stop() {
  running_ = false;
  acceptor_.close();
}

auto tcp_server::is_running() const -> bool { return running_; }
auto tcp_server::get_host() const -> const std::string & { return host_; }
auto tcp_server::get_port() const -> std::uint16_t { return port_; }
auto tcp_server::get_client_count() const -> std::size_t {
  return client_count_.load();
}

auto tcp_server::read_exact(socket &client, mutable_buffer buffer)
    -> task<std::expected<void, std::error_code>> {
  auto *bytes = static_cast<std::byte *>(buffer.data);
  std::size_t received = 0;
  while (received < buffer.size) {
    const auto result = co_await async_read(
        ctx_, client, {bytes + received, buffer.size - received});
    if (!result)
      co_return std::unexpected(result.error());
    if (*result == 0)
      co_return std::unexpected(
          std::make_error_code(std::errc::connection_reset));
    received += *result;
  }
  co_return {};
}

auto tcp_server::handle_client(socket client) -> task<void> {
  client_count_.fetch_add(1, std::memory_order_relaxed);
  while (client.is_open() && running_) {
    std::vector<std::uint8_t> header(7);
    if (!(co_await read_exact(
            client,
            {reinterpret_cast<std::byte *>(header.data()), header.size()})))
      break;
    const auto length = read_uint16_be(header, 4);
    if (length < 2 || length > 256)
      break;
    std::vector<std::uint8_t> payload(6 + length);
    std::copy(header.begin(), header.end(), payload.begin());
    if (length > 1) {
      const auto result = co_await read_exact(
          client, {reinterpret_cast<std::byte *>(payload.data() + 7),
                   static_cast<std::size_t>(length - 1)});
      if (!result)
        break;
    }
    const auto parsed = modbus_response::parse(payload);
    if (!parsed)
      break;
    modbus_request request{.header = parsed->header,
                           .func_code = parsed->func_code,
                           .data = parsed->data};
    const auto response = handler_.handle(request);
    const auto encoded = response.serialize();
    if (!(co_await async_write_all(
            ctx_, client,
            {reinterpret_cast<const std::byte *>(encoded.data()),
             encoded.size()})))
      break;
  }
  client.close();
  client_count_.fetch_sub(1, std::memory_order_relaxed);
}

} // namespace cnetmod::modbus
