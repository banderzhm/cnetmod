module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.dns.client;

import std;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.core.address;
import cnetmod.core.socket;
import cnetmod.core.dns;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.protocol.udp;
import cnetmod.protocol.http;
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

udp_client::udp_client(io_context& ctx) : ctx_(ctx), sock_(ctx) {}

auto udp_client::query(const endpoint& server, const message& msg)
    -> task<std::expected<message, std::error_code>>
{
    if (!sock_.is_open()) {
        auto opened = sock_.open(server.address().is_v4()
            ? address_family::ipv4 : address_family::ipv6);
        if (!opened) co_return std::unexpected(opened.error());
    }
    auto raw = serialize_message(msg);
    if (!raw) co_return std::unexpected(raw.error());
    auto sent = co_await async_sendto(ctx_, sock_.native_socket(),
        const_buffer{raw->data(), raw->size()}, server);
    if (!sent) co_return std::unexpected(sent.error());

    std::array<std::byte, 4096> buf{};
    endpoint from;
    auto n = co_await async_recvfrom(ctx_, sock_.native_socket(),
        mutable_buffer{buf.data(), buf.size()}, from);
    if (!n) co_return std::unexpected(n.error());
    co_return parse_message(std::span<const std::byte>{buf.data(), *n});
}

tcp_client::tcp_client(io_context& ctx) : ctx_(ctx) {}

auto tcp_client::query(std::string_view host, std::uint16_t port, const message& msg)
    -> task<std::expected<message, std::error_code>>
{
    auto connected = co_await async_connect_happy_eyeballs(ctx_, host, port);
    if (!connected) co_return std::unexpected(connected.error());
    sock_ = std::move(connected->sock);
    auto wr = co_await write_tcp_message(ctx_, sock_, msg);
    if (!wr) co_return std::unexpected(wr.error());
    co_return co_await read_tcp_message(ctx_, sock_);
}

doh_client::doh_client(io_context& ctx, std::string endpoint_url)
    : http_(ctx), endpoint_url_(std::move(endpoint_url)) {}

auto doh_client::query(const message& msg)
    -> task<std::expected<message, std::error_code>>
{
    auto raw = serialize_message(msg);
    if (!raw) co_return std::unexpected(raw.error());
    http::request req(http::http_method::POST, endpoint_url_);
    req.set_header("Accept", "application/dns-message");
    req.set_header("Content-Type", "application/dns-message");
    req.set_body(std::string(reinterpret_cast<const char*>(raw->data()), raw->size()));

    auto resp = co_await http_.send(req);
    if (!resp) co_return std::unexpected(resp.error());
    if (resp->status_code() < 200 || resp->status_code() >= 300) {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }
    auto body = resp->body();
    co_return parse_message(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(body.data()), body.size()});
}

#ifdef CNETMOD_HAS_SSL
dot_client::dot_client(io_context& ctx) : ctx_(ctx) {}

auto dot_client::query(std::string_view host, std::uint16_t port, const message& msg)
    -> task<std::expected<message, std::error_code>>
{
    auto connect_r = co_await async_connect_happy_eyeballs(ctx_, host, port);
    if (!connect_r) co_return std::unexpected(connect_r.error());
    sock_ = std::move(connect_r->sock);

    auto ssl_ctx_r = ssl_context::client();
    if (!ssl_ctx_r) co_return std::unexpected(ssl_ctx_r.error());
    ssl_ctx_ = std::make_unique<ssl_context>(std::move(*ssl_ctx_r));
    ssl_ctx_->set_verify_peer(true);
    (void)ssl_ctx_->set_default_ca();
    ssl_ = std::make_unique<ssl_stream>(*ssl_ctx_, ctx_, sock_);
    ssl_->set_hostname(host);
    ssl_->set_connect_state();
    auto hs = co_await ssl_->async_handshake();
    if (!hs) co_return std::unexpected(hs.error());

    auto wr = co_await ssl_write_tcp_message(*ssl_, msg);
    if (!wr) co_return std::unexpected(wr.error());
    co_return co_await ssl_read_tcp_message(*ssl_);
}
#endif

} // namespace cnetmod::dns
