module;

export module cnetmod.protocol.socks5:client;

import std;
import :types;
import cnetmod.core.socket;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;

namespace cnetmod::socks5 {

// =============================================================================
// SOCKS5 Client
// =============================================================================

export class client {
public:
  explicit client(io_context &ctx);

  /// Connect to SOCKS5 proxy server
  [[nodiscard]] auto connect(std::string_view proxy_host,
                             std::uint16_t proxy_port)
      -> task<std::expected<void, std::error_code>>;

  /// Authenticate with username/password
  [[nodiscard]] auto authenticate(std::string_view username,
                                  std::string_view password)
      -> task<std::expected<void, std::error_code>>;

  /// Connect to target through SOCKS5 proxy
  [[nodiscard]] auto connect_target(std::string_view target_host,
                                    std::uint16_t target_port)
      -> task<std::expected<void, std::error_code>>;

  /// Request SOCKS5 BIND and return the server-side bind endpoint.
  /// A second response is received after the inbound peer connects.
  [[nodiscard]] auto bind(std::string_view target_host,
                          std::uint16_t target_port)
      -> task<std::expected<socks5_address, std::error_code>>;

  /// Wait for the second SOCKS5 BIND response after the remote peer connects.
  [[nodiscard]] auto wait_bind_peer()
      -> task<std::expected<socks5_address, std::error_code>>;

  /// Request UDP ASSOCIATE and return the UDP relay endpoint.
  [[nodiscard]] auto udp_associate(std::string_view client_host = "0.0.0.0",
                                   std::uint16_t client_port = 0)
      -> task<std::expected<socks5_address, std::error_code>>;

  /// Get the underlying socket (after successful connection)
  [[nodiscard]] auto &socket();
  [[nodiscard]] auto &socket() const;

  /// Release socket ownership (for transferring to other protocols)
  [[nodiscard]] auto release_socket() -> cnetmod::socket;

  /// Close connection
  void close();

private:
  [[nodiscard]] auto request(command cmd, std::string_view host,
                             std::uint16_t port)
      -> task<std::expected<socks5_response, std::error_code>>;

  io_context &ctx_;
  cnetmod::socket sock_;
  auth_method selected_auth_ = auth_method::no_auth;
};

} // namespace cnetmod::socks5
