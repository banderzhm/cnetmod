/// cnetmod.protocol.mail:server -- asynchronous SMTP server API.
///
/// The server implements the RFC 5321 transaction subset needed for message
/// submission: HELO/EHLO, MAIL FROM, RCPT TO, DATA, RSET, NOOP and QUIT.

export module cnetmod.protocol.mail:server;

import std;
import :types;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.coro.task;

namespace cnetmod::mail {

export struct server_options {
  std::string hostname = "localhost";
  std::size_t max_message_size = 25U * 1024U * 1024U;
  std::size_t max_recipients = 100;
  bool require_auth = false;
};

export using recipient_handler =
    std::function<task<std::expected<void, std::error_code>>(const envelope &,
                                                             const message &)>;

export using authenticator =
    std::function<bool(std::string_view username, std::string_view password)>;

export class server {
public:
  server(io_context &ctx, server_options options = {});
  ~server();

  server(const server &) = delete;
  auto operator=(const server &) -> server & = delete;
  server(server &&) = delete;
  auto operator=(server &&) -> server & = delete;

  [[nodiscard]] auto listen(std::string_view host, std::uint16_t port)
      -> std::expected<void, std::error_code>;

  void set_message_handler(recipient_handler handler);
  void set_authenticator(authenticator fn);

  auto run() -> task<void>;
  void stop();

  [[nodiscard]] auto active_connections() const noexcept -> std::size_t;

private:
  class impl;
  std::unique_ptr<impl> impl_;
};

} // namespace cnetmod::mail
