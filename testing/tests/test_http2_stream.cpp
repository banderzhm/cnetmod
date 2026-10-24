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
import cnetmod.protocol.http.v2.flow_control;
import cnetmod.protocol.http.v2.settings;

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
    for (int mode = 0; mode != 6; ++mode)
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
        bool adapted{}, middleware_called{};
        if (mode != 0)
            http_server.set_h2_sync_adapter(
                [&](server_request&, cnetmod::cancel_token&) -> std::optional<server_response>
                {
                    adapted = true;
                    if (mode == 3)
                        return std::nullopt;
                    server_response response{.body_text = "adapter"};
                    response.automatic_content_length = mode == 5;
                    if (mode == 4)
                        response.headers.push_back({"content-length", "7"});
                    return response;
                },
                [](std::span<const header_field>)
                {
                    return true;
                });
        if (mode == 2)
            http_server.use([&](cnetmod::http::request_context&, cnetmod::http::next_fn next)
                                -> cnetmod::task<void>
                {
                    middleware_called = true;
                    co_await next();
                });
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
            header_compression decoder;
            std::optional<std::string> content_length;
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
                    if (header->type == frame_type::headers && header->stream_id == 1)
                    {
                        auto fields = decoder.decode(std::span{wire}.subspan(
                            offset + frame_header_size, header->length));
                        ASSERT_TRUE(fields.has_value());
                        for (const auto& field : *fields)
                            if (field.name == "content-length")
                                content_length = field.value;
                    }
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

            const bool adapter_body = mode == 1 || mode == 4 || mode == 5;
            ASSERT_EQ(body, std::string{adapter_body ? "adapter" : "sync"});
            ASSERT_EQ(adapted, mode == 1 || mode == 3 || mode == 4 || mode == 5);
            if (mode == 1)
                ASSERT_FALSE(content_length.has_value());
            else
            {
                ASSERT_TRUE(content_length.has_value());
                ASSERT_EQ(*content_length, std::to_string(body.size()));
            }
            ASSERT_EQ(middleware_called, mode == 2);
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
}

TEST(http2_abort_retires_parallel_request_read_and_backpressured_response_write)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::http::server server{*io};
    bool responding{};
    cnetmod::http::router routes;
    routes.post("/duplex", [&](cnetmod::http::request_context& context)
        -> cnetmod::task<void>
        {
            (void)co_await context.read_full_body();
            context.resp().set_body(std::string(8U * 1024U * 1024U, 'x'));
            responding = true;
        });
    server.set_router(std::move(routes));
    ASSERT_TRUE(server.listen("127.0.0.1", 0).has_value());
    const auto address = server.local_endpoint();
    ASSERT_TRUE(address.has_value());
    auto accept_loop = server.run();
    accept_loop.handle().resume();
    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto peer = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        ASSERT_TRUE(peer.has_value());
        ASSERT_TRUE(peer->apply_options({.recv_buffer_size = 1024}).has_value());
        ASSERT_TRUE((co_await cnetmod::async_connect(*io, *peer, *address)).has_value());
        std::vector<std::byte> input(bytes(preface).begin(), bytes(preface).end());
        const auto append = [&](frame_header header, std::span<const std::byte> payload)
        {
            auto frame = framed(header, payload);
            input.insert(input.end(), frame.begin(), frame.end());
        };
        const auto large_window = encode_settings(settings{.initial_window_size = 16U * 1024U * 1024U});
        append({.type = frame_type::settings}, large_window);
        const std::array connection_credit{std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}};
        append({.type = frame_type::window_update}, connection_credit);
        header_compression encoder;
        const std::array headers{header_field{":method", "POST"},
            header_field{":scheme", "http"}, header_field{":path", "/duplex"}};
        const auto encoded = encoder.encode(headers);
        ASSERT_TRUE(encoded.has_value());
        append({.type = frame_type::headers, .flags = 0x4, .stream_id = 1}, *encoded);
        append({.type = frame_type::data, .flags = 0x1, .stream_id = 1}, bytes("x"));
        ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *peer,
            cnetmod::const_buffer{input.data(), input.size()})).has_value());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (!responding && std::chrono::steady_clock::now() < deadline)
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
        ASSERT_TRUE(responding);
        // Do not read or close the peer: keep the response write backpressured
        // while the connection also waits for its next request.
        (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{10});
        server.stop();
        server.abort_connections();
        server.abort_connections();
        while ((!accept_loop.handle().done() || server.active_connections() != 0) &&
            std::chrono::steady_clock::now() < deadline)
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
        ASSERT_TRUE(accept_loop.handle().done());
        ASSERT_EQ(server.active_connections(), 0U);
        io->stop();
    };
    auto running = exercise();
    running.handle().resume();
    io->run();
    ASSERT_TRUE(running.handle().done());
    running.handle().promise().result();
}

TEST(http_servers_on_one_loop_share_receive_buffers_and_survive_each_other)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    const auto make_server = [&](std::string_view body)
    {
        auto created = std::make_unique<cnetmod::http::server>(*io);
        cnetmod::http::router routes;
        routes.get_sync("/", [body](cnetmod::http::request_context& context)
            { context.text(200, body); });
        created->set_router(std::move(routes));
        return created;
    };
    auto first = make_server("first");
    auto second = make_server("second");
    ASSERT_TRUE(first->listen("127.0.0.1", 0).has_value());
    ASSERT_TRUE(second->listen("127.0.0.1", 0).has_value());
    const auto first_address = first->local_endpoint();
    const auto second_address = second->local_endpoint();
    ASSERT_TRUE(first_address.has_value());
    ASSERT_TRUE(second_address.has_value());
    auto first_loop = first->run();
    auto second_loop = second->run();
    first_loop.handle().resume();
    second_loop.handle().resume();

    constexpr std::string_view request = "GET / HTTP/1.1\r\nHost: test\r\n\r\n";
    const auto exchange = [&](cnetmod::socket& peer, std::string_view expected)
        -> cnetmod::task<bool>
    {
        if (!co_await cnetmod::async_write_all(*io, peer,
                cnetmod::const_buffer{request.data(), request.size()}))
            co_return false;
        std::string received;
        std::array<char, 512> buffer{};
        while (!received.ends_with(expected))
        {
            auto read = co_await cnetmod::async_read(*io, peer,
                cnetmod::mutable_buffer{buffer.data(), buffer.size()});
            if (!read || *read == 0U)
                co_return false;
            received.append(buffer.data(), *read);
        }
        co_return received.starts_with("HTTP/1.1 200");
    };
    const auto connect = [&](const cnetmod::endpoint& address)
        -> cnetmod::task<std::optional<cnetmod::socket>>
    {
        auto peer = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        if (!peer || !co_await cnetmod::async_connect(*io, *peer, address))
            co_return std::nullopt;
        co_return std::move(*peer);
    };
    const auto settle = [&](cnetmod::http::server& server, auto& loop) -> cnetmod::task<void>
    {
        server.stop();
        server.abort_connections();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while ((!loop.handle().done() || server.active_connections() != 0) &&
            std::chrono::steady_clock::now() < deadline)
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
    };

    bool interleaved = false;
    bool survived = false;
    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto a = co_await connect(*first_address);
        auto b = co_await connect(*second_address);
        if (a && b)
            interleaved = co_await exchange(*a, "first") &&
                co_await exchange(*b, "second") &&
                co_await exchange(*a, "first") &&
                co_await exchange(*b, "second");
        b.reset();
        co_await settle(*second, second_loop);
        second.reset();
        // The first server's receive path must not depend on the destroyed
        // server: both a live connection and a new one keep working.
        auto c = co_await connect(*first_address);
        survived = a && c && co_await exchange(*a, "first") &&
            co_await exchange(*c, "first");
        a.reset();
        c.reset();
        co_await settle(*first, first_loop);
        io->stop();
    };
    auto running = exercise();
    running.handle().resume();
    io->run();
    ASSERT_TRUE(running.handle().done());
    running.handle().promise().result();
    ASSERT_TRUE(interleaved);
    ASSERT_TRUE(survived);
    ASSERT_TRUE(first_loop.handle().done());
}

TEST(http2_send_window_adjustments_allow_negative_credit_without_overflow)
{
    flow_window window{0};
    ASSERT_TRUE(window.adjust(-10));
    ASSERT_FALSE(window.consume(1));
    ASSERT_TRUE(window.increase(5));
    ASSERT_EQ(window.available(), -5);
    ASSERT_FALSE(window.increase(std::numeric_limits<std::uint32_t>::max()));
    ASSERT_FALSE(window.adjust(std::numeric_limits<std::int64_t>::max()));
    ASSERT_FALSE(window.adjust(std::numeric_limits<std::int64_t>::min()));
    ASSERT_TRUE(window.increase(5));
    ASSERT_EQ(window.available(), 0);
}

TEST(http2_pending_responses_obey_stream_and_connection_send_windows)
{
    auto io = cnetmod::make_io_context();
    cnetmod::socket unused;
    header_compression encoder;
    std::vector<std::byte> opening(bytes(preface).begin(), bytes(preface).end());
    const auto append = [](std::vector<std::byte>& target, frame_header header,
                            std::span<const std::byte> payload)
    {
        auto frame = framed(header, payload);
        target.insert(target.end(), frame.begin(), frame.end());
    };
    const auto config = encode_settings(settings{.initial_window_size = 0});
    append(opening, {.type = frame_type::settings}, config);
    for (const auto id : {1U, 3U, 5U})
    {
        const std::array fields{header_field{":method", "GET"},
            header_field{":scheme", "http"}, header_field{":path", "/flow"}};
        const auto encoded = encoder.encode(fields);
        ASSERT_TRUE(encoded.has_value());
        append(opening, {.type = frame_type::headers, .flags = 5, .stream_id = id}, *encoded);
    }
    const std::array reset{std::byte{}, std::byte{}, std::byte{}, std::byte{8}};
    append(opening, {.type = frame_type::rst_stream, .stream_id = 5}, reset);
    std::size_t data_bytes{}, ended{}, stage{}, handled{};
    std::string reusable_body(40000, 'f');
    const auto update = [&](std::uint32_t id, std::uint32_t credit)
    {
        const std::array payload{static_cast<std::byte>(credit >> 24),
            static_cast<std::byte>(credit >> 16), static_cast<std::byte>(credit >> 8),
            static_cast<std::byte>(credit)};
        return framed({.type = frame_type::window_update, .stream_id = id}, payload);
    };
    transport_reader reader = [&](cnetmod::mutable_buffer buffer)
        -> cnetmod::task<std::expected<std::size_t, std::error_code>>
    {
        // Let the structured writer complete before inspecting each boundary.
        co_await cnetmod::post_awaitable{*io};
        std::vector<std::byte> input;
        if (stage == 0)
            input = opening;
        else if (stage == 1)
        {
            ASSERT_EQ(data_bytes, std::size_t{0});
            input = update(1, 40000);
            const auto second = update(3, 40000);
            input.insert(input.end(), second.begin(), second.end());
        }
        else if (stage == 2)
        {
            ASSERT_EQ(data_bytes, std::size_t{65535});
            input = update(0, 14465);
        }
        else if (stage == 3)
        {
            ASSERT_EQ(data_bytes, std::size_t{80000});
            const std::array fields{header_field{":method", "GET"},
                header_field{":scheme", "http"}, header_field{":path", "/flow"}};
            const auto encoded = encoder.encode(fields);
            ASSERT_TRUE(encoded.has_value());
            append(input, {.type = frame_type::headers, .flags = 5, .stream_id = 7}, *encoded);
            const auto increase = encode_settings(settings{.initial_window_size = 32768});
            append(input, {.type = frame_type::settings}, increase);
            const auto credit = update(0, 40000);
            input.insert(input.end(), credit.begin(), credit.end());
        }
        else if (stage == 4)
        {
            ASSERT_EQ(data_bytes, std::size_t{112768});
            append(input, {.type = frame_type::settings}, config);
            const auto credit = update(7, 7232);
            input.insert(input.end(), credit.begin(), credit.end());
        }
        else if (stage == 5)
        {
            ASSERT_EQ(data_bytes, std::size_t{112768});
            input = update(7, 32768);
        }
        else
        {
            ASSERT_EQ(data_bytes, std::size_t{120000});
            ASSERT_EQ(ended, std::size_t{3});
            ASSERT_EQ(handled, std::size_t{3});
            co_return std::size_t{0};
        }
        ++stage;
        ASSERT_TRUE(input.size() <= buffer.size);
        std::memcpy(buffer.data, input.data(), input.size());
        co_return input.size();
    };
    header_compression response_decoder;
    static const std::array fixed_headers{header_field{"content-type", "text/plain"}};
    static const std::array fixed_trailers{header_field{"checksum", "ok"}};
    transport_writer writer = [&](cnetmod::const_buffer buffer)
        -> cnetmod::task<std::expected<void, std::error_code>>
    {
        std::size_t offset{};
        const auto wire = std::span{static_cast<const std::byte*>(buffer.data), buffer.size};
        while (offset + frame_header_size <= wire.size())
        {
            const auto header = decode_frame_header(wire.subspan(offset, frame_header_size));
            ASSERT_TRUE(header.has_value());
            if (!header)
                break;
            if (header->type == frame_type::headers)
            {
                const auto fields = response_decoder.decode(wire.subspan(offset + frame_header_size, header->length));
                ASSERT_TRUE(fields.has_value());
                if (fields)
                {
                    const auto value = [&](std::string_view name) -> std::string_view
                    {
                        const auto found = std::ranges::find_if(*fields,
                            [&](const auto& field)
                            {
                                return field.name == name;
                            });
                        return found == fields->end() ? std::string_view{} : std::string_view{found->value};
                    };
                    if ((header->flags & 1U) != 0)
                    {
                        ASSERT_EQ(value("checksum"), std::string_view{"ok"});
                        ASSERT_EQ(value("x-complete"), std::to_string(header->stream_id));
                    }
                    else
                    {
                        ASSERT_EQ(value("content-type"), std::string_view{"text/plain"});
                        ASSERT_EQ(value("x-stream"), std::to_string(header->stream_id));
                    }
                }
            }
            if (header->type == frame_type::data)
            {
                const auto payload = wire.subspan(
                    offset + frame_header_size, header->length);
                const auto expected = header->stream_id == 1 ? std::byte{'f'}
                    : header->stream_id == 3                  ? std::byte{'g'}
                                                             : std::byte{'h'};
                ASSERT_TRUE(std::ranges::all_of(payload, [expected](std::byte value)
                    {
                        return value == expected;
                    }));
                data_bytes += header->length;
            }
            if ((header->type == frame_type::data || header->type == frame_type::headers) &&
                (header->flags & 1U))
                ++ended;
            offset += frame_header_size + header->length;
        }
        ASSERT_EQ(offset, wire.size());
        co_return std::expected<void, std::error_code>{};
    };
    borrowed_streaming_server_handler fallback = [](server_request&, cnetmod::cancel_token&)
        -> cnetmod::task<server_response>
    {
        co_return server_response{};
    };
    session connection{*io, unused, std::move(fallback), {}, std::move(reader), std::move(writer),
        [&](server_request& request, cnetmod::cancel_token&) -> std::optional<server_response>
        {
            ++handled;
            // Reuse one synchronous response builder across streams. Every
            // flow-controlled response must promote the borrowed view before
            // the following request overwrites this storage.
            const auto fill = request.stream_id == 1 ? 'f'
                : request.stream_id == 3             ? 'g'
                                                     : 'h';
            std::ranges::fill(reusable_body, fill);
            return server_response{.headers = {{"x-stream", std::to_string(request.stream_id)}},
                .trailers = {{"x-complete", std::to_string(request.stream_id)}},
                .borrowed_body_text = reusable_body,
                .static_headers = static_header_fields::from<fixed_headers>(),
                .static_trailers = static_header_fields::from<fixed_trailers>()};
        },
        true, [](std::span<const header_field>)
        {
            return true;
        }};
    auto exercise = [&]() -> cnetmod::task<void>
    {
        co_await connection.run();
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

TEST(http2_shared_bodies_are_framed_by_reference_with_identical_wire_bytes)
{
    // 20 KB splits into one borrowed 16 KB DATA frame plus a borrowed 4 KB
    // remainder; 1 KB stays below the threshold and is copied.
    const auto large = std::make_shared<const std::string>(20'480, 'L');
    const auto small = std::make_shared<const std::string>(1'024, 's');
    auto exchange = [&](bool gather, std::size_t& borrowed_parts)
        -> std::vector<std::byte>
    {
        cnetmod::net_init network;
        auto io = cnetmod::make_io_context();
        cnetmod::socket unused;
        header_compression encoder;
        std::vector<std::byte> input{reinterpret_cast<const std::byte*>(preface.data()),
            reinterpret_cast<const std::byte*>(preface.data()) + preface.size()};
        auto settings = framed({.type = frame_type::settings}, {});
        input.insert(input.end(), settings.begin(), settings.end());
        for (const auto [id, path] : {std::pair{1U, "/large"},
                 std::pair{3U, "/small"}, std::pair{5U, "/large"},
                 std::pair{7U, "/owned"}})
        {
            const std::array headers{header_field{":method", "GET"},
                header_field{":scheme", "https"}, header_field{":path", path}};
            auto encoded = encoder.encode(headers);
            ASSERT_TRUE(encoded.has_value());
            auto head = framed({.type = frame_type::headers, .flags = 0x5, .stream_id = id}, *encoded);
            input.insert(input.end(), head.begin(), head.end());
        }
        bool delivered = false;
        auto reader = [&](cnetmod::mutable_buffer destination)
            -> cnetmod::task<std::expected<std::size_t, std::error_code>>
        {
            co_await cnetmod::post_awaitable{*io};
            if (delivered)
            {
                for (unsigned turn{}; turn < 8; ++turn)
                    co_await cnetmod::post_awaitable{*io};
                co_return std::size_t{};
            }
            delivered = true;
            std::memcpy(destination.data, input.data(), input.size());
            co_return input.size();
        };
        std::vector<std::byte> output;
        auto writer = [&](cnetmod::const_buffer source)
            -> cnetmod::task<std::expected<void, std::error_code>>
        {
            const auto* first = static_cast<const std::byte*>(source.data);
            output.insert(output.end(), first, first + source.size);
            co_return std::expected<void, std::error_code>{};
        };
        borrowed_streaming_server_handler handler =
            [](server_request&, cnetmod::cancel_token&) -> cnetmod::task<server_response>
        {
            co_return server_response{.status = 500};
        };
        sync_server_handler synchronous = [&](server_request& request, cnetmod::cancel_token&)
            -> std::optional<server_response>
        {
            for (const auto& header : request.headers)
                if (header.name == ":path")
                {
                    if (header.value == "/owned")
                        return server_response{.status = 200,
                            .body_text = std::string(20'480, 'O')};
                    return server_response{.status = 200,
                        .shared_body = header.value == "/large" ? large : small};
                }
            return server_response{.status = 404};
        };
        session connection{*io, unused, std::move(handler),
            {.max_bytes = 64 * 1024, .chunk_capacity = 4}, std::move(reader),
            std::move(writer), std::move(synchronous)};
        if (gather)
        {
            connection.set_gather_writer([&](std::span<const cnetmod::const_buffer> parts)
                                             -> cnetmod::task<std::expected<void, std::error_code>>
                {
                    for (const auto& part : parts)
                    {
                        const auto* first = static_cast<const std::byte*>(part.data);
                        // Shared cache bodies retain their original address;
                        // dynamically owned strings are also emitted as a
                        // separate DATA payload rather than copied into the
                        // connection's frame buffer.
                        if (first >= reinterpret_cast<const std::byte*>(large->data()) &&
                            first < reinterpret_cast<const std::byte*>(large->data() + large->size()))
                            ++borrowed_parts;
                        else if (part.size >= 4U * 1024U &&
                            std::to_integer<char>(*first) == 'O')
                            ++borrowed_parts;
                        output.insert(output.end(), first, first + part.size);
                    }
                    co_return std::expected<void, std::error_code>{};
                });
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
        return output;
    };
    std::size_t copied_parts{}, borrowed_parts{};
    const auto copied = exchange(false, copied_parts);
    const auto borrowed = exchange(true, borrowed_parts);
    ASSERT_EQ(copied_parts, std::size_t{0});
    // Three large responses, each framed as two borrowed DATA payloads.
    ASSERT_EQ(borrowed_parts, std::size_t{6});
    ASSERT_EQ(borrowed.size(), copied.size());
    ASSERT_TRUE(borrowed == copied);

    // The copied wire image carries both bodies byte for byte.
    std::array<std::size_t, 8> received{};
    for (std::size_t offset{}; offset + frame_header_size <= copied.size();)
    {
        auto header = decode_frame_header(std::span{copied}.subspan(offset, frame_header_size));
        ASSERT_TRUE(header.has_value());
        if (!header || offset + frame_header_size + header->length > copied.size())
            break;
        if (header->type == frame_type::data && header->stream_id < received.size())
        {
            const auto payload = std::span{copied}.subspan(offset + frame_header_size, header->length);
            const auto fill = header->stream_id == 3 ? std::byte{'s'}
                : header->stream_id == 7             ? std::byte{'O'}
                                                     : std::byte{'L'};
            ASSERT_TRUE(std::ranges::all_of(payload, [fill](std::byte value)
                {
                    return value == fill;
                }));
            received[header->stream_id] += header->length;
        }
        offset += frame_header_size + header->length;
    }
    ASSERT_EQ(received[1], large->size());
    ASSERT_EQ(received[3], small->size());
    ASSERT_EQ(received[5], large->size());
    ASSERT_EQ(received[7], std::size_t{20'480});
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

TEST(http2_header_name_validation_checks_the_entire_name)
{
    cnetmod::net_init network;
    for (const auto uppercase_at : std::array{-1, 0, 128})
    {
        auto io = cnetmod::make_io_context();
        cnetmod::socket unused;
        session connection{*io, unused};
        ASSERT_TRUE(connection.receive(bytes(preface)).has_value());
        ASSERT_TRUE(connection.receive(framed({.type = frame_type::settings}, {})).has_value());
        std::string name(129, 'a');
        if (uppercase_at >= 0)
            name[static_cast<std::size_t>(uppercase_at)] = 'A';
        header_compression encoder;
        const std::array fields{header_field{":method", "GET"},
            header_field{":scheme", "http"}, header_field{":path", "/"},
            header_field{std::move(name), "value"}};
        auto block = encoder.encode(fields);
        ASSERT_TRUE(block.has_value());
        ASSERT_EQ(connection.receive(framed(
                                         {.type = frame_type::headers, .flags = 5, .stream_id = 1}, *block))
                      .has_value(),
            uppercase_at < 0);
    }
}

RUN_TESTS()
