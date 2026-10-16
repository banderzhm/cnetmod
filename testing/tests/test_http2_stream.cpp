#include "test_framework.hpp"

import std;
import cnetmod.core;
import cnetmod.coro.cancel;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.io;
import cnetmod.protocol.http.semantics;
import cnetmod.protocol.http;
import cnetmod.protocol.http.v2.frame;
import cnetmod.protocol.http.v2.header_compression;
import cnetmod.protocol.http.v2.session;

using namespace cnetmod::http::v2;

namespace {
constexpr std::string_view preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

auto framed(frame_header header, std::span<const std::byte> payload)
    -> std::vector<std::byte>
{
    header.length = static_cast<std::uint32_t>(payload.size());
    auto wire = encode_frame_header(header);
    std::vector<std::byte> result{wire.begin(), wire.end()};
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

auto bytes(std::string_view value) -> std::span<const std::byte>
{
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}
} // namespace

TEST(http2_server_dispatches_sync_router_handlers)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::http::server http_server{*io};
    cnetmod::http::router routes;
    routes.get_sync("/sync", [](cnetmod::http::request_context& request)
        { request.text(cnetmod::http::status::ok, "sync"); });
    http_server.set_router(std::move(routes));
    ASSERT_TRUE(http_server.listen("127.0.0.1", 0).has_value());
    const auto endpoint = http_server.local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    auto listener = http_server.run();
    listener.handle().resume();

    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto peer = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        ASSERT_TRUE(peer.has_value());
        ASSERT_TRUE((co_await cnetmod::async_connect(*io, *peer, *endpoint))
                .has_value());

        header_compression encoder;
        const std::array request_headers{
            header_field{":method", "GET"},
            header_field{":scheme", "http"},
            header_field{":authority", "localhost"},
            header_field{":path", "/sync"}};
        auto encoded = encoder.encode(request_headers);
        ASSERT_TRUE(encoded.has_value());

        std::vector<std::byte> request{
            reinterpret_cast<const std::byte*>(preface.data()),
            reinterpret_cast<const std::byte*>(preface.data()) + preface.size()};
        auto settings = framed({.type = frame_type::settings}, {});
        request.insert(request.end(), settings.begin(), settings.end());
        auto headers = framed({.type = frame_type::headers,
                                  .flags = 0x5,
                                  .stream_id = 1},
            *encoded);
        request.insert(request.end(), headers.begin(), headers.end());
        ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *peer,
                         cnetmod::const_buffer{request.data(), request.size()}))
                .has_value());

        std::vector<std::byte> wire;
        std::string body;
        bool ended{};
        std::array<std::byte, 4096> buffer{};
        while (!ended)
        {
            auto read = co_await cnetmod::async_read(*io, *peer,
                cnetmod::mutable_buffer{buffer.data(), buffer.size()});
            ASSERT_TRUE(read.has_value() && *read > 0);
            if (!read || *read == 0)
                break;
            wire.insert(wire.end(), buffer.begin(), buffer.begin() + *read);

            std::size_t offset{};
            while (offset + frame_header_size <= wire.size())
            {
                auto header = decode_frame_header(
                    std::span{wire.data() + offset, frame_header_size});
                ASSERT_TRUE(header.has_value());
                if (!header ||
                    offset + frame_header_size + header->length > wire.size())
                    break;
                if (header->type == frame_type::data &&
                    header->stream_id == 1)
                {
                    body.append(reinterpret_cast<const char*>(wire.data() +
                                    offset + frame_header_size),
                        header->length);
                    ended = (header->flags & 0x1U) != 0;
                }
                offset += frame_header_size + header->length;
            }
            if (offset != 0)
                wire.erase(wire.begin(), wire.begin() + offset);
        }

        ASSERT_EQ(body, std::string{"sync"});
        peer->close();
        http_server.stop();
        while (!listener.handle().done())
            co_await cnetmod::post_awaitable{*io};
        listener.handle().promise().result();
        while (http_server.active_connections() != 0)
            co_await cnetmod::post_awaitable{*io};
        io->stop();
    };
    auto operation = exercise();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(operation.handle().done());
    operation.handle().promise().result();
}

TEST(http2_streams_deliver_interleaved_bodies_and_return_consumed_credit)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::socket unused;
    header_compression encoder;
    const std::array request_headers{
        header_field{":method", "POST"}, header_field{":scheme", "http"},
        header_field{":path", "/upload"}};
    auto encoded = encoder.encode(request_headers);
    ASSERT_TRUE(encoded.has_value());

    std::vector<std::vector<std::byte>> input;
    std::vector<std::byte> opening{
        reinterpret_cast<const std::byte*>(preface.data()),
        reinterpret_cast<const std::byte*>(preface.data()) + preface.size()};
    auto settings = framed({.type = frame_type::settings}, {});
    opening.insert(opening.end(), settings.begin(), settings.end());
    input.push_back(std::move(opening));
    input.push_back(framed({.type = frame_type::headers, .flags = 0x4, .stream_id = 1},
        *encoded));
    input.push_back(framed({.type = frame_type::headers, .flags = 0x4, .stream_id = 3},
        *encoded));
    input.push_back(framed(
        {.type = frame_type::data, .stream_id = 1}, bytes("abc")));
    input.push_back(framed({.type = frame_type::data, .flags = 0x1, .stream_id = 3},
        bytes("xyz")));
    input.push_back(framed({.type = frame_type::data, .flags = 0x1, .stream_id = 1},
        bytes("def")));

    std::size_t next_input{};
    std::vector<std::byte> output;
    std::unordered_map<std::uint32_t, std::string> bodies;
    auto reader = [&](cnetmod::mutable_buffer destination)
        -> cnetmod::task<std::expected<std::size_t, std::error_code>>
    {
        co_await cnetmod::post_awaitable{*io};
        if (next_input == input.size())
            co_return std::size_t{};
        const auto& source = input[next_input++];
        std::memcpy(destination.data, source.data(), source.size());
        co_return source.size();
    };
    auto writer = [&](cnetmod::const_buffer source)
        -> cnetmod::task<std::expected<void, std::error_code>>
    {
        const auto* first = static_cast<const std::byte*>(source.data);
        output.insert(output.end(), first, first + source.size);
        co_return std::expected<void, std::error_code>{};
    };
    streaming_server_handler handler =
        [&](server_request request, cnetmod::cancel_token&)
        -> cnetmod::task<server_response>
    {
        std::string body;
        while (auto chunk = co_await request.body_stream->receive())
            body.append(reinterpret_cast<const char*>(chunk->data()),
                chunk->size());
        bodies.emplace(request.stream_id, std::move(body));
        co_return server_response{.status = 204};
    };
    session connection{*io, unused, std::move(handler),
        {.max_bytes = 64, .chunk_capacity = 4}, std::move(reader),
        std::move(writer)};
    auto execute = [&]() -> cnetmod::task<void>
    {
        co_await connection.run();
        io->stop();
    };
    auto operation = execute();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(operation.handle().done());
    operation.handle().promise().result();
    ASSERT_EQ(bodies[1], std::string("abcdef"));
    ASSERT_EQ(bodies[3], std::string("xyz"));

    std::size_t offset{};
    std::size_t connection_updates{};
    std::size_t stream_updates{};
    while (offset + frame_header_size <= output.size())
    {
        auto header = decode_frame_header(
            std::span{output.data() + offset, frame_header_size});
        ASSERT_TRUE(header.has_value());
        if (!header || offset + frame_header_size + header->length > output.size())
            break;
        if (header->type == frame_type::window_update)
        {
            if (header->stream_id == 0)
                ++connection_updates;
            else
                ++stream_updates;
        }
        offset += frame_header_size + header->length;
    }
    ASSERT_EQ(connection_updates, std::size_t{3});
    // Stream 1 remains open after its first DATA frame, so at least that
    // consumed credit must be returned. Credit after END_STREAM is only
    // returned to the connection because the stream window no longer matters.
    ASSERT_TRUE(stream_updates >= std::size_t{1});
}

TEST(http2_streaming_response_source_is_framed_without_buffering_the_body)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::socket unused;
    header_compression encoder;
    const std::array request_headers{
        header_field{":method", "GET"}, header_field{":scheme", "http"},
        header_field{":path", "/file"}};
    auto encoded = encoder.encode(request_headers);
    ASSERT_TRUE(encoded.has_value());

    std::vector<std::vector<std::byte>> input;
    std::vector<std::byte> opening{
        reinterpret_cast<const std::byte*>(preface.data()),
        reinterpret_cast<const std::byte*>(preface.data()) + preface.size()};
    auto settings = framed({.type = frame_type::settings}, {});
    opening.insert(opening.end(), settings.begin(), settings.end());
    input.push_back(std::move(opening));
    input.push_back(framed({.type = frame_type::headers,
                               .flags = 0x5,
                               .stream_id = 1},
        *encoded));

    std::size_t next_input{};
    bool response_done{};
    std::vector<std::byte> output;
    auto reader = [&](cnetmod::mutable_buffer destination)
        -> cnetmod::task<std::expected<std::size_t, std::error_code>>
    {
        co_await cnetmod::post_awaitable{*io};
        while (next_input == input.size() && !response_done)
            co_await cnetmod::post_awaitable{*io};
        if (next_input == input.size())
            co_return std::size_t{};
        const auto& source = input[next_input++];
        std::memcpy(destination.data, source.data(), source.size());
        co_return source.size();
    };
    auto writer = [&](cnetmod::const_buffer source)
        -> cnetmod::task<std::expected<void, std::error_code>>
    {
        const auto* first = static_cast<const std::byte*>(source.data);
        output.insert(output.end(), first, first + source.size);
        co_return std::expected<void, std::error_code>{};
    };
    streaming_server_handler handler =
        [&response_done](server_request, cnetmod::cancel_token&)
        -> cnetmod::task<server_response>
    {
        auto index = std::make_shared<std::size_t>();
        auto source = std::make_shared<cnetmod::http::response_body_source>(
            [index, &response_done](cnetmod::cancel_token&)
                -> cnetmod::task<std::optional<cnetmod::byte_buffer>>
            {
                constexpr std::array chunks{
                    std::string_view{"hello "}, std::string_view{"world"}};
                if (*index == chunks.size())
                {
                    response_done = true;
                    co_return std::nullopt;
                }
                const auto value = chunks[(*index)++];
                co_return cnetmod::byte_buffer{
                    reinterpret_cast<const std::byte*>(value.data()),
                    reinterpret_cast<const std::byte*>(value.data()) +
                        value.size()};
            },
            11U);
        co_return server_response{
            .status = 200,
            .body_source = std::move(source)};
    };
    session connection{*io, unused, std::move(handler),
        {.max_bytes = 64, .chunk_capacity = 4}, std::move(reader),
        std::move(writer)};
    auto execute = [&]() -> cnetmod::task<void>
    {
        co_await connection.run();
        io->stop();
    };
    auto operation = execute();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(operation.handle().done());
    operation.handle().promise().result();

    std::string body;
    bool ended{};
    std::size_t offset{};
    while (offset + frame_header_size <= output.size())
    {
        auto header = decode_frame_header(
            std::span{output.data() + offset, frame_header_size});
        ASSERT_TRUE(header.has_value());
        if (!header || offset + frame_header_size + header->length > output.size())
            break;
        if (header->type == frame_type::data && header->stream_id == 1)
        {
            body.append(reinterpret_cast<const char*>(
                            output.data() + offset + frame_header_size),
                header->length);
            ended |= (header->flags & 0x1U) != 0;
        }
        offset += frame_header_size + header->length;
    }
    ASSERT_EQ(body, std::string{"hello world"});
    ASSERT_TRUE(ended);
}

TEST(http2_stream_window_updates_do_not_inflate_connection_window)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::socket unused;
    session connection{*io, unused};
    header_compression encoder;
    const std::array request_headers{
        header_field{":method", "POST"}, header_field{":scheme", "http"},
        header_field{":path", "/upload"}};
    auto encoded = encoder.encode(request_headers);
    ASSERT_TRUE(encoded.has_value());

    std::vector<std::byte> opening{
        reinterpret_cast<const std::byte*>(preface.data()),
        reinterpret_cast<const std::byte*>(preface.data()) + preface.size()};
    auto settings = framed({.type = frame_type::settings}, {});
    opening.insert(opening.end(), settings.begin(), settings.end());
    ASSERT_TRUE(connection.receive(opening).has_value());
    ASSERT_TRUE(connection.receive(framed(
                                       {.type = frame_type::headers, .flags = 0x4, .stream_id = 1},
                                       *encoded))
            .has_value());
    ASSERT_TRUE(connection.receive(framed(
                                       {.type = frame_type::headers, .flags = 0x4, .stream_id = 3},
                                       *encoded))
            .has_value());

    constexpr std::uint32_t increment = 1'500'000'000U;
    const std::array<std::byte, 4> update{
        static_cast<std::byte>(increment >> 24),
        static_cast<std::byte>(increment >> 16),
        static_cast<std::byte>(increment >> 8),
        static_cast<std::byte>(increment)};
    ASSERT_TRUE(connection.receive(framed(
                                       {.type = frame_type::window_update, .stream_id = 1}, update))
            .has_value());
    ASSERT_TRUE(connection.receive(framed(
                                       {.type = frame_type::window_update, .stream_id = 3}, update))
            .has_value());
}

RUN_TESTS()
