module cnetmod.protocol.mail;

import std;
import :types;
import :server;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.core.socket;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;
import cnetmod.protocol.tcp;

namespace cnetmod::mail {

namespace {

constexpr std::size_t max_command_line_size = 8192;

auto ascii_lower(std::string_view value) -> std::string {
  std::string lowered(value);
  std::ranges::transform(lowered, lowered.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return lowered;
}

auto trim(std::string_view value) -> std::string_view {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front()))) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back()))) {
    value.remove_suffix(1);
  }
  return value;
}

auto parse_path(std::string_view value, std::string_view prefix,
                bool allow_parameters = false) -> std::optional<std::string> {
  if (!ascii_lower(value).starts_with(prefix)) {
    return std::nullopt;
  }

  value.remove_prefix(prefix.size());
  value = trim(value);
  if (value.size() < 3 || value.front() != '<') {
    return std::nullopt;
  }
  const auto closing_bracket = value.find('>');
  if (closing_bracket == std::string_view::npos ||
      (!allow_parameters && closing_bracket + 1 != value.size())) {
    return std::nullopt;
  }
  const auto address = value.substr(1, closing_bracket - 1);
  if (address.empty() ||
      address.find_first_of("\r\n") != std::string_view::npos) {
    return std::nullopt;
  }
  return std::string(address);
}

auto declared_message_size(std::string_view mail_from_argument)
    -> std::optional<std::size_t> {
  const auto closing_bracket = mail_from_argument.find('>');
  if (closing_bracket == std::string_view::npos) {
    return std::nullopt;
  }
  auto parameters = trim(mail_from_argument.substr(closing_bracket + 1));
  while (!parameters.empty()) {
    const auto separator = parameters.find_first_of(" \t");
    const auto parameter = parameters.substr(0, separator);
    if (ascii_lower(parameter).starts_with("size=")) {
      std::size_t value = 0;
      const auto digits = parameter.substr(5);
      const auto [end, error] =
          std::from_chars(digits.data(), digits.data() + digits.size(), value);
      if (error != std::errc{} || end != digits.data() + digits.size()) {
        return std::nullopt;
      }
      return value;
    }
    if (separator == std::string_view::npos) {
      break;
    }
    parameters = trim(parameters.substr(separator + 1));
  }
  return std::size_t{};
}

auto parse_message(std::string raw) -> message {
  message result;
  const auto header_end = raw.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    result.body = std::move(raw);
    return result;
  }

  std::string_view headers(raw.data(), header_end);
  while (!headers.empty()) {
    const auto line_end = headers.find("\r\n");
    const auto line = headers.substr(0, line_end);
    const auto colon = line.find(':');
    if (colon != std::string_view::npos && colon != 0) {
      result.headers.emplace_back(std::string(trim(line.substr(0, colon))),
                                  std::string(trim(line.substr(colon + 1))));
    }
    if (line_end == std::string_view::npos) {
      break;
    }
    headers.remove_prefix(line_end + 2);
  }
  result.body = raw.substr(header_end + 4);
  return result;
}

auto decode_base64(std::string_view text) -> std::optional<std::string> {
  constexpr std::array<std::int8_t, 256> table = [] {
    std::array<std::int8_t, 256> result{};
    result.fill(-1);
    for (std::int8_t i = 0; i < 26; ++i) {
      result[static_cast<unsigned char>('A' + i)] = i;
      result[static_cast<unsigned char>('a' + i)] =
          static_cast<std::int8_t>(26 + i);
    }
    for (std::int8_t i = 0; i < 10; ++i) {
      result[static_cast<unsigned char>('0' + i)] =
          static_cast<std::int8_t>(52 + i);
    }
    result[static_cast<unsigned char>('+')] = 62;
    result[static_cast<unsigned char>('/')] = 63;
    return result;
  }();

  if (text.empty() || text.size() % 4 != 0) {
    return std::nullopt;
  }

  std::string decoded;
  decoded.reserve((text.size() / 4) * 3);
  for (std::size_t i = 0; i < text.size(); i += 4) {
    const auto first = table[static_cast<unsigned char>(text[i])];
    const auto second = table[static_cast<unsigned char>(text[i + 1])];
    const bool third_padding = text[i + 2] == '=';
    const bool fourth_padding = text[i + 3] == '=';
    const auto third =
        third_padding ? 0 : table[static_cast<unsigned char>(text[i + 2])];
    const auto fourth =
        fourth_padding ? 0 : table[static_cast<unsigned char>(text[i + 3])];
    if (first < 0 || second < 0 || third < 0 || fourth < 0 ||
        (third_padding && !fourth_padding) ||
        ((third_padding || fourth_padding) && i + 4 != text.size())) {
      return std::nullopt;
    }

    const auto bits = (static_cast<std::uint32_t>(first) << 18U) |
                      (static_cast<std::uint32_t>(second) << 12U) |
                      (static_cast<std::uint32_t>(third) << 6U) |
                      static_cast<std::uint32_t>(fourth);
    decoded.push_back(static_cast<char>((bits >> 16U) & 0xffU));
    if (!third_padding) {
      decoded.push_back(static_cast<char>((bits >> 8U) & 0xffU));
    }
    if (!fourth_padding) {
      decoded.push_back(static_cast<char>(bits & 0xffU));
    }
  }
  return decoded;
}

class line_reader {
public:
  line_reader(io_context &io, socket &client) : io_(io), client_(client) {}

  auto read_line() -> task<std::expected<std::string, std::error_code>> {
    for (;;) {
      if (const auto newline = pending_.find('\n');
          newline != std::string::npos) {
        auto line = pending_.substr(0, newline);
        pending_.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        co_return line;
      }
      if (pending_.size() > max_command_line_size) {
        co_return std::unexpected(
            std::make_error_code(std::errc::message_size));
      }

      auto read = co_await async_read(
          io_, client_, mutable_buffer{buffer_.data(), buffer_.size()});
      if (!read) {
        co_return std::unexpected(read.error());
      }
      if (*read == 0) {
        co_return std::unexpected(
            std::make_error_code(std::errc::connection_reset));
      }
      pending_.append(reinterpret_cast<const char *>(buffer_.data()), *read);
    }
  }

private:
  io_context &io_;
  socket &client_;
  std::array<std::byte, 4096> buffer_{};
  std::string pending_;
};

auto write_reply(io_context &io, socket &client, std::string_view reply)
    -> task<bool> {
  const auto write = co_await async_write_all(
      io, client,
      const_buffer{reinterpret_cast<const std::byte *>(reply.data()),
                   reply.size()});
  co_return static_cast<bool>(write);
}

} // namespace

class server::impl {
public:
  impl(io_context &context, server_options server_options)
      : ctx(context), options(std::move(server_options)) {}

  auto handle_connection(socket client) -> task<void> {
    struct active_guard {
      explicit active_guard(std::atomic<std::size_t> &value) : value(value) {
        value.fetch_add(1, std::memory_order_relaxed);
      }
      ~active_guard() { value.fetch_sub(1, std::memory_order_relaxed); }
      std::atomic<std::size_t> &value;
    } guard(active);

    line_reader reader(ctx, client);
    if (!(co_await write_reply(
            ctx, client, "220 " + options.hostname + " ESMTP cnetmod\r\n"))) {
      co_return;
    }

    bool greeted = false;
    bool authenticated = !options.require_auth;
    std::string sender;
    std::vector<std::string> recipients;
    auto reset_transaction = [&] {
      sender.clear();
      recipients.clear();
    };

    while (running.load(std::memory_order_relaxed) && client.is_open()) {
      auto line = co_await reader.read_line();
      if (!line) {
        break;
      }

      const auto space = line->find_first_of(" \t");
      const auto command = ascii_lower(line->substr(0, space));
      const auto argument =
          space == std::string::npos
              ? std::string_view{}
              : trim(std::string_view(*line).substr(space + 1));

      if (command == "quit") {
        (void)co_await write_reply(ctx, client, "221 2.0.0 Bye\r\n");
        break;
      }
      if (command == "noop") {
        if (!(co_await write_reply(ctx, client, "250 2.0.0 OK\r\n"))) {
          break;
        }
        continue;
      }
      if (command == "rset") {
        reset_transaction();
        if (!(co_await write_reply(ctx, client, "250 2.0.0 Reset state\r\n"))) {
          break;
        }
        continue;
      }
      if (command == "helo" || command == "ehlo") {
        if (argument.empty()) {
          if (!(co_await write_reply(ctx, client,
                                     "501 5.5.4 HELO requires a domain\r\n"))) {
            break;
          }
          continue;
        }
        greeted = true;
        reset_transaction();
        const auto reply =
            command == "ehlo"
                ? "250-" + options.hostname + "\r\n250-SIZE " +
                      std::to_string(options.max_message_size) +
                      (authenticator_fn
                           ? "\r\n250-8BITMIME\r\n250 AUTH PLAIN\r\n"
                           : "\r\n250 8BITMIME\r\n")
                : "250 " + options.hostname + "\r\n";
        if (!(co_await write_reply(ctx, client, reply))) {
          break;
        }
        continue;
      }
      if (command == "auth") {
        const auto auth_space = argument.find_first_of(" \t");
        const auto mechanism = ascii_lower(argument.substr(0, auth_space));
        const auto payload = auth_space == std::string_view::npos
                                 ? std::string_view{}
                                 : trim(argument.substr(auth_space + 1));
        if (!greeted) {
          if (!(co_await write_reply(ctx, client,
                                     "503 5.5.1 Send HELO/EHLO first\r\n"))) {
            break;
          }
          continue;
        }
        if (!authenticator_fn || mechanism != "plain" || payload.empty()) {
          if (!(co_await write_reply(
                  ctx, client, "504 5.5.4 Unsupported authentication\r\n"))) {
            break;
          }
          continue;
        }
        const auto decoded = decode_base64(payload);
        if (!decoded) {
          if (!(co_await write_reply(ctx, client,
                                     "501 5.5.2 Invalid AUTH payload\r\n"))) {
            break;
          }
          continue;
        }
        const auto first_nul = decoded->find('\0');
        const auto second_nul = first_nul == std::string::npos
                                    ? std::string::npos
                                    : decoded->find('\0', first_nul + 1);
        if (first_nul == std::string::npos || second_nul == std::string::npos ||
            !authenticator_fn(
                std::string_view(*decoded).substr(first_nul + 1,
                                                  second_nul - first_nul - 1),
                std::string_view(*decoded).substr(second_nul + 1))) {
          if (!(co_await write_reply(
                  ctx, client,
                  "535 5.7.8 Authentication credentials invalid\r\n"))) {
            break;
          }
          continue;
        }
        authenticated = true;
        if (!(co_await write_reply(
                ctx, client, "235 2.7.0 Authentication successful\r\n"))) {
          break;
        }
        continue;
      }
      if (command == "mail") {
        if (!greeted) {
          if (!(co_await write_reply(ctx, client,
                                     "503 5.5.1 Send HELO/EHLO first\r\n"))) {
            break;
          }
          continue;
        }
        if (!authenticated) {
          if (!(co_await write_reply(
                  ctx, client, "530 5.7.0 Authentication required\r\n"))) {
            break;
          }
          continue;
        }
        const auto path = parse_path(argument, "from:", true);
        if (!path) {
          if (!(co_await write_reply(ctx, client,
                                     "501 5.1.7 Invalid MAIL FROM\r\n"))) {
            break;
          }
          continue;
        }
        const auto declared_size = declared_message_size(argument);
        if (!declared_size || *declared_size > options.max_message_size) {
          if (!(co_await write_reply(
                  ctx, client,
                  "552 5.3.4 Message size exceeds fixed maximum\r\n"))) {
            break;
          }
          continue;
        }
        sender = *path;
        recipients.clear();
        if (!(co_await write_reply(ctx, client, "250 2.1.0 Sender OK\r\n"))) {
          break;
        }
        continue;
      }
      if (command == "rcpt") {
        if (sender.empty()) {
          if (!(co_await write_reply(ctx, client,
                                     "503 5.5.1 Need MAIL FROM first\r\n"))) {
            break;
          }
          continue;
        }
        const auto path = parse_path(argument, "to:");
        if (!path) {
          if (!(co_await write_reply(ctx, client,
                                     "501 5.1.3 Invalid RCPT TO\r\n"))) {
            break;
          }
          continue;
        }
        if (recipients.size() >= options.max_recipients) {
          if (!(co_await write_reply(ctx, client,
                                     "452 4.5.3 Too many recipients\r\n"))) {
            break;
          }
          continue;
        }
        recipients.push_back(*path);
        if (!(co_await write_reply(ctx, client,
                                   "250 2.1.5 Recipient OK\r\n"))) {
          break;
        }
        continue;
      }
      if (command == "data") {
        if (sender.empty() || recipients.empty()) {
          if (!(co_await write_reply(
                  ctx, client,
                  "503 5.5.1 Need MAIL FROM and RCPT TO first\r\n"))) {
            break;
          }
          continue;
        }
        if (!(co_await write_reply(
                ctx, client, "354 End data with <CR><LF>.<CR><LF>\r\n"))) {
          break;
        }

        std::string data;
        bool too_large = false;
        bool read_failed = false;
        for (;;) {
          auto data_line = co_await reader.read_line();
          if (!data_line) {
            read_failed = true;
            break;
          }
          if (*data_line == ".") {
            break;
          }
          if (data_line->starts_with("..")) {
            data_line->erase(0, 1);
          }
          if (!too_large &&
              data.size() + data_line->size() + 2 <= options.max_message_size) {
            data.append(*data_line);
            data.append("\r\n");
          } else {
            too_large = true;
          }
        }
        if (read_failed) {
          break;
        }
        if (too_large) {
          reset_transaction();
          if (!(co_await write_reply(
                  ctx, client,
                  "552 5.3.4 Message size exceeds fixed maximum\r\n"))) {
            break;
          }
          continue;
        }

        auto current_envelope = envelope{.sender = std::move(sender),
                                         .recipients = std::move(recipients)};
        auto current_message = parse_message(std::move(data));
        reset_transaction();
        const auto delivered =
            handler ? co_await handler(current_envelope, current_message)
                    : std::expected<void, std::error_code>{};
        if (delivered) {
          if (!(co_await write_reply(ctx, client,
                                     "250 2.0.0 Message accepted\r\n"))) {
            break;
          }
        } else if (!(co_await write_reply(
                       ctx, client,
                       "451 4.3.0 Message processing failed\r\n"))) {
          break;
        }
        continue;
      }
      if (!(co_await write_reply(ctx, client,
                                 "502 5.5.1 Command not implemented\r\n"))) {
        break;
      }
    }
    client.close();
  }

  io_context &ctx;
  server_options options;
  std::unique_ptr<tcp::acceptor> acceptor;
  recipient_handler handler;
  authenticator authenticator_fn;
  std::atomic<std::size_t> active{0};
  std::atomic<bool> running{false};
};

server::server(io_context &ctx, server_options options)
    : impl_(std::make_unique<impl>(ctx, std::move(options))) {}

server::~server() = default;

auto server::listen(std::string_view host, std::uint16_t port)
    -> std::expected<void, std::error_code> {
  const auto address = ip_address::from_string(host);
  if (!address) {
    return std::unexpected(address.error());
  }
  impl_->acceptor = std::make_unique<tcp::acceptor>(impl_->ctx);
  auto result =
      impl_->acceptor->open(endpoint{*address, port}, {.reuse_address = true});
  if (!result) {
    impl_->acceptor.reset();
    return std::unexpected(result.error());
  }
  return {};
}

void server::set_message_handler(recipient_handler handler) {
  impl_->handler = std::move(handler);
}

void server::set_authenticator(authenticator fn) {
  impl_->authenticator_fn = std::move(fn);
}

auto server::run() -> task<void> {
  if (!impl_->acceptor || !impl_->acceptor->is_open()) {
    co_return;
  }
  impl_->running.store(true, std::memory_order_relaxed);
  while (impl_->running.load(std::memory_order_relaxed)) {
    auto accepted =
        co_await async_accept(impl_->ctx, impl_->acceptor->native_socket());
    if (!accepted) {
      if (!impl_->running.load(std::memory_order_relaxed)) {
        break;
      }
      continue;
    }
    spawn(impl_->ctx, impl_->handle_connection(std::move(*accepted)));
  }
}

void server::stop() {
  impl_->running.store(false, std::memory_order_relaxed);
  if (impl_->acceptor) {
    impl_->acceptor->close();
  }
}

auto server::active_connections() const noexcept -> std::size_t {
  return impl_->active.load(std::memory_order_relaxed);
}

} // namespace cnetmod::mail
