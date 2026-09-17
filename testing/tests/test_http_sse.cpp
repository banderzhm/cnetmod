#include "test_framework.hpp"
#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.io;
import cnetmod.protocol.http;

using namespace cnetmod::http;

TEST(sse_encodes_named_multiline_event)
{
    auto frame = sse::encode(sse::event{
        .event = "update",
        .data = "line1\nline2",
        .id = "42",
        .retry = std::chrono::milliseconds{1500},
    });

    ASSERT_EQ(frame, std::string("id: 42\n" "event: update\n" "retry: 1500\n" "data: line1\n" "data: line2\n" "\n"));
}

TEST(sse_heartbeat_is_comment_frame)
{
    ASSERT_EQ(sse::heartbeat(), std::string(": keepalive\n\n"));
}

TEST(sse_prepare_sets_streaming_headers)
{
    response resp;
    resp.set_body(std::string_view("old"));
    sse::prepare(resp);

    ASSERT_EQ(resp.get_header("Content-Type"), std::string_view("text/event-stream; charset=utf-8"));
    ASSERT_EQ(resp.get_header("Cache-Control"), std::string_view("no-cache, no-transform"));
    ASSERT_EQ(resp.get_header("Connection"), std::string_view("keep-alive"));
    ASSERT_EQ(resp.get_header("X-Accel-Buffering"), std::string_view("no"));
    ASSERT_EQ(resp.get_header("X-Streamed"), std::string_view("1"));
    ASSERT_TRUE(resp.get_header("Content-Length").empty());
}

TEST(sse_make_response_omits_content_length)
{
    std::array events{
        sse::event{.event = "message", .data = "hello", .id = "1"},
        sse::event{.data = "world"},
    };

    auto resp = sse::make_response(std::span<const sse::event>{events.data(), events.size()});
    ASSERT_EQ(resp.status_code(), 200);
    ASSERT_TRUE(resp.get_header("Content-Length").empty());
    ASSERT_TRUE(resp.body().find("event: message\n") != std::string_view::npos);
    ASSERT_TRUE(resp.body().find("data: world\n\n") != std::string_view::npos);
}

TEST(sse_context_exposes_conservative_commit_state)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    auto client = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value() && client.has_value());
    ASSERT_TRUE(listener->bind(
                            {cnetmod::ipv4_address::loopback(), 0})
            .has_value());
    ASSERT_TRUE(listener->listen().has_value());
    const auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());

    bool verified = false;
    auto run = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE((co_await cnetmod::async_connect(
                         *io, *client, *endpoint))
                .has_value());
        auto accepted = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(accepted.has_value());
        header_map headers;
        response outgoing;
        request_context request{*io, *accepted, "GET", "/events", headers,
            {}, outgoing, {}};

        ASSERT_FALSE(request.sse_started());
        ASSERT_TRUE(request.sse_state() == sse_stream_state::not_started);
        ASSERT_TRUE(co_await request.sse_heartbeat());
        ASSERT_TRUE(request.sse_started());
        ASSERT_TRUE(request.sse_state() == sse_stream_state::open);
        ASSERT_TRUE(co_await request.sse_done());
        ASSERT_TRUE(request.sse_state() == sse_stream_state::closed);

        std::string received;
        std::array<char, 4096> buffer{};
        while (!received.contains(": keepalive\n\n") ||
            !received.contains("data: {\"done\":true}\n\n"))
        {
            auto read = co_await cnetmod::async_read(*io, *client,
                cnetmod::mutable_buffer{buffer.data(), buffer.size()});
            ASSERT_TRUE(read.has_value() && *read > 0);
            if (!read || *read == 0)
                break;
            received.append(buffer.data(), *read);
        }
        verified = received.contains(": keepalive\n\n") &&
            received.contains("data: {\"done\":true}\n\n");
        io->stop();
    };
    cnetmod::spawn(*io, run());
    io->run();
    ASSERT_TRUE(verified);
}

RUN_TESTS()
