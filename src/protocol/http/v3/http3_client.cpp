module;
#include <cnetmod/config.hpp>
#ifdef CNETMOD_ENABLE_QUIC
    #ifdef CNETMOD_HAS_SSL
module cnetmod.protocol.http.v3.client;
import std;
import cnetmod.core.buffer;
import cnetmod.core.ssl;
import cnetmod.core.address;
import cnetmod.core.error;
import cnetmod.core.dns;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.channel;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.coro.cancel;
import cnetmod.protocol.http;
import cnetmod.protocol.udp;
import cnetmod.protocol.quic;
import cnetmod.protocol.http.v3.session;

namespace cnetmod::http::v3 {
namespace detail {
    auto is_replay_safe(http_method method) noexcept -> bool
    {
        switch (method)
        {
        case http_method::GET:
        case http_method::HEAD:
        case http_method::OPTIONS:
        case http_method::TRACE:
            return true;
        default:
            return false;
        }
    }

    auto drive_connection(std::shared_ptr<quic::quic_connection> c,
        std::shared_ptr<channel<std::monostate>> completion) -> task<void>
    {
        (void)co_await c->run();
        (void)completion->try_send({});
    }

    auto consume_peer_unidirectional_stream(std::shared_ptr<quic::quic_connection> connection,
        http3_client_session& session, quic::stream_id stream) -> task<void>
    {
        dynamic_buffer wire{16384};
        for (;;)
        {
            auto received = co_await connection->async_recv(stream,
                wire.prepare(16384));
            if (!received)
            {
                if (received.error() != std::make_error_code(std::errc::operation_would_block))
                    co_return;
                auto ready = co_await connection->async_wait_readable(stream);
                if (!ready)
                    co_return;
                continue;
            }
            if (*received == 0U)
                co_return;
            wire.commit(*received);
            // A peer commonly sends the one-byte QPACK stream type first and
            // its initial capacity instruction in a later QUIC frame. The
            // empty encoder/decoder stream is legal, but completing consumption
            // at the type byte would drop that later data.
            const auto bytes = wire.readable_view();
            if (bytes.size() == 1U &&
                (bytes.front() == std::byte{0x02} || bytes.front() == std::byte{0x03}))
                continue;
            auto processed = session.process_peer_unidirectional_stream(stream, bytes);
            if (!processed)
            {
                if (processed.error() == std::make_error_code(std::errc::message_size))
                    continue;
                co_await connection->async_close(processed.error(),
                    "invalid peer HTTP/3 unidirectional stream");
                co_return;
            }
            // Critical unidirectional streams remain open for the connection
            // lifetime. Later GOAWAY and QPACK instructions reuse this stream.
            continue;
        }
    }

    auto consume_peer_unidirectional_streams(std::shared_ptr<quic::quic_connection> connection,
        http3_client_session& session) -> task<void>
    {
        while (!connection->is_closed())
        {
            auto stream = co_await connection->async_accept_stream();
            if (!stream)
                co_return;
            if ((*stream & 0x02U) == 0U)
                continue;
            spawn(connection->context(),
                consume_peer_unidirectional_stream(connection, session, *stream));
        }
    }
} // namespace detail

http3_client::http3_client(io_context& c, ssl_context& t, http3_client_options o) : ctx_(c), tls_(t), options_(std::move(o)) {}

auto http3_client::connect(std::string_view host, std::uint16_t port) -> task<std::expected<void, std::error_code>>
{
    if (host.empty() || port == 0U)
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    if (can_reuse_origin(host, port))
        co_return {};
    if (connection_)
    {
        co_await connection_->async_close({}, "HTTP/3 client replacing origin connection");
        session_.reset();
        co_await wait_for_connection_driver();
        connection_.reset();
    }
    auto addresses = co_await async_resolve_addresses(ctx_, host, std::to_string(port));
    if (!addresses || addresses->empty())
        co_return std::unexpected(std::make_error_code(std::errc::host_unreachable));
    const auto& address = addresses->front();
    udp::udp_socket socket(ctx_);
    auto opened = socket.open(address.is_v6() ? address_family::ipv6 : address_family::ipv4);
    if (!opened)
        co_return std::unexpected(opened.error());
    quic::quic_config config{};
    config.max_data = options_.h3_initial_max_data;
    config.max_stream_data = options_.h3_initial_max_stream_data;
    config.server_name = options_.tls_sni_host.empty() ? std::string(host) : options_.tls_sni_host;
    tls_.set_verify_peer(options_.verify_certificate);
    tls_.configure_alpn_client({"h3"});
    connection_ = std::make_shared<quic::quic_connection>(ctx_, std::move(socket), endpoint{address, port}, quic::quic_role::client, tls_, config);
    driver_completion_ = std::make_shared<channel<std::monostate>>(1);
    spawn(ctx_, detail::drive_connection(connection_, driver_completion_));
    const auto deadline = std::chrono::steady_clock::now() + options_.connect_timeout;
    while (connection_->state() == quic::connection_state::idle || connection_->state() == quic::connection_state::handshaking)
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            co_await connection_->async_close(std::make_error_code(std::errc::timed_out), "HTTP/3 QUIC handshake timed out");
            co_await wait_for_connection_driver();
            connection_.reset();
            co_return std::unexpected(std::make_error_code(std::errc::timed_out));
        }
        co_await async_sleep(ctx_, std::chrono::milliseconds{5});
    }
    if (connection_->state() != quic::connection_state::connected)
    {
        co_await wait_for_connection_driver();
        connection_.reset();
        co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
    }
    session_ = make_http3_client_session(*connection_, {});
    session_->configure_local_settings({options_.h3_max_header_list_size, options_.h3_qpack_max_table_capacity, options_.h3_qpack_blocked_streams});
    // RFC 9114 requires the client control stream and the two QPACK
    // unidirectional streams to exist before application request streams are
    // admitted.  Deferring this until send_request() leaves a scheduling gap:
    // the transport driver can observe a peer close between connect() returning
    // and async_open_stream(), which is reported as a misleading ENOTCONN.
    // Establish the HTTP/3 session while the successful QUIC handshake is
    // still owned by this coroutine and propagate any stream-open failure from
    // connect() itself.
    auto initialized = co_await session_->connect();
    if (!initialized)
    {
        session_.reset();
        co_await connection_->async_close(initialized.error(),
            "HTTP/3 control-stream initialization failed");
        co_await wait_for_connection_driver();
        connection_.reset();
        co_return std::unexpected(initialized.error());
    }
    spawn(ctx_, detail::consume_peer_unidirectional_streams(connection_, *session_));
    host_ = host;
    port_ = port;
    co_return {};
}

auto http3_client::send_request(const http3_request& r) -> task<std::expected<http3_response, std::error_code>>
{
    if (!session_)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    // A QUIC connection is pooled per origin.  Do not silently coalesce an
    // authority without proving certificate and origin-set eligibility.
    if ((!r.host.empty() && r.host != host_) || (r.port != 0U && r.port != port_))
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    if (!session_->accepting_requests())
    {
        if (!options_.retry_idempotent_requests || !detail::is_replay_safe(r.method))
            co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
        const auto origin = host_;
        const auto origin_port = port_;
        co_await close();
        auto reconnected = co_await connect(origin, origin_port);
        if (!reconnected)
            co_return std::unexpected(reconnected.error());
    }
    co_return co_await session_->send_request(r);
}

auto http3_client::send_request(const http3_request& r,
    cnetmod::cancel_token& token) -> task<std::expected<http3_response, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
    if (!session_)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    // A QUIC connection is pooled per origin.  Do not silently coalesce an
    // authority without proving certificate and origin-set eligibility.
    if ((!r.host.empty() && r.host != host_) || (r.port != 0U && r.port != port_))
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    if (!session_->accepting_requests())
    {
        if (!options_.retry_idempotent_requests || !detail::is_replay_safe(r.method))
            co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
        const auto origin = host_;
        const auto origin_port = port_;
        co_await close();
        auto reconnected = co_await connect(origin, origin_port);
        if (!reconnected)
            co_return std::unexpected(reconnected.error());
    }
    co_return co_await session_->send_request(r, token);
}

auto http3_client::send_request(const http3_request& r,
    cnetmod::deadline request_deadline)
    -> task<std::expected<http3_response, std::error_code>>
{
    co_return co_await cnetmod::with_deadline(ctx_, request_deadline,
        [&](cnetmod::cancel_token& token) { return send_request(r, token); });
}

auto http3_client::close() -> task<void>
{
    if (connection_)
        co_await connection_->async_close({}, "HTTP/3 client closed");
    co_await wait_for_connection_driver();
    session_.reset();
    connection_.reset();
    co_return;
}

auto http3_client::wait_for_connection_driver() -> task<void>
{
    if (!driver_completion_)
        co_return;
    (void)co_await driver_completion_->receive();
    driver_completion_.reset();
}

auto http3_client::is_connected() const noexcept -> bool
{
    return connection_ && !connection_->is_closed();
}

auto http3_client::peer_host() const noexcept -> std::string_view
{
    return host_;
}

auto http3_client::peer_port() const noexcept -> std::uint16_t
{
    return port_;
}

auto http3_client::can_reuse_origin(std::string_view host, std::uint16_t port) const noexcept -> bool
{
    return session_ && session_->accepting_requests() && host == host_ && port == port_;
}
} // namespace cnetmod::http::v3
    #endif
#endif
