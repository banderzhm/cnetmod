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

auto run_client(cnetmod::io_context &context, std::uint16_t port,
                std::string &failure) -> cnetmod::task<void> {
  cnetmod::mail::client client(context, {.hostname = "client.cnetmod.test"});
  if (const auto connected = co_await client.connect("127.0.0.1", port);
      !connected) {
    failure = connected.error();
    co_return;
  }
  cnetmod::mail::envelope envelope{
      .sender = "sender@example.test",
      .recipients = {"primary@example.test", "copy@example.test"}};
  cnetmod::mail::message message{
      .headers = {{"From", "sender@example.test"},
                  {"To", "primary@example.test"},
                  {"Subject", "cnetmod SMTP semantic interop"}},
      .body = "first line\r\n.leading dot\r\n..double dot\r\n"};
  if (const auto sent = co_await client.send(envelope, message); !sent) {
    failure = sent.error();
    co_return;
  }
  if (const auto quit = co_await client.quit(); !quit) {
    failure = quit.error();
  }
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fputs("usage: smtp_interop_client <port>\n", stderr);
    return 2;
  }
  const auto port = parse_port(argv[1]);
  if (!port) {
    std::fputs("invalid port\n", stderr);
    return 2;
  }

  cnetmod::net_init network;
  auto context = cnetmod::make_io_context();
  std::string failure;
  cnetmod::spawn(*context, [&] -> cnetmod::task<void> {
    co_await run_client(*context, *port, failure);
    context->stop();
  }());
  context->run();
  if (!failure.empty()) {
    std::println(stderr, "FAIL {}", failure);
    return 1;
  }
  std::puts("PASS cnetmod SMTP client semantic interop");
  return 0;
}
