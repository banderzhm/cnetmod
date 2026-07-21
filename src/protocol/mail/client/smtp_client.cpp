/// SMTP client implementation.  Keep protocol details out of the exported
/// module interface so consumers do not rebuild for implementation changes.
module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.mail;

import std;
import cnetmod.core.buffer;
import cnetmod.core.dns;
import cnetmod.core.socket;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;
import cnetmod.protocol.tcp;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :client;
import :types;
import :codec;

namespace cnetmod::mail {
namespace {

constexpr std::size_t max_reply_bytes = 64 * 1024;

auto base64_encode(std::string_view input) -> std::string {
  static constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve(((input.size() + 2) / 3) * 4);
  for (std::size_t offset = 0; offset < input.size(); offset += 3) {
    const auto remaining = input.size() - offset;
    const auto first = static_cast<unsigned char>(input[offset]);
    const auto second =
        remaining > 1 ? static_cast<unsigned char>(input[offset + 1]) : 0;
    const auto third =
        remaining > 2 ? static_cast<unsigned char>(input[offset + 2]) : 0;
    result.push_back(alphabet[first >> 2]);
    result.push_back(alphabet[((first & 0x03U) << 4U) | (second >> 4U)]);
    result.push_back(remaining > 1
                         ? alphabet[((second & 0x0FU) << 2U) | (third >> 6U)]
                         : '=');
    result.push_back(remaining > 2 ? alphabet[third & 0x3FU] : '=');
  }
  return result;
}

auto reply_error(const reply &value, std::string_view action) -> std::string {
  return std::string(action) + " failed (" + std::to_string(value.code) +
         "): " + value.text();
}

} // namespace

struct client::impl {
  explicit impl(io_context &context, client_options client_options) noexcept
      : context_(context), options_(std::move(client_options)),
        connection_(context) {}

  auto connect(std::string_view host, std::uint16_t port)
      -> task<std::expected<void, std::string>>;
  auto authenticate(std::string_view username, std::string_view password,
                    auth_mechanism mechanism)
      -> task<std::expected<void, std::string>>;
  auto send(const envelope &sender_envelope, const message &mail)
      -> task<std::expected<void, std::string>>;
  auto quit() -> task<std::expected<void, std::string>>;
  void close() noexcept;
  [[nodiscard]] auto is_open() const noexcept -> bool;

  auto write(std::string_view text) -> task<std::expected<void, std::string>>;
  auto read(std::span<std::byte> output)
      -> task<std::expected<std::size_t, std::error_code>>;
  auto read_line() -> task<std::expected<std::string, std::string>>;
  auto read_reply() -> task<std::expected<reply, std::string>>;
  auto command(std::string_view verb,
               std::span<const std::string_view> arguments)
      -> task<std::expected<reply, std::string>>;
  auto expect(std::string_view verb,
              std::span<const std::string_view> arguments,
              std::uint16_t expected)
      -> task<std::expected<reply, std::string>>;
  auto identify() -> task<std::expected<void, std::string>>;
  auto begin_tls() -> task<std::expected<void, std::string>>;

  io_context &context_;
  client_options options_;
  tcp::connection connection_;
  std::string host_;
  std::string receive_buffer_;
  bool identified_ = false;
#ifdef CNETMOD_HAS_SSL
  std::unique_ptr<ssl_context> ssl_context_;
  std::unique_ptr<ssl_stream> ssl_stream_;
#endif
};

client::client(io_context &context, client_options options) noexcept
    : impl_(std::make_unique<impl>(context, std::move(options))) {}
client::~client() = default;
client::client(client &&) noexcept = default;
auto client::operator=(client &&) noexcept -> client & = default;

auto client::connect(std::string_view host, std::uint16_t port)
    -> task<std::expected<void, std::string>> {
  co_return co_await impl_->connect(host, port);
}
auto client::authenticate(std::string_view username, std::string_view password,
                          auth_mechanism mechanism)
    -> task<std::expected<void, std::string>> {
  co_return co_await impl_->authenticate(username, password, mechanism);
}
auto client::send(const envelope &sender_envelope, const message &mail)
    -> task<std::expected<void, std::string>> {
  co_return co_await impl_->send(sender_envelope, mail);
}
auto client::quit() -> task<std::expected<void, std::string>> {
  co_return co_await impl_->quit();
}
void client::close() noexcept { impl_->close(); }
auto client::is_open() const noexcept -> bool { return impl_->is_open(); }
auto client::options() const noexcept -> const client_options & {
  return impl_->options_;
}

auto client::impl::connect(std::string_view host, std::uint16_t port)
    -> task<std::expected<void, std::string>> {
  close();
  host_ = host;
  const auto target_port = port == 0 ? options_.port : port;
  auto connected =
      co_await async_connect_happy_eyeballs(context_, host, target_port);
  if (!connected)
    co_return std::unexpected("SMTP connect failed: " +
                              connected.error().message());
  connection_ = tcp::connection{context_, std::move(connected->sock)};

  if (options_.tls) {
    auto upgraded = co_await begin_tls();
    if (!upgraded)
      co_return upgraded;
  }

  auto greeting = co_await read_reply();
  if (!greeting) {
    close();
    co_return std::unexpected(greeting.error());
  }
  if (greeting->code != 220) {
    const auto error = reply_error(*greeting, "SMTP greeting");
    close();
    co_return std::unexpected(error);
  }

  auto hello = co_await identify();
  if (!hello) {
    close();
    co_return hello;
  }
  if (options_.starttls && !options_.tls) {
    auto upgrade = co_await expect("STARTTLS", {}, 220);
    if (!upgrade) {
      close();
      co_return std::unexpected(upgrade.error());
    }
    auto tls = co_await begin_tls();
    if (!tls) {
      close();
      co_return tls;
    }
    identified_ = false;
    auto post_tls_hello = co_await identify();
    if (!post_tls_hello) {
      close();
      co_return post_tls_hello;
    }
  }
  co_return std::expected<void, std::string>{};
}

auto client::impl::authenticate(std::string_view username,
                                std::string_view password,
                                auth_mechanism mechanism)
    -> task<std::expected<void, std::string>> {
  if (!is_open() || !identified_)
    co_return std::unexpected(std::string("SMTP client is not connected"));
  if (username.empty() || password.empty())
    co_return std::unexpected(
        std::string("SMTP credentials must not be empty"));

  if (mechanism == auth_mechanism::plain) {
    std::string payload;
    payload.reserve(username.size() + password.size() + 2);
    payload.push_back('\0');
    payload.append(username);
    payload.push_back('\0');
    payload.append(password);
    const auto encoded = base64_encode(payload);
    const std::array arguments{std::string_view{"PLAIN"},
                               std::string_view{encoded}};
    auto response = co_await command("AUTH", arguments);
    if (!response)
      co_return std::unexpected(response.error());
    if (response->code != 235)
      co_return std::unexpected(reply_error(*response, "AUTH PLAIN"));
    co_return std::expected<void, std::string>{};
  }

  if (mechanism == auth_mechanism::login) {
    const std::array login{std::string_view{"LOGIN"}};
    auto challenge = co_await command("AUTH", login);
    if (!challenge)
      co_return std::unexpected(challenge.error());
    if (challenge->code != 334)
      co_return std::unexpected(reply_error(*challenge, "AUTH LOGIN"));
    const auto encoded_user = base64_encode(username);
    auto user_reply = co_await write(encoded_user + "\r\n");
    if (!user_reply)
      co_return user_reply;
    challenge = co_await read_reply();
    if (!challenge)
      co_return std::unexpected(challenge.error());
    if (challenge->code != 334)
      co_return std::unexpected(reply_error(*challenge, "AUTH LOGIN username"));
    const auto encoded_password = base64_encode(password);
    auto password_reply = co_await write(encoded_password + "\r\n");
    if (!password_reply)
      co_return password_reply;
    auto complete = co_await read_reply();
    if (!complete)
      co_return std::unexpected(complete.error());
    if (complete->code != 235)
      co_return std::unexpected(reply_error(*complete, "AUTH LOGIN password"));
    co_return std::expected<void, std::string>{};
  }

  co_return std::unexpected(
      std::string("unsupported SMTP authentication mechanism"));
}

auto client::impl::send(const envelope &sender_envelope, const message &mail)
    -> task<std::expected<void, std::string>> {
  if (!is_open() || !identified_)
    co_return std::unexpected(std::string("SMTP client is not connected"));
  if (!sender_envelope.valid())
    co_return std::unexpected(std::string("invalid SMTP envelope"));

  const auto mail_from = "FROM:<" + sender_envelope.sender + ">";
  const std::array mail_arguments{std::string_view{mail_from}};
  auto accepted = co_await expect("MAIL", mail_arguments, 250);
  if (!accepted)
    co_return std::unexpected(accepted.error());

  for (const auto &recipient : sender_envelope.recipients) {
    const auto rcpt_to = "TO:<" + recipient + ">";
    const std::array recipient_arguments{std::string_view{rcpt_to}};
    auto recipient_reply = co_await command("RCPT", recipient_arguments);
    if (!recipient_reply)
      co_return std::unexpected(recipient_reply.error());
    if (recipient_reply->code != 250 && recipient_reply->code != 251)
      co_return std::unexpected(reply_error(*recipient_reply, "RCPT TO"));
  }

  auto data = co_await expect("DATA", {}, 354);
  if (!data)
    co_return std::unexpected(data.error());

  std::string wire;
  for (const auto &[name, value] : mail.headers) {
    wire.append(name);
    wire.append(": ");
    wire.append(value);
    wire.append("\r\n");
  }
  wire.append("\r\n");
  wire.append(mail.body);
  if (!wire.ends_with("\r\n"))
    wire.append("\r\n");
  auto stuffed = dot_stuff(wire);
  if (!stuffed)
    co_return std::unexpected("failed to encode SMTP DATA: " + stuffed.error());
  stuffed->append(".\r\n");
  auto sent = co_await write(*stuffed);
  if (!sent)
    co_return sent;
  auto completed = co_await read_reply();
  if (!completed)
    co_return std::unexpected(completed.error());
  if (completed->code != 250)
    co_return std::unexpected(reply_error(*completed, "SMTP DATA"));
  co_return std::expected<void, std::string>{};
}

auto client::impl::quit() -> task<std::expected<void, std::string>> {
  if (!is_open())
    co_return std::expected<void, std::string>{};
  auto response = co_await expect("QUIT", {}, 221);
  close();
  if (!response)
    co_return std::unexpected(response.error());
  co_return std::expected<void, std::string>{};
}

void client::impl::close() noexcept {
  identified_ = false;
  receive_buffer_.clear();
#ifdef CNETMOD_HAS_SSL
  ssl_stream_.reset();
  ssl_context_.reset();
#endif
  connection_.close();
}

auto client::impl::is_open() const noexcept -> bool {
  return connection_.is_open();
}

auto client::impl::write(std::string_view text)
    -> task<std::expected<void, std::string>> {
#ifdef CNETMOD_HAS_SSL
  if (ssl_stream_) {
    auto result = co_await ssl_stream_->async_write_all(buffer(text));
    if (!result)
      co_return std::unexpected(result.error().message());
    co_return std::expected<void, std::string>{};
  }
#endif
  auto result = co_await async_write_all(context_, connection_.native_socket(),
                                         buffer(text));
  if (!result)
    co_return std::unexpected(result.error().message());
  co_return std::expected<void, std::string>{};
}

auto client::impl::read(std::span<std::byte> output)
    -> task<std::expected<std::size_t, std::error_code>> {
#ifdef CNETMOD_HAS_SSL
  if (ssl_stream_)
    co_return co_await ssl_stream_->async_read(
        mutable_buffer{output.data(), output.size()});
#endif
  co_return co_await async_read(context_, connection_.native_socket(),
                                mutable_buffer{output.data(), output.size()});
}

auto client::impl::read_line()
    -> task<std::expected<std::string, std::string>> {
  while (true) {
    const auto delimiter = receive_buffer_.find("\r\n");
    if (delimiter != std::string::npos) {
      auto line = receive_buffer_.substr(0, delimiter);
      receive_buffer_.erase(0, delimiter + 2);
      co_return line;
    }
    if (receive_buffer_.size() >= max_reply_bytes)
      co_return std::unexpected(std::string("SMTP reply exceeds limit"));
    std::array<std::byte, 2048> chunk{};
    auto bytes = co_await read(chunk);
    if (!bytes)
      co_return std::unexpected(bytes.error().message());
    if (*bytes == 0)
      co_return std::unexpected(std::string("SMTP peer closed connection"));
    receive_buffer_.append(reinterpret_cast<const char *>(chunk.data()),
                           *bytes);
  }
}

auto client::impl::read_reply() -> task<std::expected<reply, std::string>> {
  auto first = co_await read_line();
  if (!first)
    co_return std::unexpected(first.error());
  if (first->size() < 4 ||
      !std::isdigit(static_cast<unsigned char>((*first)[0])) ||
      !std::isdigit(static_cast<unsigned char>((*first)[1])) ||
      !std::isdigit(static_cast<unsigned char>((*first)[2])))
    co_return std::unexpected(std::string("malformed SMTP reply"));
  const auto code = first->substr(0, 3);
  std::string raw = *first;
  raw.append("\r\n");
  if ((*first)[3] == '-') {
    while (true) {
      auto line = co_await read_line();
      if (!line)
        co_return std::unexpected(line.error());
      raw.append(*line);
      raw.append("\r\n");
      if (line->size() >= 4 && line->substr(0, 3) == code && (*line)[3] == ' ')
        break;
      if (raw.size() > max_reply_bytes)
        co_return std::unexpected(std::string("SMTP reply exceeds limit"));
    }
  } else if ((*first)[3] != ' ') {
    co_return std::unexpected(std::string("malformed SMTP reply separator"));
  }
  auto decoded = parse_reply(raw);
  if (!decoded)
    co_return std::unexpected(decoded.error());
  co_return std::move(*decoded);
}

auto client::impl::command(std::string_view verb,
                           std::span<const std::string_view> arguments)
    -> task<std::expected<reply, std::string>> {
  auto payload = serialize_command(verb, arguments);
  if (!payload)
    co_return std::unexpected(payload.error());
  auto sent = co_await write(*payload);
  if (!sent)
    co_return std::unexpected(sent.error());
  co_return co_await read_reply();
}

auto client::impl::expect(std::string_view verb,
                          std::span<const std::string_view> arguments,
                          std::uint16_t expected)
    -> task<std::expected<reply, std::string>> {
  auto response = co_await command(verb, arguments);
  if (!response)
    co_return std::unexpected(response.error());
  if (response->code != expected)
    co_return std::unexpected(reply_error(*response, verb));
  co_return std::move(*response);
}

auto client::impl::identify() -> task<std::expected<void, std::string>> {
  const auto name = options_.hostname.empty()
                        ? std::string_view{"localhost"}
                        : std::string_view{options_.hostname};
  const std::array arguments{name};
  auto response = co_await command("EHLO", arguments);
  if (!response)
    co_return std::unexpected(response.error());
  if (response->code != 250)
    co_return std::unexpected(reply_error(*response, "EHLO"));
  identified_ = true;
  co_return std::expected<void, std::string>{};
}

auto client::impl::begin_tls() -> task<std::expected<void, std::string>> {
#ifdef CNETMOD_HAS_SSL
  auto context = ssl_context::client();
  if (!context)
    co_return std::unexpected("SMTP TLS context: " + context.error().message());
  ssl_context_ = std::make_unique<ssl_context>(std::move(*context));
  ssl_context_->set_verify_peer(options_.verify);
  if (options_.verify)
    (void)ssl_context_->set_default_ca();
  ssl_stream_ = std::make_unique<ssl_stream>(*ssl_context_, context_,
                                             connection_.native_socket());
  ssl_stream_->set_connect_state();
  ssl_stream_->set_hostname(options_.hostname.empty() ? host_
                                                      : options_.hostname);
  auto handshake = co_await ssl_stream_->async_handshake();
  if (!handshake) {
    ssl_stream_.reset();
    ssl_context_.reset();
    co_return std::unexpected("SMTP TLS handshake: " +
                              handshake.error().message());
  }
  co_return std::expected<void, std::string>{};
#else
  co_return std::unexpected(
      std::string("SMTP TLS requested but SSL is unavailable"));
#endif
}

} // namespace cnetmod::mail
