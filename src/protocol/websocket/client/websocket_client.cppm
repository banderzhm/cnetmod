module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.websocket:client;

import std; // client API declarations
import :types;
import :connection;
import cnetmod.io.io_context;
import cnetmod.coro.task;

namespace cnetmod::ws {

export struct client_options {
  connect_options connect;
  std::chrono::steady_clock::duration heartbeat_interval{0};
  std::vector<std::byte> heartbeat_payload;
};

export class client {
public:
  explicit client(io_context &ctx) noexcept;
  ~client() = default;
  client(const client &) = delete;
  auto operator=(const client &) -> client & = delete;
  client(client &&) noexcept = default;
  auto operator=(client &&) noexcept -> client & = delete;

  [[nodiscard]] auto connect(std::string_view url,
                             const client_options &opts = {})
      -> task<std::expected<void, std::error_code>>;
  [[nodiscard]] auto send_text(std::string_view text)
      -> task<std::expected<void, std::error_code>>;
  [[nodiscard]] auto send_binary(std::span<const std::byte> data)
      -> task<std::expected<void, std::error_code>>;
  [[nodiscard]] auto ping(std::span<const std::byte> payload = {})
      -> task<std::expected<void, std::error_code>>;
  [[nodiscard]] auto recv() -> task<std::expected<ws_message, std::error_code>>;
  [[nodiscard]] auto close(std::uint16_t code = close_code::normal,
                           std::string_view reason = "")
      -> task<std::expected<void, std::error_code>>;
  [[nodiscard]] auto run_heartbeat()
      -> task<std::expected<void, std::error_code>>;
  [[nodiscard]] auto is_open() const noexcept -> bool;
  [[nodiscard]] auto native_connection() noexcept -> connection &;
  [[nodiscard]] auto native_connection() const noexcept -> const connection &;

private:
  io_context &ctx_;
  connection conn_;
  client_options options_;
};

} // namespace cnetmod::ws
