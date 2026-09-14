#include "test_framework.hpp"

import std;
import cnetmod.protocol.redis;
import cnetmod.core.ssl;
import cnetmod.core.net_init;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;


TEST(redis_tls_probe_timeout_and_reconnection_preserve_session_isolation)
{
    for (unsigned first_session_mode = 0; first_session_mode < 3; ++first_session_mode)
    {
        const bool silent_first_session = first_session_mode != 0;
        cnetmod::net_init network;
        auto io = cnetmod::make_io_context();
        auto identity = cnetmod::ssl_context::server();
        ASSERT_TRUE(identity.has_value());
        ASSERT_TRUE(identity->load_cert_file(CNETMOD_REDIS_TEST_CERT).has_value());
        ASSERT_TRUE(identity->load_key_file(CNETMOD_REDIS_TEST_KEY).has_value());
        auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        ASSERT_TRUE(listener.has_value());
        ASSERT_TRUE(listener->bind(cnetmod::endpoint{
                                       cnetmod::ipv4_address::loopback(), 0})
                .has_value());
        ASSERT_TRUE(listener->listen().has_value());
        const auto endpoint = listener->local_endpoint();
        ASSERT_TRUE(endpoint.has_value());
        bool successful = true;
        bool peer_observed_close = !silent_first_session;
        unsigned finished = 0;
        auto finish = [&]
        {
            if (++finished == 2U)
                io->stop();
        };
        auto server = [&]() -> cnetmod::task<void>
        {
            for (int session = 0; session < 2; ++session)
            {
                auto peer = co_await cnetmod::async_accept(*io, *listener);
                if (!peer)
                {
                    successful = false;
                    io->stop();
                    co_return;
                }
                cnetmod::ssl_stream stream{*identity, *io, *peer};
                stream.set_accept_state();
                auto handshake = co_await stream.async_handshake();
                if (!handshake)
                {
                    successful = false;
                    io->stop();
                    co_return;
                }
                constexpr std::string_view command = "*1\r\n$4\r\nPING\r\n";
                std::array<char, command.size()> bytes{};
                std::size_t offset = 0;
                while (offset < bytes.size())
                {
                    auto received = co_await stream.async_read(
                        {bytes.data() + offset, bytes.size() - offset});
                    if (!received || *received == 0U)
                        break;
                    offset += *received;
                }
                successful = successful && std::string_view{bytes.data(), offset} == command;
                if (session == 0 && silent_first_session)
                {
                    if (first_session_mode == 2)
                    {
                        constexpr std::string_view prefix = "+PO";
                        auto sent = co_await stream.async_write_all({prefix.data(), prefix.size()});
                        successful = successful && sent.has_value();
                    }
                    char byte{};
                    auto received = co_await stream.async_read({&byte, 1});
                    peer_observed_close = !received || *received == 0U;
                }
                else
                {
                    constexpr std::string_view first = "+PO", last = "NG\r\n";
                    auto part = co_await stream.async_write_all({first.data(), first.size()});
                    auto remainder = co_await stream.async_write_all({last.data(), last.size()});
                    successful = successful && part.has_value() && remainder.has_value();
                }
            }
            finish();
        };
        auto exercise = [&]() -> cnetmod::task<void>
        {
            cnetmod::redis::client client{*io};
            for (int session = 0; session < 2; ++session)
            {
                cnetmod::cancel_token connection_token;
                cnetmod::redis::connect_options options{.host = "127.0.0.1", .port = endpoint->port(), .resp3 = false, .tls = true, .tls_verify = true, .tls_ca_file = CNETMOD_REDIS_TEST_CERT};
                bool connected = false;
                if (session == 0)
                    connected = (co_await client.connect(options)).has_value();
                else
                    connected = (co_await cnetmod::with_timeout(*io, std::chrono::seconds{2},
                                     client.connect(options, connection_token), connection_token))
                                    .has_value();
                if (!connected)
                {
                    successful = false;
                    io->stop();
                    co_return;
                }
                cnetmod::cancel_token token;
                if (session == 1)
                {
                    cnetmod::redis::request batch;
                    batch.push("PING");
                    auto reply = co_await cnetmod::with_timeout(*io,
                        std::chrono::milliseconds{150}, client.exchange(batch, token), token);
                    successful = successful && reply && reply->size() == 1U &&
                        reply->front().value == "PONG";
                    continue;
                }
                auto pong = co_await cnetmod::with_timeout(*io,
                    std::chrono::milliseconds{150}, client.ping(token), token);
                if (session == 0 && silent_first_session)
                    successful = successful && !pong && pong.error() == std::errc::timed_out && !client.is_open();
                else
                    successful = successful && pong.has_value() && client.is_open();
            }
            client.close();
            finish();
        };
        cnetmod::spawn(*io, server());
        cnetmod::spawn(*io, exercise());
        io->run();
        ASSERT_TRUE(successful);
        ASSERT_TRUE(peer_observed_close);
        ASSERT_EQ(finished, 2U);
    }
}

TEST(redis_tls_connection_deadline_interrupts_a_stalled_handshake)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(cnetmod::endpoint{cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    bool timed_out = false, peer_closed = false;
    unsigned finished = 0;
    auto finish = [&]
    {
        if (++finished == 2U)
            io->stop();
    };
    auto server = [&]() -> cnetmod::task<void>
    {
        auto peer = co_await cnetmod::async_accept(*io, *listener);
        if (!peer)
        {
            io->stop();
            co_return;
        }
        std::array<char, 4096> bytes{};
        std::size_t total = 0;
        while (true)
        {
            auto received = co_await cnetmod::async_read(*io, *peer,
                cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            if (!received || *received == 0U)
                break;
            total += *received;
        }
        peer_closed = total > 0;
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        cnetmod::redis::client client{*io};
        cnetmod::cancel_token token;
        auto result = co_await cnetmod::with_timeout(*io, std::chrono::milliseconds{150},
            client.connect({.host = "127.0.0.1", .port = endpoint->port(), .resp3 = false, .tls = true, .tls_verify = true, .tls_ca_file = CNETMOD_REDIS_TEST_CERT}, token), token);
        timed_out = !result && result.error() == std::errc::timed_out && !client.is_open();
        finish();
    };
    cnetmod::spawn(*io, server());
    cnetmod::spawn(*io, exercise());
    io->run();
    ASSERT_TRUE(timed_out);
    ASSERT_TRUE(peer_closed);
}

RUN_TESTS()
