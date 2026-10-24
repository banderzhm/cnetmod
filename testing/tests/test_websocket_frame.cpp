import std;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.net_init;
import cnetmod.core.log;
import cnetmod.core.socket;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.protocol.http;
import cnetmod.protocol.websocket;

namespace ws = cnetmod::ws;

namespace {
void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

void check_mask()
{
    // Every tail, alignment, and all four key bytes, including zero keys.
    for (const std::uint32_t key : {0U, 0x12345678U, 0xff00fe01U, 0xffffffffU})
    {
        const auto bytes = std::bit_cast<std::array<std::byte, 4>>(key);
        for (std::size_t offset = 0; offset < 16; ++offset)
        {
            for (std::size_t size = 0; size <= 257; ++size)
            {
                std::vector<std::byte> data(size + offset + 16, std::byte{0x5a});
                const auto original = data;
                auto span = std::span{data}.subspan(offset, size);
                ws::apply_mask(span, key);
                for (std::size_t i = 0; i < data.size(); ++i)
                {
                    const auto expected = i >= offset && i < offset + size
                        ? original[i] ^ bytes[(i - offset) % 4]
                        : original[i];
                    require(data[i] == expected, "mask differs from RFC byte-wise XOR or overwrote sentinel");
                }
                ws::apply_mask(span, key);
                require(data == original, "mask round trip");
            }
        }
    }
    ws::apply_mask({}, 0x12345678);
}

void check_reused_frames()
{
    std::vector<std::byte> frame;
    for (const std::size_t size : {65536, 65535, 126, 125, 16, 0, 126, 65536, 0})
    {
        std::vector<std::byte> payload(size);
        for (std::size_t i = 0; i < size; ++i)
            payload[i] = static_cast<std::byte>(i % 251);
        for (const bool masked : {false, true})
        {
            for (const bool fin : {false, true})
            {
                ws::build_frame_into(frame, ws::opcode::binary, payload, masked, fin);
                auto parsed = ws::parse_frame_header(frame);
                require(parsed.has_value(), "frame header round trip");
                const auto& [header, header_size] = *parsed;
                const auto expected_header = (size <= 125 ? 2U : size <= 65535 ? 4U
                                                                               : 10U) +
                    (masked ? 4U : 0U);
                require(header_size == expected_header, "canonical RFC length encoding");
                require(header.op == ws::opcode::binary && header.fin == fin && header.masked == masked,
                    "reused frame flags");
                require(header.payload_length == size && frame.size() == header_size + size,
                    "frame boundary length");
                require(!header.rsv1 && !header.rsv2 && !header.rsv3, "reused reserved bits");
                auto body = std::span{frame}.subspan(header_size);
                if (masked)
                    ws::apply_mask(body, header.masking_key);
                require(std::ranges::equal(body, payload), "reused frame payload");
                for (std::size_t partial = 0; partial < header_size; ++partial)
                {
                    auto result = ws::parse_frame_header(std::span{frame}.first(partial));
                    require(!result && result.error() == ws::make_error_code(ws::ws_errc::need_more_data),
                        "partial frame header");
                }
            }
        }
    }
    for (auto op : {ws::opcode::ping, ws::opcode::pong, ws::opcode::close})
    {
        std::vector<std::byte> payload(126);
        require(!ws::parse_frame_header(ws::build_frame(op, payload, false)), "oversized control frame");
        require(!ws::parse_frame_header(ws::build_frame(op, {}, false, false)), "fragmented control frame");
    }
}

void check_disconnected_operations()
{
    auto io = cnetmod::make_io_context();
    ws::connection connection(*io);
    ws::ws_context context(connection, "/ws", {}, {}, {});
    const auto run = [&]() -> cnetmod::task<void>
    {
        auto text = co_await context.send_text("hello");
        require(!text && text.error() == ws::make_error_code(ws::ws_errc::not_connected),
            "disconnected text send preserves connection errors");
        std::array<std::byte, 2> payload{std::byte{0x00}, std::byte{0xff}};
        auto binary = co_await context.send_binary(payload);
        require(!binary && binary.error() == ws::make_error_code(ws::ws_errc::not_connected),
            "disconnected binary send preserves connection errors");
        auto received = co_await context.recv();
        require(!received && received.error() == ws::make_error_code(ws::ws_errc::not_connected),
            "disconnected receive preserves connection errors");
    };
    cnetmod::sync_wait(run());
}

void check_appended_frames()
{
    for (bool mask : {false, true})
    {
        std::vector<std::byte> frames(3, std::byte{0x55});
        std::size_t offset = frames.size();
        for (std::size_t size : {0U, 5U, 125U, 126U, 65535U, 65536U})
        {
            std::vector<std::byte> payload(size, std::byte{0xa5});
            ws::append_frame_into(frames, ws::opcode::binary, payload, mask);
            auto parsed = ws::parse_frame_header(std::span{frames}.subspan(offset));
            require(parsed && parsed->first.payload_length == size, "appended frame header");
            auto body = std::span{frames}.subspan(offset + parsed->second, size);
            if (mask)
                ws::apply_mask(body, parsed->first.masking_key);
            require(std::ranges::equal(body, payload), "appended frame payload");
            offset += parsed->second + size;
        }
        require(frames[0] == std::byte{0x55} && frames[2] == std::byte{0x55},
            "appending preserves existing prefix");
    }
}

void check_sharded_stop()
{
    cnetmod::server_context context(2, 1);
    ws::server server(context);
    require(static_cast<bool>(server.listen("127.0.0.1", 0)), "sharded listener on ephemeral port");
    bool completed = false;
    const auto stop_later = [&]() -> cnetmod::task<void>
    {
        (void)co_await cnetmod::async_sleep(context.accept_io(), std::chrono::milliseconds{10});
        server.stop();
    };
    const auto work = [&]() -> cnetmod::task<void>
    {
        cnetmod::spawn(context.accept_io(), stop_later());
        co_await server.run();
        completed = true;
        context.stop();
    };
    cnetmod::spawn(context.accept_io(), work());
    context.run();
    require(completed, "stop joins all worker accept operations");
}

void check_http_route_upgrade_preserves_first_frame()
{
    cnetmod::server_context context(1, 1);
    cnetmod::http::server server(context);
    cnetmod::http::router routes;
    routes.get("/health", [](cnetmod::http::request_context& request)
        -> cnetmod::task<void>
        {
            request.text(cnetmod::http::status::ok, "ok");
            co_return;
        });
    routes.get("/ws", [](cnetmod::http::request_context& request)
        -> cnetmod::task<void>
        {
            auto accepted = ws::accept_upgrade(request,
                [](ws::ws_context& connection) -> cnetmod::task<void>
                {
                    (void)co_await connection.raw_connection().async_process_messages(
                        [](ws::opcode op, std::span<const std::byte> payload,
                            ws::message_writer& output)
                            -> std::expected<void, std::error_code>
                        {
                            return output.send(op, payload);
                        });
                });
            if (!accepted)
                request.text(cnetmod::http::status::bad_request,
                    "Bad WebSocket handshake");
            co_return;
        });
    server.set_router(std::move(routes));
    require(static_cast<bool>(server.listen("127.0.0.1", 0)),
        "HTTP listener for WebSocket upgrade");
    const auto endpoint = server.local_endpoint();
    require(endpoint.has_value(), "HTTP upgrade endpoint");
    auto listener = server.run();
    listener.handle().resume();

    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto peer = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        require(peer.has_value(), "WebSocket test socket");
        require((co_await cnetmod::async_connect(
                     context.accept_io(), *peer, *endpoint))
                    .has_value(),
            "connect WebSocket test socket");

        const std::string payload = "hello";
        auto frame = ws::build_frame(ws::opcode::text,
            std::as_bytes(std::span{payload}), true);
        std::string request =
            "GET /ws HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n";
        request.append(reinterpret_cast<const char*>(frame.data()), frame.size());
        require((co_await cnetmod::async_write_all(context.accept_io(), *peer,
                     cnetmod::const_buffer{request.data(), request.size()}))
                    .has_value(),
            "write handshake and first frame together");

        std::string wire;
        std::array<char, 1024> buffer{};
        while (wire.find("hello") == std::string::npos)
        {
            auto read = co_await cnetmod::async_read(context.accept_io(), *peer,
                cnetmod::mutable_buffer{buffer.data(), buffer.size()});
            require(read.has_value() && *read != 0,
                "read upgraded response and echoed frame");
            wire.append(buffer.data(), *read);
        }
        require(wire.starts_with("HTTP/1.1 101"),
            "HTTP route emits switching-protocols response");
        require(wire.contains("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo="),
            "HTTP route emits RFC 6455 accept key");
        require(wire.contains("hello"),
            "frame coalesced with handshake survives protocol transfer");

        const std::string unicode{"\xe2\x82\xac"};
        auto unicode_frame = ws::build_frame(ws::opcode::text,
            std::as_bytes(std::span{unicode}), true);
        require((co_await cnetmod::async_write_all(context.accept_io(), *peer,
                     cnetmod::const_buffer{unicode_frame.data(),
                         unicode_frame.size()}))
                    .has_value(),
            "write non-ASCII UTF-8 frame");
        while (wire.find(unicode) == std::string::npos)
        {
            auto read = co_await cnetmod::async_read(context.accept_io(), *peer,
                cnetmod::mutable_buffer{buffer.data(), buffer.size()});
            require(read.has_value() && *read != 0,
                "read non-ASCII UTF-8 echo");
            wire.append(buffer.data(), *read);
        }
        peer->close();
        server.stop();
        while (!listener.handle().done())
            co_await cnetmod::async_sleep(
                context.accept_io(), std::chrono::milliseconds{1});
        listener.handle().promise().result();
        context.stop();
    };
    cnetmod::spawn(context.accept_io(), exercise());
    context.run();
}
} // namespace

auto main() -> int
{
    cnetmod::net_init net;
    int result = 0;
    try
    {
        check_mask();
        check_reused_frames();
        check_disconnected_operations();
        check_appended_frames();
        check_sharded_stop();
        check_http_route_upgrade_preserves_first_frame();
        logger::info{"WebSocket mask, frame reuse, boundaries and control regressions passed"};
    }
    catch (const std::exception& error)
    {
        logger::error{"{}", error.what()};
        result = 1;
    }
    logger::shutdown();
    return result;
}
