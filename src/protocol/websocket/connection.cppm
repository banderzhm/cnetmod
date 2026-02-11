module;

#include <cnetmod/config.hpp>
#include <cstring>

export module cnetmod.protocol.websocket:connection;

import std;
import :types;
import :frame;
import :handshake;
import cnetmod.protocol.http;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.coro.cancel;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cnetmod::ws {

// =============================================================================
// WebSocket 连接选项
// =============================================================================

export struct connect_options {
    std::string subprotocol;
    std::string origin;
#ifdef CNETMOD_HAS_SSL
    bool tls_verify = true;
    std::string tls_ca_file;
#endif
};

// =============================================================================
// ws::connection — 异步 WebSocket 连接
// =============================================================================

export class connection {
public:
    explicit connection(io_context& ctx) noexcept
        : ctx_(ctx) {}

    ~connection() { close_socket(); }

    // 不可复制
    connection(const connection&) = delete;
    auto operator=(const connection&) -> connection& = delete;

    // 可移动
    connection(connection&& o) noexcept
        : ctx_(o.ctx_)
        , sock_(std::move(o.sock_))
        , is_server_(o.is_server_)
        , connected_(std::exchange(o.connected_, false))
        , close_sent_(std::exchange(o.close_sent_, false))
        , close_received_(std::exchange(o.close_received_, false))
        , recv_buf_(std::move(o.recv_buf_))
#ifdef CNETMOD_HAS_SSL
        , ssl_ctx_(std::move(o.ssl_ctx_))
        , ssl_(std::move(o.ssl_))
        , secure_(std::exchange(o.secure_, false))
#endif
    {}

    // =========================================================================
    // 客户端：连接到 WebSocket 服务器
    // =========================================================================

    /// 连接到 ws:// 或 wss:// URL
    auto async_connect(std::string_view url_str,
                       const connect_options& opts = {})
        -> task<std::expected<void, std::error_code>>
    {
        auto url_r = http::url::parse(url_str);
        if (!url_r) co_return std::unexpected(make_error_code(ws_errc::handshake_failed));
        auto& u = *url_r;

        bool use_ssl = (u.scheme == "wss" || u.scheme == "https");

#ifndef CNETMOD_HAS_SSL
        if (use_ssl) {
            co_return std::unexpected(make_error_code(ws_errc::handshake_failed));
        }
#endif

        // TCP 连接
        auto addr_r = ip_address::from_string(u.host);
        if (!addr_r)
            co_return std::unexpected(make_error_code(errc::host_not_found));

        auto family = addr_r->is_v4() ? address_family::ipv4 : address_family::ipv6;
        auto sock_r = socket::create(family, socket_type::stream);
        if (!sock_r) co_return std::unexpected(sock_r.error());
        sock_ = std::move(*sock_r);

        std::expected<void, std::error_code> cr;
        if (cancel_token_)
            cr = co_await cnetmod::async_connect(ctx_, sock_,
                endpoint{*addr_r, u.port}, *cancel_token_);
        else
            cr = co_await cnetmod::async_connect(ctx_, sock_,
                endpoint{*addr_r, u.port});
        if (!cr) { close_socket(); co_return std::unexpected(cr.error()); }

        // SSL/TLS
#ifdef CNETMOD_HAS_SSL
        if (use_ssl) {
            auto ssl_ctx_r = ssl_context::client();
            if (!ssl_ctx_r) { close_socket(); co_return std::unexpected(ssl_ctx_r.error()); }
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
            if (!hs) { close_socket(); co_return std::unexpected(hs.error()); }
            secure_ = true;
        }
#endif

        // WebSocket 握手
        auto sec_key = generate_sec_key();
        auto expected_accept = compute_accept_key(sec_key);

        // Host header 含端口（非默认端口时）
        std::string host_header = u.host;
        if ((u.scheme == "ws" && u.port != 80) ||
            (u.scheme == "wss" && u.port != 443)) {
            host_header += ":" + std::to_string(u.port);
        }

        auto req = build_upgrade_request(host_header, u.path, sec_key,
                                         opts.subprotocol, opts.origin);
        auto req_data = req.serialize();

        auto wr = co_await async_write_all(req_data.data(), req_data.size());
        if (!wr) { close_socket(); co_return std::unexpected(wr.error()); }

        // 读取响应
        http::response_parser resp_parser;
        while (!resp_parser.ready()) {
            auto buf = recv_buf_.prepare(4096);
            auto rd = co_await async_read_some(
                static_cast<char*>(buf.data), buf.size);
            if (!rd || *rd == 0) {
                close_socket();
                co_return std::unexpected(make_error_code(ws_errc::handshake_failed));
            }
            recv_buf_.commit(*rd);

            auto readable = recv_buf_.data();
            auto consumed = resp_parser.consume(
                static_cast<const char*>(readable.data), readable.size);
            if (!consumed) {
                close_socket();
                co_return std::unexpected(consumed.error());
            }
            recv_buf_.consume(*consumed);
        }

        auto vr = validate_upgrade_response(resp_parser, expected_accept);
        if (!vr) { close_socket(); co_return std::unexpected(vr.error()); }

        is_server_ = false;
        connected_ = true;
        co_return {};
    }

    // =========================================================================
    // 服务端：接受 WebSocket 升级
    // =========================================================================

    /// 在已接受的 TCP socket 上执行 WebSocket 握手
    auto async_accept(socket client_sock)
        -> task<std::expected<void, std::error_code>>
    {
        sock_ = std::move(client_sock);

        // 读取客户端的 HTTP 升级请求
        http::request_parser req_parser;
        while (!req_parser.ready()) {
            auto buf = recv_buf_.prepare(4096);
            auto rd = co_await async_read_some(
                static_cast<char*>(buf.data), buf.size);
            if (!rd || *rd == 0) {
                close_socket();
                co_return std::unexpected(make_error_code(ws_errc::handshake_failed));
            }
            recv_buf_.commit(*rd);

            auto readable = recv_buf_.data();
            auto consumed = req_parser.consume(
                static_cast<const char*>(readable.data), readable.size);
            if (!consumed) {
                close_socket();
                co_return std::unexpected(consumed.error());
            }
            recv_buf_.consume(*consumed);
        }

        auto accept_key = validate_upgrade_request(req_parser);
        if (!accept_key) {
            close_socket();
            co_return std::unexpected(accept_key.error());
        }

        auto resp = build_upgrade_response(*accept_key);
        auto resp_data = resp.serialize();

        auto wr = co_await async_write_all(resp_data.data(), resp_data.size());
        if (!wr) { close_socket(); co_return std::unexpected(wr.error()); }

        is_server_ = true;
        connected_ = true;
        co_return {};
    }

#ifdef CNETMOD_HAS_SSL
    /// 在已接受的 TCP socket 上执行 TLS + WebSocket 握手（服务端）
    auto async_accept_tls(socket client_sock, ssl_context& ssl_ctx)
        -> task<std::expected<void, std::error_code>>
    {
        sock_ = std::move(client_sock);

        ssl_ = std::make_unique<ssl_stream>(ssl_ctx, ctx_, sock_);
        ssl_->set_accept_state();

        auto hs = co_await ssl_->async_handshake();
        if (!hs) { close_socket(); co_return std::unexpected(hs.error()); }
        secure_ = true;

        // 然后执行 WebSocket 握手（复用 async_accept 的逻辑）
        http::request_parser req_parser;
        while (!req_parser.ready()) {
            auto buf = recv_buf_.prepare(4096);
            auto rd = co_await async_read_some(
                static_cast<char*>(buf.data), buf.size);
            if (!rd || *rd == 0) {
                close_socket();
                co_return std::unexpected(make_error_code(ws_errc::handshake_failed));
            }
            recv_buf_.commit(*rd);

            auto readable = recv_buf_.data();
            auto consumed = req_parser.consume(
                static_cast<const char*>(readable.data), readable.size);
            if (!consumed) {
                close_socket();
                co_return std::unexpected(consumed.error());
            }
            recv_buf_.consume(*consumed);
        }

        auto accept_key = validate_upgrade_request(req_parser);
        if (!accept_key) {
            close_socket();
            co_return std::unexpected(accept_key.error());
        }

        auto resp = build_upgrade_response(*accept_key);
        auto resp_data = resp.serialize();

        auto wr = co_await async_write_all(resp_data.data(), resp_data.size());
        if (!wr) { close_socket(); co_return std::unexpected(wr.error()); }

        is_server_ = true;
        connected_ = true;
        co_return {};
    }
#endif

    // =========================================================================
    // 发送
    // =========================================================================

    /// 发送文本消息
    auto async_send_text(std::string_view text)
        -> task<std::expected<void, std::error_code>>
    {
        co_return co_await async_send(opcode::text,
            std::span{reinterpret_cast<const std::byte*>(text.data()), text.size()});
    }

    /// 发送二进制消息
    auto async_send_binary(std::span<const std::byte> data)
        -> task<std::expected<void, std::error_code>>
    {
        co_return co_await async_send(opcode::binary, data);
    }

    /// 发送 ping
    auto async_ping(std::span<const std::byte> payload = {})
        -> task<std::expected<void, std::error_code>>
    {
        co_return co_await async_send(opcode::ping, payload);
    }

    /// 发送通用帧
    auto async_send(opcode op, std::span<const std::byte> payload)
        -> task<std::expected<void, std::error_code>>
    {
        if (!connected_)
            co_return std::unexpected(make_error_code(ws_errc::not_connected));
        if (close_sent_)
            co_return std::unexpected(make_error_code(ws_errc::already_closed));

        bool do_mask = !is_server_; // 客户端必须 mask
        auto frame_data = build_frame(op, payload, do_mask);
        co_return co_await async_write_all(
            reinterpret_cast<const char*>(frame_data.data()), frame_data.size());
    }

    // =========================================================================
    // 接收
    // =========================================================================

    /// 接收一条完整的 WebSocket 消息（自动处理分片、ping/pong、close）
    auto async_recv() -> task<std::expected<ws_message, std::error_code>> {
        if (!connected_)
            co_return std::unexpected(make_error_code(ws_errc::not_connected));

        ws_message msg;
        bool first_frame = true;

        for (;;) {
            // 确保 recv_buf_ 有数据
            auto readable = recv_buf_.data();
            auto hdr_r = parse_frame_header(
                std::span{static_cast<const std::byte*>(readable.data), readable.size});

            if (!hdr_r && hdr_r.error() == make_error_code(ws_errc::need_more_data)) {
                // 读取更多数据
                auto buf = recv_buf_.prepare(4096);
                auto rd = co_await async_read_some(
                    static_cast<char*>(buf.data), buf.size);
                if (!rd || *rd == 0) {
                    connected_ = false;
                    co_return std::unexpected(
                        make_error_code(errc::connection_reset));
                }
                recv_buf_.commit(*rd);
                continue;
            }

            if (!hdr_r) co_return std::unexpected(hdr_r.error());

            auto& [hdr, hdr_size] = *hdr_r;
            auto total_frame = hdr_size + hdr.payload_length;

            // 确保完整帧可用
            while (recv_buf_.readable_bytes() < total_frame) {
                auto buf = recv_buf_.prepare(4096);
                auto rd = co_await async_read_some(
                    static_cast<char*>(buf.data), buf.size);
                if (!rd || *rd == 0) {
                    connected_ = false;
                    co_return std::unexpected(
                        make_error_code(errc::connection_reset));
                }
                recv_buf_.commit(*rd);
            }

            // 提取 payload
            readable = recv_buf_.data();
            auto payload_ptr = static_cast<const std::byte*>(readable.data) + hdr_size;
            std::vector<std::byte> payload_data(
                payload_ptr, payload_ptr + hdr.payload_length);

            // Unmask
            if (hdr.masked)
                apply_mask(payload_data, hdr.masking_key);

            recv_buf_.consume(total_frame);

            // 处理控制帧
            if (is_control(hdr.op)) {
                co_await handle_control_frame(hdr, payload_data);
                if (hdr.op == opcode::close) {
                    close_received_ = true;
                    // 构建 close 消息返回
                    ws_message close_msg;
                    close_msg.op = opcode::close;
                    close_msg.payload = std::move(payload_data);
                    co_return close_msg;
                }
                continue; // ping/pong 已处理，继续读
            }

            // 数据帧
            if (first_frame) {
                msg.op = hdr.op;
                first_frame = false;
            }

            msg.payload.insert(msg.payload.end(),
                payload_data.begin(), payload_data.end());

            if (hdr.fin) {
                co_return msg;
            }
            // 否则继续读取 continuation 帧
        }
    }

    // =========================================================================
    // 关闭
    // =========================================================================

    /// 发送 close 帧并等待对端回复
    auto async_close(std::uint16_t code = close_code::normal,
                     std::string_view reason = "")
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
        if (!wr) co_return std::unexpected(wr.error());

        close_sent_ = true;

        // 如果对端还没发 close，等待
        if (!close_received_) {
            auto msg = co_await async_recv();
            // 不管成功失败，连接都该关了
        }

#ifdef CNETMOD_HAS_SSL
        if (secure_ && ssl_) {
            (void)co_await ssl_->async_shutdown();
        }
#endif
        close_socket();
        connected_ = false;
        co_return {};
    }

    // =========================================================================
    // 附加已完成握手的 socket（供 ws::server 使用）
    // =========================================================================

    /// 将已完成 WebSocket 握手的 socket 附加到此 connection
    /// 调用后可直接使用 async_send_*/async_recv/async_close
    void attach(socket sock, bool as_server = true) noexcept {
        close_socket();
        sock_ = std::move(sock);
        is_server_ = as_server;
        connected_ = true;
        close_sent_ = false;
        close_received_ = false;
    }

    // =========================================================================
    // 取消令牌支持（配合 with_timeout 使用）
    // =========================================================================

    /// 设置取消令牌，后续内部 I/O 操作将使用此令牌
    void set_cancel_token(cancel_token* t) noexcept { cancel_token_ = t; }

    /// 清除取消令牌
    void clear_cancel_token() noexcept { cancel_token_ = nullptr; }

    // =========================================================================
    // 状态查询
    // =========================================================================

    [[nodiscard]] auto is_open() const noexcept -> bool { return connected_; }
    [[nodiscard]] auto is_server() const noexcept -> bool { return is_server_; }
#ifdef CNETMOD_HAS_SSL
    [[nodiscard]] auto is_secure() const noexcept -> bool { return secure_; }
#endif

private:
    // =========================================================================
    // 内部辅助
    // =========================================================================

    /// 读取（根据 secure_ / cancel_token_ 分发）
    auto async_read_some(char* buf, std::size_t len)
        -> task<std::expected<std::size_t, std::error_code>>
    {
#ifdef CNETMOD_HAS_SSL
        if (secure_ && ssl_) {
            co_return co_await ssl_->async_read(
                mutable_buffer{buf, len});
        }
#endif
        if (cancel_token_)
            co_return co_await cnetmod::async_read(ctx_, sock_,
                mutable_buffer{buf, len}, *cancel_token_);
        co_return co_await cnetmod::async_read(ctx_, sock_,
            mutable_buffer{buf, len});
    }

    /// 写入全部数据
    auto async_write_all(const char* data, std::size_t len)
        -> task<std::expected<void, std::error_code>>
    {
        std::size_t written = 0;
        while (written < len) {
#ifdef CNETMOD_HAS_SSL
            if (secure_ && ssl_) {
                auto w = co_await ssl_->async_write(
                    const_buffer{data + written, len - written});
                if (!w) co_return std::unexpected(w.error());
                written += *w;
                continue;
            }
#endif
            if (cancel_token_) {
                auto w = co_await cnetmod::async_write(ctx_, sock_,
                    const_buffer{data + written, len - written}, *cancel_token_);
                if (!w) co_return std::unexpected(w.error());
                written += *w;
            } else {
                auto w = co_await cnetmod::async_write(ctx_, sock_,
                    const_buffer{data + written, len - written});
                if (!w) co_return std::unexpected(w.error());
                written += *w;
            }
        }
        co_return {};
    }

    /// 处理控制帧（ping → auto pong, close → auto reply）
    auto handle_control_frame(const frame_header& hdr,
                              const std::vector<std::byte>& payload)
        -> task<void>
    {
        if (hdr.op == opcode::ping) {
            // 自动回复 pong
            bool do_mask = !is_server_;
            auto pong = build_frame(opcode::pong, payload, do_mask);
            (void)co_await async_write_all(
                reinterpret_cast<const char*>(pong.data()), pong.size());
        } else if (hdr.op == opcode::close && !close_sent_) {
            // 自动回复 close
            bool do_mask = !is_server_;
            auto close_reply = build_frame(opcode::close, payload, do_mask);
            (void)co_await async_write_all(
                reinterpret_cast<const char*>(close_reply.data()),
                close_reply.size());
            close_sent_ = true;
        }
    }

    void close_socket() noexcept {
#ifdef CNETMOD_HAS_SSL
        ssl_.reset();
        ssl_ctx_.reset();
        secure_ = false;
#endif
        sock_.close();
    }

    io_context& ctx_;
    socket sock_;
    bool is_server_ = false;
    bool connected_ = false;
    bool close_sent_ = false;
    bool close_received_ = false;
    dynamic_buffer recv_buf_{8192};
    cancel_token* cancel_token_{nullptr};

#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context> ssl_ctx_;
    std::unique_ptr<ssl_stream> ssl_;
    bool secure_ = false;
#endif
};

} // namespace cnetmod::ws
