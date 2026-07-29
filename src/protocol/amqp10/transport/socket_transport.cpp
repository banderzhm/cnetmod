module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp10;
import :socket_transport;
import std;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.dns;
import cnetmod.executor.async_op;
import :amqp_value_codec;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :protocol_error;

namespace cnetmod::amqp10 {
struct socket_transport::impl
{
    io_context& ctx;
    socket sock;
#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context> tls_context;
    std::unique_ptr<ssl_stream> tls_stream;
#endif
    explicit impl(io_context& c)
        : ctx(c) {}

    auto read_some(mutable_buffer b, cancel_token& t)
        -> task<std::expected<std::size_t, std::error_code>>
    {
#ifdef CNETMOD_HAS_SSL
        if (tls_stream)
            co_return co_await tls_stream->async_read(b);
#endif
        co_return co_await async_read(ctx, sock, b, t);
    }

    auto write_all(const_buffer b, cancel_token& t)
        -> task<std::expected<void, std::error_code>>
    {
#ifdef CNETMOD_HAS_SSL
        if (tls_stream)
            co_return co_await tls_stream->async_write_all(b);
#endif
        co_return co_await async_write_all(ctx, sock, b, t);
    }

    auto read_exact(std::span<std::byte> b, cancel_token& t)
        -> task<std::expected<void, std::error_code>>
    {
        std::size_t done = 0;
        while (done < b.size())
        {
            if (t.is_cancelled())
                co_return std::unexpected(make_error_code(errc::cancelled));
            auto n = co_await read_some(
                mutable_buffer{b.data() + done, b.size() - done}, t);
            if (!n)
                co_return std::unexpected(n.error());
            if (*n == 0)
                co_return std::unexpected(make_error_code(errc::connection_closed));
            done += *n;
        }
        co_return {};
    }
};

socket_transport::socket_transport(io_context& c)
    : impl_(std::make_unique<impl>(c)) {}

socket_transport::~socket_transport() = default;
socket_transport::socket_transport(socket_transport&&) noexcept = default;
auto socket_transport::operator=(socket_transport&&) noexcept
    -> socket_transport& = default;

auto socket_transport::connect(const endpoint& e, cancel_token& t)
    -> task<std::expected<void, error>>
{
    if (t.is_cancelled())
        co_return std::unexpected(make_error(error_stage::cancelled,
            errc::cancelled,
            "connection cancelled"));
    auto connected = co_await async_connect_happy_eyeballs(
        impl_->ctx, e.host, e.port,
        happy_eyeballs_options{.connect_timeout = e.connect_timeout});
    if (!connected)
        co_return std::unexpected(
            error{.stage = error_stage::transport,
                .code = connected.error(),
                .message = "AMQP TCP connection failed",
                .retryable = true});
    impl_->sock = std::move(connected->sock);
#ifdef CNETMOD_HAS_SSL
    if (e.tls.enabled)
    {
        auto context = ssl_context::client();
        if (!context)
            co_return std::unexpected(
                error{.stage = error_stage::tls,
                    .code = context.error(),
                    .message = "cannot create TLS context"});
        impl_->tls_context = std::make_unique<ssl_context>(std::move(*context));
        impl_->tls_context->set_verify_peer(e.tls.verify_peer);
        if (e.tls.ca_file.empty())
        {
            if (e.tls.verify_peer)
                (void)impl_->tls_context->set_default_ca();
        }
        else if (auto r = impl_->tls_context->load_ca_file(e.tls.ca_file); !r)
            co_return std::unexpected(
                error{.stage = error_stage::tls,
                    .code = r.error(),
                    .message = "cannot load TLS CA"});
        if (!e.tls.certificate_file.empty())
            if (auto r = impl_->tls_context->load_cert_file(e.tls.certificate_file);
                !r)
                co_return std::unexpected(
                    error{.stage = error_stage::tls,
                        .code = r.error(),
                        .message = "cannot load TLS certificate"});
        if (!e.tls.private_key_file.empty())
            if (auto r = impl_->tls_context->load_key_file(e.tls.private_key_file);
                !r)
                co_return std::unexpected(
                    error{.stage = error_stage::tls,
                        .code = r.error(),
                        .message = "cannot load TLS key"});
        impl_->tls_stream = std::make_unique<ssl_stream>(*impl_->tls_context,
            impl_->ctx, impl_->sock);
        impl_->tls_stream->set_connect_state();
        impl_->tls_stream->set_hostname(
            e.tls.server_name.empty() ? e.host : e.tls.server_name);
        auto hs = co_await impl_->tls_stream->async_handshake();
        if (!hs)
            co_return std::unexpected(
                error{.stage = error_stage::tls,
                    .code = hs.error(),
                    .message = "TLS handshake failed"});
    }
#else
    if (e.tls.enabled)
        co_return std::unexpected(make_error(error_stage::tls,
            errc::protocol_state,
            "TLS support is not enabled"));
#endif
    if (t.is_cancelled())
    {
        close();
        co_return std::unexpected(make_error(error_stage::cancelled,
            errc::cancelled,
            "connection cancelled"));
    }
    co_return {};
}

auto socket_transport::write_header(protocol_header h, cancel_token& t)
    -> task<std::expected<void, error>>
{
    auto b = encode_protocol_header(h);
    auto r = co_await impl_->write_all(const_buffer{b.data(), b.size()}, t);
    if (!r)
        co_return std::unexpected(
            error{.stage = error_stage::transport,
                .code = r.error(),
                .message = "AMQP protocol header write failed",
                .retryable = true});
    co_return {};
}

auto socket_transport::read_header(cancel_token& t)
    -> task<std::expected<protocol_header, error>>
{
    std::array<std::byte, 8> b{};
    auto r = co_await impl_->read_exact(b, t);
    if (!r)
        co_return std::unexpected(
            error{.stage = error_stage::transport,
                .code = r.error(),
                .message = "AMQP protocol header read failed",
                .retryable = true});
    auto decoded = decode_protocol_header(b);
    if (!decoded)
        co_return std::unexpected(
            error{.stage = error_stage::handshake,
                .code = decoded.error(),
                .message = "invalid AMQP protocol header"});
    co_return *decoded;
}

auto socket_transport::write_frame(const frame& f, cancel_token& t)
    -> task<std::expected<void, error>>
{
    auto b = encode_frame(f);
    auto r = co_await impl_->write_all(const_buffer{b.data(), b.size()}, t);
    if (!r)
        co_return std::unexpected(
            error{.stage = error_stage::transport,
                .code = r.error(),
                .message = "AMQP frame write failed",
                .retryable = true});
    co_return {};
}

auto socket_transport::read_frame(std::uint32_t maximum, cancel_token& t)
    -> task<std::expected<frame, error>>
{
    std::array<std::byte, 8> header{};
    auto r = co_await impl_->read_exact(header, t);
    if (!r)
        co_return std::unexpected(
            error{.stage = error_stage::transport,
                .code = r.error(),
                .message = "AMQP frame header read failed",
                .retryable = true});
    decoder d(header);
    auto size = d.read_u32();
    if (!size || *size < 8 || *size > maximum)
        co_return std::unexpected(make_error(error_stage::protocol,
            errc::frame_size_too_large,
            "invalid AMQP frame size"));
    binary all(*size);
    std::ranges::copy(header, all.begin());
    if (*size > 8)
    {
        auto body = co_await impl_->read_exact(std::span(all).subspan(8), t);
        if (!body)
            co_return std::unexpected(
                error{.stage = error_stage::transport,
                    .code = body.error(),
                    .message = "AMQP frame body read failed",
                    .retryable = true});
    }
    auto decoded = decode_frame(all, maximum);
    if (!decoded)
        co_return std::unexpected(
            error{.stage = error_stage::protocol,
                .code = decoded.error(),
                .message = "invalid AMQP frame"});
    co_return std::move(*decoded);
}

auto socket_transport::shutdown() -> task<void>
{
#ifdef CNETMOD_HAS_SSL
    if (impl_->tls_stream)
        (void)co_await impl_->tls_stream->async_shutdown();
#endif
    impl_->sock.shutdown_both();
    impl_->sock.close();
    co_return;
}

void socket_transport::close() noexcept
{
    impl_->sock.close();
}

auto socket_transport::is_open() const noexcept -> bool
{
    return impl_->sock.is_open();
}
} // namespace cnetmod::amqp10
