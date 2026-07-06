module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.dns.server;

import std;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.core.address;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.executor.async_op;
import cnetmod.protocol.udp;
import cnetmod.protocol.tcp;
import cnetmod.protocol.dns.types;
import cnetmod.protocol.dns.codec;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cnetmod::dns {

namespace {

auto read_exact(io_context& ctx, socket& sock, mutable_buffer buf)
    -> task<std::expected<void, std::error_code>>
{
    auto* data = static_cast<std::byte*>(buf.data);
    std::size_t got = 0;
    while (got < buf.size) {
        auto n = co_await async_read(ctx, sock, mutable_buffer{data + got, buf.size - got});
        if (!n) co_return std::unexpected(n.error());
        if (*n == 0) co_return std::unexpected(make_error_code(std::errc::connection_reset));
        got += *n;
    }
    co_return {};
}

auto read_tcp_message(io_context& ctx, socket& sock)
    -> task<std::expected<message, std::error_code>>
{
    std::array<std::byte, 2> len{};
    auto lr = co_await read_exact(ctx, sock, mutable_buffer{len.data(), len.size()});
    if (!lr) co_return std::unexpected(lr.error());
    const auto want = (std::to_integer<std::size_t>(len[0]) << 8) |
                      std::to_integer<std::size_t>(len[1]);
    std::vector<std::byte> buf(want);
    auto br = co_await read_exact(ctx, sock, mutable_buffer{buf.data(), buf.size()});
    if (!br) co_return std::unexpected(br.error());
    co_return parse_message(buf);
}

auto write_tcp_message(io_context& ctx, socket& sock, const message& msg)
    -> task<std::expected<void, std::error_code>>
{
    auto raw = serialize_message(msg);
    if (!raw) co_return std::unexpected(raw.error());
    if (raw->size() > std::numeric_limits<std::uint16_t>::max()) {
        co_return std::unexpected(make_error_code(std::errc::message_size));
    }
    std::array<std::byte, 2> len{
        static_cast<std::byte>((raw->size() >> 8) & 0xff),
        static_cast<std::byte>(raw->size() & 0xff),
    };
    auto wr1 = co_await async_write_all(ctx, sock, const_buffer{len.data(), len.size()});
    if (!wr1) co_return std::unexpected(wr1.error());
    auto wr2 = co_await async_write_all(ctx, sock, const_buffer{raw->data(), raw->size()});
    if (!wr2) co_return std::unexpected(wr2.error());
    co_return {};
}

#ifdef CNETMOD_HAS_SSL
auto ssl_read_exact(ssl_stream& ssl, mutable_buffer buf)
    -> task<std::expected<void, std::error_code>>
{
    auto* data = static_cast<std::byte*>(buf.data);
    std::size_t got = 0;
    while (got < buf.size) {
        auto n = co_await ssl.async_read(mutable_buffer{data + got, buf.size - got});
        if (!n) co_return std::unexpected(n.error());
        if (*n == 0) co_return std::unexpected(make_error_code(std::errc::connection_reset));
        got += *n;
    }
    co_return {};
}

auto ssl_read_tcp_message(ssl_stream& ssl)
    -> task<std::expected<message, std::error_code>>
{
    std::array<std::byte, 2> len{};
    auto lr = co_await ssl_read_exact(ssl, mutable_buffer{len.data(), len.size()});
    if (!lr) co_return std::unexpected(lr.error());
    const auto want = (std::to_integer<std::size_t>(len[0]) << 8) |
                      std::to_integer<std::size_t>(len[1]);
    std::vector<std::byte> buf(want);
    auto br = co_await ssl_read_exact(ssl, mutable_buffer{buf.data(), buf.size()});
    if (!br) co_return std::unexpected(br.error());
    co_return parse_message(buf);
}

auto ssl_write_tcp_message(ssl_stream& ssl, const message& msg)
    -> task<std::expected<void, std::error_code>>
{
    auto raw = serialize_message(msg);
    if (!raw) co_return std::unexpected(raw.error());
    if (raw->size() > std::numeric_limits<std::uint16_t>::max()) {
        co_return std::unexpected(make_error_code(std::errc::message_size));
    }
    std::array<std::byte, 2> len{
        static_cast<std::byte>((raw->size() >> 8) & 0xff),
        static_cast<std::byte>(raw->size() & 0xff),
    };
    auto wr1 = co_await ssl.async_write_all(const_buffer{len.data(), len.size()});
    if (!wr1) co_return std::unexpected(wr1.error());
    auto wr2 = co_await ssl.async_write_all(const_buffer{raw->data(), raw->size()});
    if (!wr2) co_return std::unexpected(wr2.error());
    co_return {};
}
#endif

} // namespace

udp_server::udp_server(io_context& ctx) : ctx_(ctx), sock_(ctx) {}

auto udp_server::listen(std::string_view host, std::uint16_t port, socket_options opts)
    -> std::expected<void, std::error_code>
{
    auto addr = ip_address::from_string(host);
    if (!addr) return std::unexpected(addr.error());
    return sock_.open(endpoint{*addr, port}, opts);
}

void udp_server::set_handler(query_handler handler) { handler_ = std::move(handler); }
void udp_server::stop() noexcept { running_ = false; sock_.close(); }

auto udp_server::run() -> task<void> {
    running_ = true;
    std::array<std::byte, 4096> buf{};
    while (running_ && sock_.is_open()) {
        endpoint peer;
        auto n = co_await async_recvfrom(ctx_, sock_.native_socket(),
            mutable_buffer{buf.data(), buf.size()}, peer);
        if (!n || *n == 0) continue;

        auto req = parse_message(std::span<const std::byte>{buf.data(), *n});
        if (!req) continue;

        message resp;
        if (handler_) {
            resp = co_await handler_(*req, peer);
        } else {
            resp = *req;
            resp.rcode = response_code::not_implemented;
        }
        resp.id = req->id;
        resp.query = false;

        auto raw = serialize_message(resp);
        if (!raw) continue;
        (void)co_await async_sendto(ctx_, sock_.native_socket(),
            const_buffer{raw->data(), raw->size()}, peer);
    }
}

tcp_server::tcp_server(io_context& ctx) : ctx_(ctx), acc_(ctx) {}

auto tcp_server::listen(std::string_view host, std::uint16_t port, socket_options opts)
    -> std::expected<void, std::error_code>
{
    auto addr = ip_address::from_string(host);
    if (!addr) return std::unexpected(addr.error());
    return acc_.open(endpoint{*addr, port}, opts);
}

void tcp_server::set_handler(query_handler handler) { handler_ = std::move(handler); }
void tcp_server::stop() noexcept { running_ = false; acc_.close(); }

auto tcp_server::run() -> task<void> {
    running_ = true;
    while (running_ && acc_.is_open()) {
        auto accepted = co_await async_accept(ctx_, acc_.native_socket());
        if (!accepted) continue;
        spawn(ctx_, handle_client(std::move(*accepted)));
    }
}

auto tcp_server::handle_client(socket client) -> task<void> {
    auto peer = client.remote_endpoint().value_or(endpoint{});
    while (client.is_open()) {
        auto req = co_await read_tcp_message(ctx_, client);
        if (!req) break;
        message resp;
        if (handler_) {
            resp = co_await handler_(*req, peer);
        } else {
            resp = *req;
            resp.rcode = response_code::not_implemented;
        }
        resp.id = req->id;
        resp.query = false;
        auto wr = co_await write_tcp_message(ctx_, client, resp);
        if (!wr) break;
    }
    client.close();
}

#ifdef CNETMOD_HAS_SSL
dot_server::dot_server(io_context& ctx, dot_server_options opts)
    : ctx_(ctx), opts_(std::move(opts)), acc_(ctx) {}

auto dot_server::listen(std::string_view host, std::uint16_t port, socket_options sock_opts)
    -> std::expected<void, std::error_code>
{
    auto ctx_r = ssl_context::server();
    if (!ctx_r) return std::unexpected(ctx_r.error());
    ssl_ctx_ = std::make_unique<ssl_context>(std::move(*ctx_r));
    if (auto r = ssl_ctx_->load_cert_file(opts_.cert_file); !r) return std::unexpected(r.error());
    if (auto r = ssl_ctx_->load_key_file(opts_.key_file); !r) return std::unexpected(r.error());
    ssl_ctx_->set_verify_peer(opts_.verify_peer);
    auto addr = ip_address::from_string(host);
    if (!addr) return std::unexpected(addr.error());
    return acc_.open(endpoint{*addr, port}, sock_opts);
}

void dot_server::set_handler(query_handler handler) { handler_ = std::move(handler); }
void dot_server::stop() noexcept { running_ = false; acc_.close(); }

auto dot_server::run() -> task<void> {
    running_ = true;
    while (running_ && acc_.is_open()) {
        auto accepted = co_await async_accept(ctx_, acc_.native_socket());
        if (!accepted || !ssl_ctx_) continue;
        spawn(ctx_, handle_client(std::move(*accepted)));
    }
}

auto dot_server::handle_client(socket client) -> task<void> {
    ssl_stream ssl(*ssl_ctx_, ctx_, client);
    ssl.set_accept_state();
    auto hs = co_await ssl.async_handshake();
    if (!hs) {
        client.close();
        co_return;
    }

    auto peer = client.remote_endpoint().value_or(endpoint{});
    while (client.is_open()) {
        auto req = co_await ssl_read_tcp_message(ssl);
        if (!req) break;
        message resp;
        if (handler_) {
            resp = co_await handler_(*req, peer);
        } else {
            resp = *req;
            resp.rcode = response_code::not_implemented;
        }
        resp.id = req->id;
        resp.query = false;
        auto wr = co_await ssl_write_tcp_message(ssl, resp);
        if (!wr) break;
    }
    (void)co_await ssl.async_shutdown();
    client.close();
}
#endif

} // namespace cnetmod::dns
