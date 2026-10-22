module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.websocket;

import std;
import cnetmod.core.buffer;
import cnetmod.core.dns;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :connection;

namespace cnetmod::ws {
namespace {
    auto valid_message_utf8(std::span<const std::byte> text) noexcept -> bool
    {
        for (std::size_t i = 0; i < text.size();)
        {
            const auto first = std::to_integer<unsigned char>(text[i++]);
            if (first < 0x80)
                continue;
            unsigned count;
            std::uint32_t value;
            if (first >= 0xc2 && first <= 0xdf)
                count = 1, value = first & 0x1f;
            else if (first >= 0xe0 && first <= 0xef)
                count = 2, value = first & 0x0f;
            else if (first >= 0xf0 && first <= 0xf4)
                count = 3, value = first & 0x07;
            else
                return false;
            if (text.size() - i < count)
                return false;
            for (unsigned j = 0; j < count; ++j)
            {
                const auto next = std::to_integer<unsigned char>(text[i++]);
                if ((next & 0xc0) != 0x80)
                    return false;
                value = (value << 6) | (next & 0x3f);
            }
            if ((count == 2 && value < 0x800) || (count == 3 && value < 0x10000) ||
                (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff)
                return false;
        }
        return true;
    }

    auto valid_close_payload(std::span<const std::byte> payload) noexcept -> bool
    {
        if (payload.empty())
            return true;
        if (payload.size() == 1)
            return false;
        const auto code = (std::to_integer<unsigned>(payload[0]) << 8) |
            std::to_integer<unsigned>(payload[1]);
        return ((code >= 1000 && code <= 1014 && code != 1004 &&
                    code != 1005 && code != 1006) || (code >= 3000 && code <= 4999)) &&
            valid_message_utf8(payload.subspan(2));
    }
}

message_writer::message_writer(std::vector<std::byte>& output, bool mask,
    std::size_t limit) : output_(output), mask_(mask), limit_(limit) {}

auto message_writer::send(opcode op, std::span<const std::byte> payload)
    -> std::expected<void, std::error_code>
{
    if (!error_ && (op != opcode::text && op != opcode::binary))
        error_ = make_error_code(ws_errc::reserved_opcode);
    if (!error_ && (payload.size() > limit_ - 14 ||
        output_.size() > limit_ - 14 - payload.size()))
        error_ = std::make_error_code(std::errc::message_size);
    if (!error_ && op == opcode::text && !valid_message_utf8(payload))
        error_ = make_error_code(ws_errc::invalid_utf8);
    if (error_)
        return std::unexpected(error_);
    append_frame_into(output_, op, payload, mask_);
    return {};
}

auto connection::async_process_messages(message_handler_fn handler,
    std::size_t max_message_size) -> task<std::expected<void, std::error_code>>
{
    if (!connected_)
        co_return std::unexpected(make_error_code(ws_errc::not_connected));
    if (!handler || max_message_size == 0 ||
        max_message_size > std::numeric_limits<std::size_t>::max() - 65536)
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));

    std::vector<std::byte> fragments;
    auto fragmented_op = opcode::continuation;
    message_writer writer(send_buf_, !is_server_, max_message_size + 65536);
    send_buf_.clear();
    send_buf_.reserve(256);
    std::error_code failure;
    std::uint16_t failure_close = close_code::protocol_error;
    bool received_close = false;
    while (connected_)
    {
        unsigned processed = 0;
        bool need_read = false;
        std::size_t next_read = 1024;
        while (recv_buf_.readable_bytes() && processed < 64)
        {
            const auto readable = recv_buf_.data();
            auto parsed = parse_frame_header(std::span{
                static_cast<const std::byte*>(readable.data), readable.size});
            if (!parsed)
            {
                if (parsed.error() != make_error_code(ws_errc::need_more_data))
                    failure = parsed.error();
                else
                    need_read = true;
                break;
            }
            const auto [header, header_size] = *parsed;
            const auto op = header.op;
            if (header.rsv1 || header.rsv2 || header.rsv3 ||
                (op != opcode::continuation && op != opcode::text && op != opcode::binary &&
                    op != opcode::close && op != opcode::ping && op != opcode::pong) ||
                header.masked != is_server_ ||
                (!is_control(op) && ((op == opcode::continuation) !=
                    (fragmented_op != opcode::continuation))))
            {
                failure = make_error_code(ws_errc::protocol_error);
                break;
            }
            if (!is_control(op) && (header.payload_length > max_message_size ||
                header.payload_length > max_message_size - fragments.size()))
            {
                failure = std::make_error_code(std::errc::message_size);
                failure_close = close_code::message_too_big;
                break;
            }
            const auto frame_size = header_size + static_cast<std::size_t>(header.payload_length);
            if (readable.size < frame_size)
            {
                need_read = true;
                next_read = std::clamp<std::size_t>(frame_size - readable.size, 1024, 65536);
                break;
            }
            // This connection owns mutable storage. Borrowing never crosses a
            // callback boundary or an I/O suspension that may grow the buffer.
            auto payload = std::span{const_cast<std::byte*>(
                static_cast<const std::byte*>(readable.data)) + header_size,
                static_cast<std::size_t>(header.payload_length)};
            if (header.masked)
                apply_mask(payload, header.masking_key);
            ++processed;
            if (is_control(op))
            {
                if (op == opcode::close)
                {
                    if (!valid_close_payload(payload))
                    {
                        failure = make_error_code(ws_errc::invalid_close_payload);
                        if (payload.size() > 2 && !valid_message_utf8(payload.subspan(2)))
                            failure_close = close_code::invalid_payload;
                        break;
                    }
                    if (!close_sent_)
                        append_frame_into(send_buf_, opcode::close, payload, !is_server_);
                    close_sent_ = close_received_ = received_close = true;
                }
                else if (op == opcode::ping)
                    append_frame_into(send_buf_, opcode::pong, payload, !is_server_);
            }
            else
            {
                auto message_op = op;
                std::span<const std::byte> message = payload;
                if (!header.fin || op == opcode::continuation)
                {
                    if (op != opcode::continuation)
                        fragmented_op = op;
                    fragments.insert(fragments.end(), payload.begin(), payload.end());
                    message_op = fragmented_op;
                    message = fragments;
                }
                if (header.fin)
                {
                    if (message_op == opcode::text && !valid_message_utf8(message))
                    {
                        failure = make_error_code(ws_errc::invalid_utf8);
                        failure_close = close_code::invalid_payload;
                        break;
                    }
                    const auto handled = handler(message_op, message, writer);
                    if (!handled || writer.error_)
                    {
                        failure = writer.error_ ? writer.error_ : handled.error();
                        failure_close = close_code::internal_error;
                        break;
                    }
                    fragments.clear();
                    fragmented_op = opcode::continuation;
                }
            }
            recv_buf_.consume(frame_size);
            if (received_close || send_buf_.size() >= 65536)
                break;
        }
        if (failure && !close_sent_)
        {
            const std::array code{static_cast<std::byte>(failure_close >> 8),
                static_cast<std::byte>(failure_close)};
            append_frame_into(send_buf_, opcode::close, code, !is_server_);
            close_sent_ = true;
        }
        if (!send_buf_.empty())
        {
            const auto written = co_await async_write_all(
                reinterpret_cast<const char*>(send_buf_.data()), send_buf_.size());
            send_buf_.clear();
            if (!written)
            {
                connected_ = false;
                co_return std::unexpected(written.error());
            }
        }
        if (failure || received_close)
        {
            connected_ = false;
            if (failure)
                co_return std::unexpected(failure);
            co_return {};
        }
        if (!need_read && recv_buf_.readable_bytes())
            continue;
        auto writable = recv_buf_.prepare(next_read);
        const auto read = co_await async_read_some(static_cast<char*>(writable.data), writable.size);
        if (!read || *read == 0)
        {
            connected_ = false;
            co_return std::unexpected(read ? make_error_code(errc::connection_reset) : read.error());
        }
        recv_buf_.commit(*read);
    }
    co_return {};
}

connection::connection(io_context& ctx) noexcept
    : ctx_(ctx) {}

connection::~connection()
{
    close_socket();
}

connection::connection(connection&& o) noexcept
    : ctx_(o.ctx_), sock_(std::move(o.sock_)), is_server_(o.is_server_), connected_(std::exchange(o.connected_, false)), close_sent_(std::exchange(o.close_sent_, false)), close_received_(std::exchange(o.close_received_, false)), recv_buf_(std::move(o.recv_buf_))
#ifdef CNETMOD_HAS_SSL
      ,
      ssl_ctx_(std::move(o.ssl_ctx_)),
      ssl_(std::move(o.ssl_)),
      secure_(std::exchange(o.secure_, false))
#endif
{
}

auto connection::async_connect(std::string_view url_str,
    const connect_options& opts)
    -> task<std::expected<void, std::error_code>>
{
    auto url_r = http::url::parse(url_str);
    if (!url_r)
        co_return std::unexpected(make_error_code(ws_errc::handshake_failed));
    auto& u = *url_r;

    bool use_ssl = (u.scheme == "wss" || u.scheme == "https");

#ifndef CNETMOD_HAS_SSL
    if (use_ssl)
    {
        co_return std::unexpected(make_error_code(ws_errc::handshake_failed));
    }
#endif

    auto connect_r = co_await async_connect_happy_eyeballs(ctx_, u.host, u.port);
    if (!connect_r)
    {
        co_return std::unexpected(connect_r.error());
    }
    sock_ = std::move(connect_r->sock);

    // SSL/TLS
#ifdef CNETMOD_HAS_SSL
    if (use_ssl)
    {
        auto ssl_ctx_r = ssl_context::client();
        if (!ssl_ctx_r)
        {
            close_socket();
            co_return std::unexpected(ssl_ctx_r.error());
        }
        ssl_ctx_ = std::make_unique<ssl_context>(std::move(*ssl_ctx_r));
        ssl_ctx_->set_verify_peer(opts.tls_verify);

        if (!opts.tls_ca_file.empty())
            (void)ssl_ctx_->load_ca_file(opts.tls_ca_file);
        else if (opts.tls_verify)
            (void)ssl_ctx_->set_default_ca();

        ssl_ = std::make_unique<ssl_stream>(*ssl_ctx_, ctx_, sock_);
        ssl_->set_connect_state();
        ssl_->set_hostname(u.host);

        auto hs = co_await ssl_->async_handshake();
        if (!hs)
        {
            close_socket();
            co_return std::unexpected(hs.error());
        }
        secure_ = true;
    }
#endif

    // WebSocket handshake
    auto sec_key = generate_sec_key();
    auto expected_accept = compute_accept_key(sec_key);

    // Host header with port (for non-default ports)
    auto host_header = http::format_authority(
        u.host, u.port, u.scheme == "wss" || u.scheme == "https");

    auto req = build_upgrade_request(host_header, u.path, sec_key,
        opts.subprotocol, opts.origin);
    auto req_data = req.serialize();

    auto wr = co_await async_write_all(req_data.data(), req_data.size());
    if (!wr)
    {
        close_socket();
        co_return std::unexpected(wr.error());
    }

    // Read response
    http::response_parser resp_parser;
    while (!resp_parser.ready())
    {
        auto buf = recv_buf_.prepare(4096);
        auto rd = co_await async_read_some(static_cast<char*>(buf.data), buf.size);
        if (!rd || *rd == 0)
        {
            close_socket();
            co_return std::unexpected(make_error_code(ws_errc::handshake_failed));
        }
        recv_buf_.commit(*rd);

        auto readable = recv_buf_.data();
        auto consumed = resp_parser.consume(
            static_cast<const char*>(readable.data), readable.size);
        if (!consumed)
        {
            close_socket();
            co_return std::unexpected(consumed.error());
        }
        recv_buf_.consume(*consumed);
    }

    auto vr = validate_upgrade_response(resp_parser, expected_accept);
    if (!vr)
    {
        close_socket();
        co_return std::unexpected(vr.error());
    }

    const auto pending = resp_parser.take_buffered_data();
    if (!pending.empty())
    {
        auto destination = recv_buf_.prepare(pending.size());
        std::memcpy(destination.data, pending.data(), pending.size());
        recv_buf_.commit(pending.size());
    }
    is_server_ = false;
    connected_ = true;
    co_return {};
}

auto connection::async_accept(socket client_sock)
    -> task<std::expected<void, std::error_code>>
{
    sock_ = std::move(client_sock);
    reset_server_request_metadata();

    // Read client HTTP upgrade request
    http::request_parser req_parser;
    while (!req_parser.ready())
    {
        auto buf = recv_buf_.prepare(4096);
        auto rd = co_await async_read_some(static_cast<char*>(buf.data), buf.size);
        if (!rd || *rd == 0)
        {
            close_socket();
            co_return std::unexpected(make_error_code(ws_errc::handshake_failed));
        }
        recv_buf_.commit(*rd);

        auto readable = recv_buf_.data();
        auto consumed = req_parser.consume(static_cast<const char*>(readable.data),
            readable.size);
        if (!consumed)
        {
            close_socket();
            co_return std::unexpected(consumed.error());
        }
        recv_buf_.consume(*consumed);
    }

    store_server_request_metadata(req_parser);

    auto accept_key = validate_upgrade_request(req_parser);
    if (!accept_key)
    {
        close_socket();
        co_return std::unexpected(accept_key.error());
    }

    auto resp = build_upgrade_response(*accept_key);
    auto resp_data = resp.serialize();

    auto wr = co_await async_write_all(resp_data.data(), resp_data.size());
    if (!wr)
    {
        close_socket();
        co_return std::unexpected(wr.error());
    }

    const auto pending = req_parser.take_buffered_data();
    if (!pending.empty())
    {
        auto destination = recv_buf_.prepare(pending.size());
        std::memcpy(destination.data, pending.data(), pending.size());
        recv_buf_.commit(pending.size());
    }
    is_server_ = true;
    connected_ = true;
    co_return {};
}

#ifdef CNETMOD_HAS_SSL
auto connection::async_accept_tls(socket client_sock, ssl_context& ssl_ctx)
    -> task<std::expected<void, std::error_code>>
{
    sock_ = std::move(client_sock);
    reset_server_request_metadata();

    ssl_ = std::make_unique<ssl_stream>(ssl_ctx, ctx_, sock_);
    ssl_->set_accept_state();

    auto hs = co_await ssl_->async_handshake();
    if (!hs)
    {
        close_socket();
        co_return std::unexpected(hs.error());
    }
    secure_ = true;

    // Then perform WebSocket handshake (reuse async_accept logic)
    http::request_parser req_parser;
    while (!req_parser.ready())
    {
        auto buf = recv_buf_.prepare(4096);
        auto rd = co_await async_read_some(static_cast<char*>(buf.data), buf.size);
        if (!rd || *rd == 0)
        {
            close_socket();
            co_return std::unexpected(make_error_code(ws_errc::handshake_failed));
        }
        recv_buf_.commit(*rd);

        auto readable = recv_buf_.data();
        auto consumed = req_parser.consume(static_cast<const char*>(readable.data),
            readable.size);
        if (!consumed)
        {
            close_socket();
            co_return std::unexpected(consumed.error());
        }
        recv_buf_.consume(*consumed);
    }

    store_server_request_metadata(req_parser);

    auto accept_key = validate_upgrade_request(req_parser);
    if (!accept_key)
    {
        close_socket();
        co_return std::unexpected(accept_key.error());
    }

    auto resp = build_upgrade_response(*accept_key);
    auto resp_data = resp.serialize();

    auto wr = co_await async_write_all(resp_data.data(), resp_data.size());
    if (!wr)
    {
        close_socket();
        co_return std::unexpected(wr.error());
    }

    const auto pending = req_parser.take_buffered_data();
    if (!pending.empty())
    {
        auto destination = recv_buf_.prepare(pending.size());
        std::memcpy(destination.data, pending.data(), pending.size());
        recv_buf_.commit(pending.size());
    }
    is_server_ = true;
    connected_ = true;
    co_return {};
}
#endif

auto connection::async_send_text(std::string_view text)
    -> task<std::expected<void, std::error_code>>
{
    co_return co_await async_send(
        opcode::text,
        std::span{reinterpret_cast<const std::byte*>(text.data()), text.size()});
}

auto connection::async_send_binary(std::span<const std::byte> data)
    -> task<std::expected<void, std::error_code>>
{
    co_return co_await async_send(opcode::binary, data);
}

auto connection::async_ping(std::span<const std::byte> payload)
    -> task<std::expected<void, std::error_code>>
{
    co_return co_await async_send(opcode::ping, payload);
}

auto connection::async_send(opcode op, std::span<const std::byte> payload)
    -> task<std::expected<void, std::error_code>>
{
    if (!connected_)
        co_return std::unexpected(make_error_code(ws_errc::not_connected));
    if (close_sent_)
        co_return std::unexpected(make_error_code(ws_errc::already_closed));

    bool do_mask = !is_server_; // Client must mask
    build_frame_into(send_buf_, op, payload, do_mask);
    co_return co_await async_write_all(
        reinterpret_cast<const char*>(send_buf_.data()), send_buf_.size());
}

auto connection::async_recv()
    -> task<std::expected<ws_message, std::error_code>>
{
    if (!connected_)
        co_return std::unexpected(make_error_code(ws_errc::not_connected));

    ws_message msg;
    bool first_frame = true;

    for (;;)
    {
        // Ensure recv_buf_ has data
        auto readable = recv_buf_.data();
        auto hdr_r = parse_frame_header(std::span{
            static_cast<const std::byte*>(readable.data), readable.size});

        if (!hdr_r && hdr_r.error() == make_error_code(ws_errc::need_more_data))
        {
            // Read more data
            auto buf = recv_buf_.prepare(4096);
            auto rd =
                co_await async_read_some(static_cast<char*>(buf.data), buf.size);
            if (!rd || *rd == 0)
            {
                connected_ = false;
                co_return std::unexpected(make_error_code(errc::connection_reset));
            }
            recv_buf_.commit(*rd);
            continue;
        }

        if (!hdr_r)
            co_return std::unexpected(hdr_r.error());

        auto& [hdr, hdr_size] = *hdr_r;
        auto total_frame = hdr_size + hdr.payload_length;

        // Ensure complete frame is available
        while (recv_buf_.readable_bytes() < total_frame)
        {
            auto buf = recv_buf_.prepare(4096);
            auto rd =
                co_await async_read_some(static_cast<char*>(buf.data), buf.size);
            if (!rd || *rd == 0)
            {
                connected_ = false;
                co_return std::unexpected(make_error_code(errc::connection_reset));
            }
            recv_buf_.commit(*rd);
        }

        // Extract payload
        readable = recv_buf_.data();
        auto payload_ptr = static_cast<const std::byte*>(readable.data) + hdr_size;
        std::vector<std::byte> payload_data(payload_ptr,
            payload_ptr + hdr.payload_length);

        // Unmask
        if (hdr.masked)
            apply_mask(payload_data, hdr.masking_key);

        recv_buf_.consume(total_frame);

        // Handle control frames
        if (is_control(hdr.op))
        {
            co_await handle_control_frame(hdr, payload_data);
            if (hdr.op == opcode::close)
            {
                close_received_ = true;
                // Build close message to return
                ws_message close_msg;
                close_msg.op = opcode::close;
                close_msg.payload = std::move(payload_data);
                co_return close_msg;
            }
            continue; // ping/pong already handled, continue reading
        }

        // Data frame
        if (first_frame)
        {
            msg.op = hdr.op;
            first_frame = false;
            if (hdr.fin)
            {
                msg.payload = std::move(payload_data);
                co_return msg;
            }
        }

        msg.payload.insert(msg.payload.end(), payload_data.begin(),
            payload_data.end());

        if (hdr.fin)
        {
            co_return msg;
        }
        // Otherwise continue reading continuation frames
    }
}

auto connection::async_close(std::uint16_t code, std::string_view reason)
    -> task<std::expected<void, std::error_code>>
{
    if (!connected_)
        co_return std::unexpected(make_error_code(ws_errc::not_connected));
    if (close_sent_)
        co_return std::unexpected(make_error_code(ws_errc::already_closed));

    bool do_mask = !is_server_;
    auto frame_data = build_close_frame(code, reason, do_mask);

    auto wr = co_await async_write_all(
        reinterpret_cast<const char*>(frame_data.data()), frame_data.size());
    if (!wr)
        co_return std::unexpected(wr.error());

    close_sent_ = true;

    // If peer hasn't sent close yet, wait for it
    if (!close_received_)
    {
        auto msg = co_await async_recv();
        // Regardless of success/failure, connection should close
    }

#ifdef CNETMOD_HAS_SSL
    if (secure_ && ssl_)
    {
        (void)co_await ssl_->async_shutdown();
    }
#endif
    close_socket();
    connected_ = false;
    co_return {};
}

void connection::attach(socket sock, bool as_server) noexcept
{
    close_socket();
    recv_buf_.consume(recv_buf_.readable_bytes());
    send_buf_.clear();
    sock_ = std::move(sock);
    is_server_ = as_server;
    connected_ = true;
    close_sent_ = false;
    close_received_ = false;
}

void connection::set_cancel_token(cancel_token* t) noexcept
{
    cancel_token_ = t;
}

void connection::attach(socket sock, bool as_server, std::span<const std::byte> received)
{
    attach(std::move(sock), as_server);
    auto destination = recv_buf_.prepare(received.size());
    if (!received.empty())
        std::memcpy(destination.data, received.data(), received.size());
    recv_buf_.commit(received.size());
}

void connection::clear_cancel_token() noexcept
{
    cancel_token_ = nullptr;
}

[[nodiscard]] auto connection::is_open() const noexcept -> bool
{
    return connected_;
}

[[nodiscard]] auto connection::is_server() const noexcept -> bool
{
    return is_server_;
}

[[nodiscard]] auto connection::handshake_path() const noexcept
    -> std::string_view
{
    return handshake_path_;
}

[[nodiscard]] auto connection::handshake_query() const noexcept
    -> std::string_view
{
    return handshake_query_;
}

[[nodiscard]] auto connection::handshake_headers() const noexcept
    -> const http::header_map&
{
    return handshake_headers_;
}

#ifdef CNETMOD_HAS_SSL
[[nodiscard]] auto connection::is_secure() const noexcept -> bool
{
    return secure_;
}
#endif

auto connection::async_read_some(char* buf, std::size_t len)
    -> task<std::expected<std::size_t, std::error_code>>
{
#ifdef CNETMOD_HAS_SSL
    if (secure_ && ssl_)
    {
        co_return co_await ssl_->async_read(mutable_buffer{buf, len});
    }
#endif
    if (cancel_token_)
        co_return co_await cnetmod::async_read(
            ctx_, sock_, mutable_buffer{buf, len}, *cancel_token_);
    co_return co_await cnetmod::async_read(ctx_, sock_, mutable_buffer{buf, len});
}

auto connection::async_write_all(const char* data, std::size_t len)
    -> task<std::expected<void, std::error_code>>
{
#ifdef CNETMOD_HAS_SSL
    if (secure_ && ssl_)
    {
        co_return co_await ssl_->async_write_all(const_buffer{data, len});
    }
#endif
    if (cancel_token_)
    {
        co_return co_await cnetmod::async_write_all(
            ctx_, sock_, const_buffer{data, len}, *cancel_token_);
    }
    co_return co_await cnetmod::async_write_all(ctx_, sock_,
        const_buffer{data, len});
}

auto connection::handle_control_frame(const frame_header& hdr,
    const std::vector<std::byte>& payload)
    -> task<void>
{
    if (hdr.op == opcode::ping)
    {
        // Auto-reply with pong
        bool do_mask = !is_server_;
        auto pong = build_frame(opcode::pong, payload, do_mask);
        (void)co_await async_write_all(reinterpret_cast<const char*>(pong.data()),
            pong.size());
    }
    else if (hdr.op == opcode::close && !close_sent_)
    {
        // Auto-reply with close
        bool do_mask = !is_server_;
        auto close_reply = build_frame(opcode::close, payload, do_mask);
        (void)co_await async_write_all(
            reinterpret_cast<const char*>(close_reply.data()), close_reply.size());
        close_sent_ = true;
    }
}

void connection::close_socket() noexcept
{
#ifdef CNETMOD_HAS_SSL
    ssl_.reset();
    ssl_ctx_.reset();
    secure_ = false;
#endif
    sock_.close();
}

void connection::reset_server_request_metadata()
{
    handshake_path_.clear();
    handshake_query_.clear();
    handshake_headers_.clear();
}

void connection::store_server_request_metadata(
    const http::request_parser& req_parser)
{
    auto uri = req_parser.uri();
    auto qpos = uri.find('?');
    if (qpos != std::string_view::npos)
    {
        handshake_path_ = std::string(uri.substr(0, qpos));
        handshake_query_ = std::string(uri.substr(qpos + 1));
    }
    else
    {
        handshake_path_ = std::string(uri);
        handshake_query_.clear();
    }
    handshake_headers_ = req_parser.headers();
}

} // namespace cnetmod::ws
