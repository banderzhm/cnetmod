/// SMTP client public API.
///
/// This partition intentionally contains declarations only.  The wire state
/// machine, TLS upgrade and command sequencing live in smtp_client.cpp.
module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mail:client;

import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import :types;

export namespace cnetmod::mail {

struct client_options {
  /// Connect with TLS immediately (SMTPS, normally port 465).
  bool tls = false;
  /// Upgrade a plaintext connection through STARTTLS after EHLO.
  bool starttls = false;
  /// TLS peer name.  An empty value uses the host passed to connect().
  std::string hostname;
  std::uint16_t port = 25;
  bool verify = true;
};

class client {
public:
  explicit client(io_context &context, client_options options = {}) noexcept;
  ~client();

  client(const client &) = delete;
  auto operator=(const client &) -> client & = delete;
  client(client &&) noexcept;
  auto operator=(client &&) noexcept -> client &;

  /// Open the transport, receive the greeting, and identify this client with
  /// EHLO.  TLS is either immediate or upgraded through STARTTLS according to
  /// client_options.
  auto connect(std::string_view host, std::uint16_t port = 0)
      -> task<std::expected<void, std::string>>;

  auto authenticate(std::string_view username, std::string_view password,
                    auth_mechanism mechanism = auth_mechanism::plain)
      -> task<std::expected<void, std::string>>;
  auto send(const envelope &envelope, const message &message)
      -> task<std::expected<void, std::string>>;
  auto quit() -> task<std::expected<void, std::string>>;

  void close() noexcept;
  [[nodiscard]] auto is_open() const noexcept -> bool;
  [[nodiscard]] auto options() const noexcept -> const client_options &;

private:
  struct impl;
  std::unique_ptr<impl> impl_;
};

} // namespace cnetmod::mail
