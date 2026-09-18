#include "test_framework.hpp"

import std;
import cnetmod.core;
import cnetmod.coro.cancel;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.io;
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

RUN_TESTS()
