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

auto accept_message(const cnetmod::mail::envelope &,
                    const cnetmod::mail::message &)
    -> cnetmod::task<std::expected<void, std::error_code>> {
  co_return std::expected<void, std::error_code>{};
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fputs("usage: smtp_bench_server <port>\n", stderr);
    return 2;
  }

  const auto port = parse_port(argv[1]);
  if (!port) {
    std::fputs("invalid port\n", stderr);
    return 2;
  }

  cnetmod::net_init network;
  auto context = cnetmod::make_io_context();
  cnetmod::mail::server server(*context,
                               {.hostname = "bench.cnetmod.test",
                                .max_message_size = 16U * 1024U * 1024U});
  server.set_message_handler(accept_message);
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
