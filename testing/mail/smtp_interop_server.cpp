#include <cnetmod/config.hpp>

#include <cstdio>

import std;
import cnetmod.core.net_init;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.mail;

namespace {

auto parse_port(std::string_view text) -> std::optional<std::uint16_t> {
  unsigned value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || value == 0 ||
      value > std::numeric_limits<std::uint16_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>(value);
}

auto save_message(const std::filesystem::path &result_path,
                  const cnetmod::mail::envelope &envelope,
                  const cnetmod::mail::message &message)
    -> cnetmod::task<std::expected<void, std::error_code>> {
  const auto temporary_path = result_path.string() + ".tmp";
  std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    co_return std::unexpected(std::make_error_code(std::errc::io_error));
  }
  output << "FROM=" << envelope.sender << "\n";
  for (const auto &recipient : envelope.recipients) {
    output << "TO=" << recipient << "\n";
  }
  for (const auto &[name, value] : message.headers) {
    output << "HEADER=" << name << ":" << value << "\n";
  }
  output << "BODY\n" << message.body;
  output.close();
  std::error_code error;
  std::filesystem::rename(temporary_path, result_path, error);
  if (error) {
    co_return std::unexpected(error);
  }
  co_return std::expected<void, std::error_code>{};
}

auto authenticate(std::string_view username, std::string_view password)
    -> bool {
  return username == "semantic-user" && password == "semantic-password";
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::fputs("usage: smtp_interop_server <port> <result-file>\n", stderr);
    return 2;
  }
  const auto port = parse_port(argv[1]);
  if (!port) {
    std::fputs("invalid port\n", stderr);
    return 2;
  }

  cnetmod::net_init network;
  auto context = cnetmod::make_io_context();
  cnetmod::mail::server server(
      *context, {.hostname = "interop.cnetmod.test", .require_auth = true});
  const std::filesystem::path result_path = argv[2];
  server.set_authenticator(authenticate);
  server.set_message_handler(
      [&result_path](const cnetmod::mail::envelope &envelope,
                     const cnetmod::mail::message &message) {
        return save_message(result_path, envelope, message);
      });
  if (const auto result = server.listen("127.0.0.1", *port); !result) {
    std::println(stderr, "listen failed: {}", result.error().message());
    return 1;
  }

  std::println("READY {}", *port);
  std::fflush(stdout);
  cnetmod::spawn(*context, server.run());
  context->run();
  return 0;
}
