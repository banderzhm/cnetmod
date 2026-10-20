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
        {
            request.text(cnetmod::http::status::ok, "sync");
        });
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
        std::vector<std::byte> padded_headers{
            static_cast<std::byte>(7)};
        padded_headers.insert(
            padded_headers.end(), encoded->begin(), encoded->end());
        padded_headers.insert(padded_headers.end(), 7, std::byte{});
        auto headers = framed({.type = frame_type::headers,
                                  .flags = 0xD,
                                  .stream_id = 1},
            padded_headers);
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
    std::uint32_t connection_credit{};
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
            {
                ++connection_updates;
                std::uint32_t increment{};
                for (std::size_t index = 0; index < 4; ++index)
                    increment = (increment << 8) |
                        std::to_integer<std::uint32_t>(output[offset + frame_header_size + index]);
                connection_credit += increment;
            }
            else
                ++stream_updates;
        }
        offset += frame_header_size + header->length;
    }
    ASSERT_TRUE(connection_updates >= std::size_t{1});
    ASSERT_EQ(connection_credit, std::uint32_t{9});
    // Stream 1 remains open after its first DATA frame, so at least that
    // consumed credit must be returned. Credit after END_STREAM is only
    // returned to the connection because the stream window no longer matters.
    ASSERT_TRUE(stream_updates >= std::size_t{1});
}

TEST(http2_borrowed_consumers_batch_credit_without_returning_unconsumed_bytes)
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
    std::vector<std::byte> input{
        reinterpret_cast<const std::byte*>(preface.data()),
        reinterpret_cast<const std::byte*>(preface.data()) + preface.size()};
    auto append = [&](frame_header header, std::span<const std::byte> payload)
    {
        auto frame = framed(header, payload);
        input.insert(input.end(), frame.begin(), frame.end());
    };
    append({.type = frame_type::settings}, {});
    append({.type = frame_type::headers, .flags = 0x4, .stream_id = 1}, *encoded);
    append({.type = frame_type::headers, .flags = 0x4, .stream_id = 3}, *encoded);
    const std::string payload(16 * 1024, 'x');
    append({.type = frame_type::data, .stream_id = 1}, bytes(payload));
    append({.type = frame_type::data, .flags = 0x1, .stream_id = 1}, bytes(payload));
    append({.type = frame_type::data, .flags = 0x1, .stream_id = 3}, bytes(payload));

    std::vector<std::byte> output;
    std::size_t consumed{};
    auto reader = [&](cnetmod::mutable_buffer)
        -> cnetmod::task<std::expected<std::size_t, std::error_code>>
    {
        // Give the two consumers and their shared credit task time to finish
        // before EOF cancels the connection's structured task group.
        for (unsigned turn = 0; turn < 8; ++turn)
            co_await cnetmod::post_awaitable{*io};
        co_return std::size_t{};
    };
    auto writer = [&](cnetmod::const_buffer source)
        -> cnetmod::task<std::expected<void, std::error_code>>
    {
        const auto* first = static_cast<const std::byte*>(source.data);
        output.insert(output.end(), first, first + source.size);
        co_return std::expected<void, std::error_code>{};
    };
    borrowed_streaming_server_handler handler =
        [&](server_request& request, cnetmod::cancel_token&)
        -> cnetmod::task<server_response>
    {
        while (auto chunk = co_await request.body_stream->receive())
            consumed += chunk->size();
        co_return server_response{.status = 204};
    };
    session connection{*io, unused, std::move(handler),
        {.max_bytes = 64 * 1024, .chunk_capacity = 4}, std::move(reader),
        std::move(writer)};
    ASSERT_TRUE(connection.receive(input).has_value());
    // Crossing half the connection window must not grant borrowed-stream
    // credit before the application has consumed any body bytes.
    const auto unconsumed = connection.take_outbound();
    for (std::size_t offset = 0; offset + frame_header_size <= unconsumed.size();)
    {
        const auto header = decode_frame_header(std::span{unconsumed}.subspan(offset, frame_header_size));
        ASSERT_TRUE(header.has_value());
        if (!header)
            break;
        ASSERT_TRUE(header->type != frame_type::window_update);
        offset += frame_header_size + header->length;
    }
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
    ASSERT_EQ(consumed, std::size_t{49152});
    std::size_t updates{};
    std::uint32_t credit{};
    for (std::size_t offset = 0; offset + frame_header_size <= output.size();)
    {
        const auto header = decode_frame_header(std::span{output}.subspan(offset, frame_header_size));
        ASSERT_TRUE(header.has_value());
        if (!header || offset + frame_header_size + header->length > output.size())
            break;
        if (header->type == frame_type::window_update && header->stream_id == 0)
        {
            ++updates;
            std::uint32_t increment{};
            for (std::size_t index = 0; index < 4; ++index)
                increment = (increment << 8) |
                    std::to_integer<std::uint32_t>(output[offset + frame_header_size + index]);
            credit += increment;
        }
        offset += frame_header_size + header->length;
    }
    ASSERT_EQ(updates, std::size_t{1});
    ASSERT_EQ(credit, std::uint32_t{49152});
}

TEST(http2_borrowed_handler_is_not_dispatched_twice_after_streamed_body)
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
    input.push_back(framed(
        {.type = frame_type::headers, .flags = 0x4, .stream_id = 1},
        *encoded));
    input.push_back(framed(
        {.type = frame_type::data, .flags = 0x1, .stream_id = 1},
        bytes("payload")));

    std::size_t next_input{};
    std::size_t calls{};
    std::string body;
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
    auto writer = [](cnetmod::const_buffer)
        -> cnetmod::task<std::expected<void, std::error_code>>
    {
        co_return std::expected<void, std::error_code>{};
    };
    borrowed_streaming_server_handler handler =
        [&](server_request& request, cnetmod::cancel_token&)
        -> cnetmod::task<server_response>
    {
        ++calls;
        while (auto chunk = co_await request.body_stream->receive())
            body.append(reinterpret_cast<const char*>(chunk->data()),
                chunk->size());
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
    ASSERT_EQ(calls, std::size_t{1});
    ASSERT_EQ(body, std::string{"payload"});
}

TEST(http2_complete_body_batch_and_split_body_return_exact_consumed_credit)
{
    for (const unsigned mode : {0U, 1U, 2U, 3U})
    {
        const bool split_reads = mode != 0;
        const bool filtered = mode >= 2;
        const std::string expected = mode == 3 ? std::string(24'000, 'x') : "abc";
        cnetmod::net_init network;
        auto io = cnetmod::make_io_context();
        cnetmod::socket unused;
        header_compression encoder;
        const std::array headers{header_field{":method", "POST"},
            header_field{":scheme", "http"}, header_field{":path", "/rpc"}};
        auto encoded = encoder.encode(headers);
        ASSERT_TRUE(encoded.has_value());
        std::vector<std::vector<std::byte>> reads;
        reads.emplace_back(reinterpret_cast<const std::byte*>(preface.data()),
            reinterpret_cast<const std::byte*>(preface.data()) + preface.size());
        auto settings = framed({.type = frame_type::settings}, {});
        reads.back().insert(reads.back().end(), settings.begin(), settings.end());
        for (const auto id : {1U, 3U})
        {
            auto head = framed({.type = frame_type::headers, .flags = 0x4,
                                   .stream_id = id}, *encoded);
            auto data = framed({.type = frame_type::data, .flags = 0x1,
                                   .stream_id = id}, bytes(expected));
            if (split_reads)
            {
                reads.push_back(std::move(head));
                if (mode == 3)
                {
                    reads.push_back(framed({.type = frame_type::data, .stream_id = id},
                        bytes(std::string_view{expected}.substr(0, 12'000))));
                    reads.push_back(framed({.type = frame_type::data, .flags = 0x1, .stream_id = id},
                        bytes(std::string_view{expected}.substr(12'000))));
                }
                else
                    reads.push_back(std::move(data));
            }
            else
            {
                reads.back().insert(reads.back().end(), head.begin(), head.end());
                reads.back().insert(reads.back().end(), data.begin(), data.end());
            }
        }
        std::size_t next{}, calls{}, buffered_calls{}, sync_calls{};
        bool second_started{};
        std::vector<std::byte> output;
        auto reader = [&](cnetmod::mutable_buffer destination)
            -> cnetmod::task<std::expected<std::size_t, std::error_code>>
        {
            co_await cnetmod::post_awaitable{*io};
            if (next == reads.size())
            {
                for (unsigned turn{}; turn < 8; ++turn)
                    co_await cnetmod::post_awaitable{*io};
                co_return std::size_t{};
            }
            const auto& input = reads[next++];
            std::memcpy(destination.data, input.data(), input.size());
            co_return input.size();
        };
        auto writer = [&](cnetmod::const_buffer source)
            -> cnetmod::task<std::expected<void, std::error_code>>
        {
            const auto* first = static_cast<const std::byte*>(source.data);
            output.insert(output.end(), first, first + source.size);
            co_return std::expected<void, std::error_code>{};
        };
        borrowed_streaming_server_handler handler =
            [&](server_request& request, cnetmod::cancel_token&)
            -> cnetmod::task<server_response>
        {
            ASSERT_TRUE(mode != 2);
            ++calls;
            std::string body;
            if (request.body_stream)
            {
                while (auto chunk = co_await request.body_stream->receive())
                    body.append(reinterpret_cast<const char*>(chunk->data()), chunk->size());
            }
            else
            {
                ++buffered_calls;
                body.assign(reinterpret_cast<const char*>(request.body.data()), request.body.size());
                request.body.clear(); // Credit must survive a handler taking ownership.
            }
            ASSERT_EQ(body, expected);
            if (request.stream_id == 1)
            {
                for (unsigned turn{}; turn < 64 && !second_started; ++turn)
                    co_await cnetmod::post_awaitable{*io};
                ASSERT_TRUE(second_started);
            }
            else
                second_started = true;
            co_return server_response{.status = 204};
        };
        buffered_request_filter filter;
        if (filtered)
            filter = [](std::span<const header_field>) { return true; };
        sync_server_handler synchronous;
        if (mode == 2)
            synchronous = [&](server_request& request, cnetmod::cancel_token&)
                -> std::optional<server_response>
            {
                ++calls;
                ++buffered_calls;
                ++sync_calls;
                ASSERT_EQ(request.body.size(), expected.size());
                ASSERT_TRUE(std::ranges::equal(request.body, bytes(expected)));
                return server_response{.status = 204};
            };
        session connection{*io, unused, std::move(handler),
            {.max_bytes = 64 * 1024, .chunk_capacity = 4}, std::move(reader),
            std::move(writer), std::move(synchronous), true, std::move(filter)};
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
        ASSERT_EQ(calls, std::size_t{2});
        ASSERT_EQ(sync_calls, mode == 2 ? std::size_t{2} : std::size_t{0});
        ASSERT_EQ(buffered_calls, mode == 0 || mode == 2 ? std::size_t{2} : std::size_t{0});
        std::uint32_t credit{};
        std::size_t updates{};
        for (std::size_t offset{}; offset + frame_header_size <= output.size();)
        {
            auto header = decode_frame_header(std::span{output}.subspan(offset, frame_header_size));
            ASSERT_TRUE(header.has_value());
            if (!header || offset + frame_header_size + header->length > output.size())
                break;
            if (header->type == frame_type::window_update && header->stream_id == 0)
            {
                ++updates;
                std::uint32_t increment{};
                for (std::size_t index{}; index < 4; ++index)
                    increment = (increment << 8) | std::to_integer<std::uint32_t>(
                        output[offset + frame_header_size + index]);
                credit += increment;
            }
            offset += frame_header_size + header->length;
        }
        ASSERT_EQ(credit, static_cast<std::uint32_t>(expected.size() * 2));
        if (!split_reads)
            ASSERT_EQ(updates, std::size_t{1});
    }
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
