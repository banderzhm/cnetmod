module cnetmod.protocol.websocket; // implementation unit

import :client;
import cnetmod.coro.timer;

namespace cnetmod::ws {
client::client(io_context &ctx) noexcept : ctx_(ctx), conn_(ctx) {}

auto client::connect(std::string_view url, const client_options &opts)
    -> task<std::expected<void, std::error_code>> {
  options_ = opts;
  co_return co_await conn_.async_connect(url, opts.connect);
}

auto client::send_text(std::string_view text)
    -> task<std::expected<void, std::error_code>> {
  co_return co_await conn_.async_send_text(text);
}

auto client::send_binary(std::span<const std::byte> data)
    -> task<std::expected<void, std::error_code>> {
  co_return co_await conn_.async_send_binary(data);
}

auto client::ping(std::span<const std::byte> payload)
    -> task<std::expected<void, std::error_code>> {
  co_return co_await conn_.async_ping(payload);
}

auto client::recv() -> task<std::expected<ws_message, std::error_code>> {
  co_return co_await conn_.async_recv();
}

auto client::close(std::uint16_t code, std::string_view reason)
    -> task<std::expected<void, std::error_code>> {
  co_return co_await conn_.async_close(code, reason);
}

auto client::run_heartbeat() -> task<std::expected<void, std::error_code>> {
  if (options_.heartbeat_interval <=
      std::chrono::steady_clock::duration::zero())
    co_return {};
  steady_timer timer{ctx_};
  while (conn_.is_open()) {
    auto wait = co_await timer.async_wait(options_.heartbeat_interval);
    if (!wait)
      co_return std::unexpected(wait.error());
    auto pong = co_await conn_.async_ping(options_.heartbeat_payload);
    if (!pong)
      co_return std::unexpected(pong.error());
  }
  co_return {};
}

auto client::is_open() const noexcept -> bool { return conn_.is_open(); }
auto client::native_connection() noexcept -> connection & { return conn_; }
auto client::native_connection() const noexcept -> const connection & {
  return conn_;
}
} // namespace cnetmod::ws
