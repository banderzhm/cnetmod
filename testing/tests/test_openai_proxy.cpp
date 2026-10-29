#include "test_framework.hpp"

import std;
import cnetmod.core.net_init;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;
import cnetmod.protocol.openai;

namespace {
namespace cn = cnetmod;

auto write_bytes(cn::io_context& io, cn::socket& peer, std::string_view data)
    -> cn::task<bool>
{
    auto result = co_await cn::async_write_all(io, peer,
        cn::const_buffer{data.data(), data.size()});
    co_return result.has_value();
}

auto read_bytes(cn::io_context& io, cn::socket& peer) -> cn::task<std::string>
{
    std::array<char, 2048> buffer{};
    auto result = co_await cn::async_read(io, peer,
        cn::mutable_buffer{buffer.data(), buffer.size()});
    if (!result) co_return std::string{};
    co_return std::string{buffer.data(), *result};
}

auto mock_proxy(cn::io_context& io, cn::socket& listener, bool allow, bool downgrade,
    bool& domain_received) -> cn::task<void>
{
    auto peer = co_await cn::async_accept(io, listener);
    if (!peer) co_return;
    auto greeting = co_await read_bytes(io, *peer);
    if (greeting.size() < 3) co_return;
    if (downgrade) {
        (void)co_await write_bytes(io, *peer, std::string_view{"\x05\x00", 2});
        co_return;
    }
    if (!(co_await write_bytes(io, *peer, std::string_view{"\x05\x02", 2}))) co_return;
    auto credentials = co_await read_bytes(io, *peer);
    const std::string expected{"\x01\x04user\x04pass", 11};
    const bool valid = allow && credentials == expected;
    const auto auth = valid ? std::string_view{"\x01\x00", 2}
                            : std::string_view{"\x01\x01", 2};
    if (!(co_await write_bytes(io, *peer, auth)) || !valid) co_return;
    auto target = co_await read_bytes(io, *peer);
    domain_received = target.find("example.invalid") != std::string::npos;
    constexpr char reply[] = "\x05\x00\x00\x01\x7f\x00\x00\x01\x00\x50";
    if (!(co_await write_bytes(io, *peer, std::string_view{reply, 10}))) co_return;
    auto request = co_await read_bytes(io, *peer);
    if (request.find("POST /v1/chat/completions") == std::string::npos) co_return;
    constexpr std::string_view body =
        R"({"model":"fixture","choices":[{"message":{"role":"assistant","content":"ok"},"finish_reason":"stop"}]})";
    auto response = std::format("HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", body.size(), body);
    (void)co_await write_bytes(io, *peer, response);
}

auto exercise_proxy(bool allow, bool downgrade = false) -> void
{
    cn::net_init network;
    auto io = cn::make_io_context();
    auto listener = cn::socket::create(cn::address_family::ipv4, cn::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    if (!listener) return;
    ASSERT_TRUE(listener->bind({cn::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!endpoint) return;
    bool done = false, authenticated = false, received = false, domain = false;
    auto client = [&]() -> cn::task<void> {
        cn::openai::client api{*io};
        auto result = co_await api.connect({.api_base = "http://example.invalid:8080/v1",
            .api_key = "fixture", .proxy_host = "127.0.0.1", .proxy_port = endpoint->port(),
            .proxy_username = "user", .proxy_password = "pass"});
        authenticated = result.has_value();
        if (result) {
            cn::openai::chat_request request{.model = "fixture",
                .messages = {cn::openai::message::user("hello")}};
            auto response = co_await api.chat(std::move(request));
            received = response && response->content() == "ok";
        }
        done = true;
        io->stop();
    };
    auto watchdog = [&]() -> cn::task<void> {
        co_await cn::async_sleep(*io, std::chrono::seconds{10});
        if (!done) io->stop();
    };
    cn::spawn(*io, mock_proxy(*io, *listener, allow, downgrade, domain));
    cn::spawn(*io, client());
    cn::spawn(*io, watchdog());
    io->run();
    ASSERT_TRUE(done);
    ASSERT_EQ(authenticated, allow);
    ASSERT_EQ(received, allow);
    ASSERT_EQ(domain, allow);
}
} // namespace

TEST(openai_proxy_uses_auth_and_remote_domain) { exercise_proxy(true); }
TEST(openai_proxy_rejects_invalid_credentials) { exercise_proxy(false); }
TEST(openai_proxy_rejects_auth_downgrade) { exercise_proxy(false, true); }
RUN_TESTS()
