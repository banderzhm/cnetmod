module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.http.v3.client;
import std;
import cnetmod.core.buffer;
import cnetmod.core.ssl;
import cnetmod.core.address;
import cnetmod.core.error;
import cnetmod.core.log;
import cnetmod.core.dns;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.channel;
import cnetmod.coro.mutex;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;
import cnetmod.protocol.http.semantics;
import cnetmod.protocol.udp;
import cnetmod.protocol.quic;
import cnetmod.protocol.http.v3.session;

namespace cnetmod::http::v3 {
namespace detail {
    struct local_path_receiver
    {
        std::shared_ptr<udp::udp_socket> socket;
        cancel_token cancellation;
        std::shared_ptr<channel<std::monostate>> completion{
            std::make_shared<channel<std::monostate>>(1)};
    };

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
        std::shared_ptr<channel<std::monostate>> completion,
        std::shared_ptr<std::atomic_bool> close_requested) -> task<void>
    {

        auto result = co_await c->run();
        // Closing the client cancels the outstanding UDP receive. IOCP may
        // report that completion with a provider-specific system error rather
        // than ERROR_OPERATION_ABORTED. Only suppress it after the client
        // explicitly requested shutdown: a peer- or transport-initiated close
        // must remain visible in the diagnostic log.
        if (!result && !close_requested->load(std::memory_order_acquire) &&
            result.error() != std::make_error_code(std::errc::operation_canceled))
        {
            logger::warn{"HTTP/3 transport driver exited: {} ({})",
                result.error().message(), result.error().value()};
        }

        (void)completion->try_send({});
    }

    auto drive_local_path_receiver(io_context& context,
        std::shared_ptr<quic::quic_connection> connection,
        std::shared_ptr<local_path_receiver> receiver) -> task<void>
    {
        std::array<std::byte, quic::max_udp_receive_payload> storage{};
        endpoint sender;
        while (!receiver->cancellation.is_cancelled() && !connection->is_closed())
        {
            auto received = co_await async_recvfrom(context,
                receiver->socket->native_socket(),
                mutable_buffer{storage.data(), storage.size()}, sender,
                receiver->cancellation);
            if (!received)
            {
                if (receiver->cancellation.is_cancelled())
                    break;
                // A separate candidate socket can observe ICMP feedback for
                // its own probe. Let QUIC's three-PTO validation state make
                // the decision instead of killing the established path.
                if (received.error() == make_error_code(errc::connection_refused) ||
                    received.error() == make_error_code(errc::connection_reset) ||
                    received.error() == make_error_code(errc::host_unreachable) ||
                    received.error() == make_error_code(errc::network_unreachable))
                    continue;
                logger::warn{"HTTP/3 local Multipath receiver exited: {} ({})",
                    received.error().message(), received.error().value()};
                break;
            }
            auto processed = co_await connection->process_datagram(
                std::span<const std::byte>{storage.data(), *received}, sender);
            if (!processed && !connection->is_closed())
            {
                logger::warn{"HTTP/3 local Multipath datagram rejected: {} ({})",
                    processed.error().message(), processed.error().value()};
            }
        }
        (void)receiver->completion->try_send({});
    }

    auto consume_peer_stream(std::shared_ptr<quic::quic_connection> connection,
        std::shared_ptr<http3_client_session> session, quic::stream_id stream) -> task<void>
    {
        dynamic_buffer wire{16384};
        const bool unidirectional = (stream & 0x02U) != 0U;
        bool stream_classified = false;
        bool server_push_stream = false;
        for (;;)
        {
            auto received = co_await connection->async_recv(stream,
                wire.prepare(stream_classified ? 16384U : 1U));
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
            {
                if (server_push_stream)
                {
                    const auto pushed = co_await session->consume_server_push_stream(
                        stream, wire.readable_view());
                    if (!pushed)
                    {
                        co_await connection->async_close(pushed.error(),
                            "invalid HTTP/3 server push stream");
                    }
                }
                co_return;
            }
            wire.commit(*received);
            if (!stream_classified)
            {
                const auto routed = session->route_webtransport_stream(
                    stream, wire.readable_view(), unidirectional);
                if (!routed)
                {
                    if (routed.error() == std::make_error_code(std::errc::message_size))
                        continue;
                    co_await connection->async_close(routed.error(),
                        "invalid WebTransport stream preface");
                    co_return;
                }
                if (*routed)
                    co_return;
                stream_classified = true;
                if (!unidirectional)
                {
                    co_await connection->async_close(
                        std::make_error_code(std::errc::protocol_error),
                        "unexpected server-initiated bidirectional HTTP/3 stream");
                    co_return;
                }
            }
            if (!unidirectional)
                co_return;
            if (!server_push_stream)
            {
                const auto type = quic::decode_varint(wire.readable_view());
                if (!type)
                {
                    if (type.error() == std::make_error_code(std::errc::bad_message))
                        continue;
                    co_await connection->async_close(type.error(),
                        "invalid HTTP/3 unidirectional stream type");
                    co_return;
                }
                if (type->first == 0x01U)
                {
                    server_push_stream = true;
                    continue;
                }
            }
            if (server_push_stream)
                continue;
            // A peer commonly sends the one-byte QPACK stream type first and
            // its initial capacity instruction in a later QUIC frame. The
            // empty encoder/decoder stream is legal, but completing consumption
            // at the type byte would drop that later data.
            const auto bytes = wire.readable_view();
            if (bytes.size() == 1U &&
                (bytes.front() == std::byte{0x02} || bytes.front() == std::byte{0x03}))
                continue;
            auto processed = co_await session->process_peer_unidirectional_stream(stream, bytes);
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

    auto consume_peer_streams(std::shared_ptr<quic::quic_connection> connection,
        std::shared_ptr<http3_client_session> session) -> task<void>
    {

        while (!connection->is_closed())
        {
            auto stream = co_await connection->async_accept_stream();
            if (!stream)
                co_return;

            spawn(connection->context(),
                consume_peer_stream(connection, session, *stream));
        }
    }
} // namespace detail

http3_client::http3_client(io_context& c, ssl_context& t, http3_client_options o) : ctx_(c), tls_(t), options_(std::move(o)) {}

auto http3_client::connect(std::string_view host, std::uint16_t port,
    std::string_view origin_host, std::uint16_t origin_port)
    -> task<std::expected<void, std::error_code>>
{
    co_await connect_mutex_.lock();
    cnetmod::async_lock_guard connect_guard{connect_mutex_, std::adopt_lock};
    if (host.empty() || port == 0U)
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    const auto authority_host = origin_host.empty() ? host : origin_host;
    const auto authority_port = origin_port == 0U ? port : origin_port;
    co_await lifecycle_mutex_.lock();
    cnetmod::async_lock_guard lifecycle_guard{lifecycle_mutex_, std::adopt_lock};
    auto generation = lifecycle_generation_;
    const bool reusable = session_ && session_->accepting_requests() &&
        authority_host == origin_host_ && authority_port == origin_port_;
    const bool has_connection = static_cast<bool>(connection_);
    lifecycle_guard.release();
    lifecycle_mutex_.unlock();
    if (reusable)
        co_return {};
    early_data_attempted_ = false;
    if (has_connection)
    {
        co_await close();
        co_await lifecycle_mutex_.lock();
        cnetmod::async_lock_guard refreshed_guard{lifecycle_mutex_, std::adopt_lock};
        generation = lifecycle_generation_;
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
    config.max_datagram_frame_size = options_.max_datagram_frame_size;
    config.enable_path_mtu_discovery = options_.enable_path_mtu_discovery;
    config.max_path_mtu = options_.max_path_mtu;
    config.path_mtu_probe_interval = options_.path_mtu_probe_interval;
    config.path_mtu_initial_probe_delay = options_.path_mtu_initial_probe_delay;
    config.multipath_initial_max_path_id = options_.multipath_initial_max_path_id;
    config.server_name = options_.tls_sni_host.empty() ? std::string(host) : options_.tls_sni_host;
    tls_.set_verify_peer(options_.verify_certificate);
    tls_.configure_alpn_client({"h3"});
    // Keep an operation-local owner.  close() is allowed to clear the public
    // handle while this coroutine is awaiting the handshake; the local owner
    // keeps the transport valid until this attempt returns.
    auto connection = std::make_shared<quic::quic_connection>(ctx_, std::move(socket), endpoint{address, port}, quic::quic_role::client, tls_, config);
    const bool requested_early_data = options_.enable_early_data &&
        options_.resumption_ticket && !options_.resumption_ticket->empty();
    if (options_.resumption_ticket)
    {
        const auto ticket = connection->set_resumption_ticket(
            *options_.resumption_ticket);
        if (!ticket)
        {
            co_return std::unexpected(ticket.error());
        }
    }
    if (requested_early_data)
    {
        const auto early_data = connection->enable_early_data();
        if (!early_data)
        {
            co_return std::unexpected(early_data.error());
        }
    }
    early_data_attempted_ = requested_early_data;
    // A ticket is single-use for one handshake. Do not accidentally offer it
    // again if this client reconnects after a failed or closed session.
    options_.resumption_ticket.reset();
    auto driver_completion = std::make_shared<channel<std::monostate>>(1);
    auto driver_close_requested = std::make_shared<std::atomic_bool>(false);
    // close() can complete while DNS is outstanding.  Do not publish a new
    // UDP socket after that linearization point.
    co_await lifecycle_mutex_.lock();
    cnetmod::async_lock_guard publish_guard{lifecycle_mutex_, std::adopt_lock};
    if (lifecycle_generation_ != generation)
        co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
    connection_ = connection;
    driver_completion_ = driver_completion;
    driver_close_requested_ = driver_close_requested;
    driver_joined_ = false;
    publish_guard.release();
    lifecycle_mutex_.unlock();
    spawn(ctx_, detail::drive_connection(connection, std::move(driver_completion), std::move(driver_close_requested)));
    const auto deadline = std::chrono::steady_clock::now() + options_.connect_timeout;
    // With an explicitly supplied ticket, expose the session as soon as the
    // driver has started. HTTP/3 control/QPACK/request streams can then be
    // queued into QUIC 0-RTT. Without a ticket we retain the established
    // handshake-complete connect() contract.
    if (requested_early_data)
    {
        // The driver first creates and sends the Initial flight, then BoringSSL
        // exposes the 0-RTT write secret. Opening HTTP/3 control streams in
        // the small gap between those two steps can enqueue application frames
        // without a valid packet protection level and sporadically tear the
        // connection down as a protocol error. Wait for the actual key, not
        // just a non-idle state transition.
        while (!connection->early_data_write_ready() &&
            connection->state() != quic::connection_state::connected &&
            connection->early_data_status() != quic::early_data_state::rejected)
        {
            if (connection->is_closed() ||
                connection->state() == quic::connection_state::closing ||
                connection->state() == quic::connection_state::draining)
                break;
            if (std::chrono::steady_clock::now() >= deadline)
            {
                co_await connection->async_close(std::make_error_code(std::errc::timed_out), "HTTP/3 QUIC handshake timed out");
                co_await close();
                co_return std::unexpected(std::make_error_code(std::errc::timed_out));
            }
            co_await async_sleep(ctx_, std::chrono::milliseconds{1});
        }
    }
    else
    {
        while (connection->state() == quic::connection_state::idle || connection->state() == quic::connection_state::handshaking)
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                co_await connection->async_close(std::make_error_code(std::errc::timed_out), "HTTP/3 QUIC handshake timed out");
                co_await close();
                co_return std::unexpected(std::make_error_code(std::errc::timed_out));
            }
            co_await async_sleep(ctx_, std::chrono::milliseconds{5});
        }
    }
    if (connection->state() == quic::connection_state::closed ||
        connection->state() == quic::connection_state::closing ||
        connection->state() == quic::connection_state::draining ||
        (connection->state() != quic::connection_state::connected &&
            !requested_early_data))
    {
        co_await close();
        co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
    }

    auto session = std::shared_ptr<http3_client_session>{
        make_http3_client_session(*connection, {})};
    session->configure_local_settings({options_.h3_max_header_list_size,
        options_.h3_qpack_max_table_capacity, options_.h3_qpack_blocked_streams,
        options_.max_datagram_frame_size != 0U, options_.max_datagram_frame_size != 0U,
        options_.max_datagram_frame_size != 0U, options_.max_datagram_frame_size != 0U ? 1U : 0U});
    session->configure_server_push(options_.max_push_id,
        options_.on_server_push, options_.on_server_push_promise);
    // RFC 9114 requires the client control stream and the two QPACK
    // unidirectional streams to exist before application request streams are
    // admitted.  Deferring this until send_request() leaves a scheduling gap:
    // the transport driver can observe a peer close between connect() returning
    // and async_open_stream(), which is reported as a misleading ENOTCONN.
    // Establish the HTTP/3 session while the successful QUIC handshake is
    // still owned by this coroutine and propagate any stream-open failure from
    // connect() itself.
    auto initialized = co_await session->connect();

    if (!initialized)
    {
        co_await connection->async_close(initialized.error(),
            "HTTP/3 control-stream initialization failed");
        co_await close();
        co_return std::unexpected(initialized.error());
    }
    // The server may reject a replayed ticket while the mandatory HTTP/3
    // control streams are being opened. In that case no application request
    // has been submitted yet, so reconnect immediately without the consumed
    // ticket instead of returning a session that cannot make progress.
    if (requested_early_data &&
        connection->early_data_status() == quic::early_data_state::rejected)
    {
        co_await connection->async_close({}, "HTTP/3 0-RTT rejected before request");
        co_await close();
        connect_guard.release();
        connect_mutex_.unlock();
        co_return co_await connect(host, port, authority_host, authority_port);
    }
    spawn(ctx_, detail::consume_peer_streams(connection, session));
    // For a fresh 1-RTT connection, peer SETTINGS are part of connection
    // setup: they authorize dynamic QPACK capacity.  Waiting here gives the
    // encoder one serialized state transition before any request coroutine
    // can submit headers.  0-RTT deliberately retains its fast path because
    // its replay-safe request is allowed to be queued before peer SETTINGS.
    if (!requested_early_data)
    {

        while (!session->peer_settings_received())
        {
            if (connection->is_closed() ||
                std::chrono::steady_clock::now() >= deadline)
            {
                const auto error = connection->is_closed()
                    ? std::make_error_code(std::errc::connection_aborted)
                    : std::make_error_code(std::errc::timed_out);
                co_await connection->async_close(error,
                    "HTTP/3 peer SETTINGS timed out");
                co_await close();
                co_return std::unexpected(error);
            }
            co_await async_sleep(ctx_, std::chrono::milliseconds{1});
        }
    }
    // Publish only after session setup has completed.  A concurrent close()
    // may already have removed this connection from the client, in which case
    // this connect attempt must not resurrect a closed session.
    co_await lifecycle_mutex_.lock();
    cnetmod::async_lock_guard session_publish_guard{lifecycle_mutex_, std::adopt_lock};
    if (lifecycle_generation_ != generation || connection_ != connection ||
        connection->is_closed())
        co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
    host_ = host;
    port_ = port;
    origin_host_ = authority_host;
    origin_port_ = authority_port;
    session_ = std::move(session);
    co_return {};
}

auto http3_client::send_request(const http3_request& r) -> task<std::expected<http3_response, std::error_code>>
{
    auto session = session_;
    auto connection = connection_;
    if (!session || !connection)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    // A QUIC connection is pooled per origin.  Do not silently coalesce an
    // authority without proving certificate and origin-set eligibility.
    if ((!r.host.empty() && r.host != origin_host_) ||
        (r.port != 0U && r.port != origin_port_))
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    if (connection->early_data_status() == quic::early_data_state::rejected)
    {
        const auto peer_host = host_;
        const auto peer_port = port_;
        const auto origin = origin_host_;
        const auto origin_port = origin_port_;
        co_await close();
        auto reconnected = co_await connect(peer_host, peer_port, origin, origin_port);
        if (!reconnected)
            co_return std::unexpected(reconnected.error());
        session = session_;
        connection = connection_;
        if (!session || !connection)
            co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
    }
    if (connection->early_data_status() == quic::early_data_state::pending &&
        !detail::is_replay_safe(r.method))
    {
        // Never put a non-idempotent request into 0-RTT. The ticket is
        // single-use, so reconnecting here gives the request a clean 1-RTT
        // connection without risking an application-level replay.
        const auto peer_host = host_;
        const auto peer_port = port_;
        const auto origin = origin_host_;
        const auto origin_port = origin_port_;
        co_await close();
        auto reconnected = co_await connect(peer_host, peer_port, origin, origin_port);
        if (!reconnected)
            co_return std::unexpected(reconnected.error());
        session = session_;
        connection = connection_;
        if (!session || !connection)
            co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
    }

    if (!session->accepting_requests())
    {

        if (!options_.retry_idempotent_requests || !detail::is_replay_safe(r.method))
            co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
        const auto peer_host = host_;
        const auto peer_port = port_;
        const auto origin = origin_host_;
        const auto origin_port = origin_port_;
        co_await close();
        auto reconnected = co_await connect(peer_host, peer_port, origin, origin_port);
        if (!reconnected)
            co_return std::unexpected(reconnected.error());
        session = session_;
        connection = connection_;
        if (!session || !connection)
            co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
    }
    // Consume the replay allowance before submitting the first request. The
    // TLS outcome may change from pending to rejected between the checks
    // above and this point; tying retry eligibility to that transient state
    // loses the safe 1-RTT fallback. Later requests are ordinary traffic and
    // must retain normal connection-error semantics.
    const bool replay_early_request = std::exchange(early_data_attempted_, false);
    auto result = co_await session->send_request(r);
    if (!result && replay_early_request &&
        options_.retry_idempotent_requests && detail::is_replay_safe(r.method))
    {
        // The transport discards rejected 0-RTT bytes and wakes the affected
        // stream. Reconnect without the single-use ticket, then replay only
        // this explicitly safe method at 1-RTT.
        const auto peer_host = host_;
        const auto peer_port = port_;
        const auto origin = origin_host_;
        const auto origin_port = origin_port_;
        co_await close();
        auto reconnected = co_await connect(peer_host, peer_port, origin, origin_port);
        if (reconnected)
        {
            session = session_;
            connection = connection_;
            if (session && connection)
                result = co_await session->send_request(r);
        }
    }
    co_return result;
}

auto http3_client::send_request(const http3_request& r,
    cnetmod::cancel_token& token) -> task<std::expected<http3_response, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
    auto session = session_;
    auto connection = connection_;
    if (!session || !connection)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    // A QUIC connection is pooled per origin.  Do not silently coalesce an
    // authority without proving certificate and origin-set eligibility.
    if ((!r.host.empty() && r.host != origin_host_) ||
        (r.port != 0U && r.port != origin_port_))
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    if (connection->early_data_status() == quic::early_data_state::rejected)
    {
        const auto peer_host = host_;
        const auto peer_port = port_;
        const auto origin = origin_host_;
        const auto origin_port = origin_port_;
        co_await close();
        auto reconnected = co_await connect(peer_host, peer_port, origin, origin_port);
        if (!reconnected)
            co_return std::unexpected(reconnected.error());
        session = session_;
        connection = connection_;
        if (!session || !connection)
            co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
    }
    if (connection->early_data_status() == quic::early_data_state::pending &&
        !detail::is_replay_safe(r.method))
    {
        const auto peer_host = host_;
        const auto peer_port = port_;
        const auto origin = origin_host_;
        const auto origin_port = origin_port_;
        co_await close();
        auto reconnected = co_await connect(peer_host, peer_port, origin, origin_port);
        if (!reconnected)
            co_return std::unexpected(reconnected.error());
        session = session_;
        connection = connection_;
        if (!session || !connection)
            co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
    }
    if (!session->accepting_requests())
    {

        if (!options_.retry_idempotent_requests || !detail::is_replay_safe(r.method))
            co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
        const auto peer_host = host_;
        const auto peer_port = port_;
        const auto origin = origin_host_;
        const auto origin_port = origin_port_;
        co_await close();
        auto reconnected = co_await connect(peer_host, peer_port, origin, origin_port);
        if (!reconnected)
            co_return std::unexpected(reconnected.error());
        session = session_;
        connection = connection_;
        if (!session || !connection)
            co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
    }
    // See the non-cancellable overload: retry eligibility belongs to the
    // first request submitted by an early-data connection, not to a racy
    // snapshot of BoringSSL's handshake state.
    const bool replay_early_request = std::exchange(early_data_attempted_, false);
    auto result = co_await session->send_request(r, token);
    if (!result && replay_early_request &&
        options_.retry_idempotent_requests && detail::is_replay_safe(r.method) &&
        !token.is_cancelled())
    {
        const auto peer_host = host_;
        const auto peer_port = port_;
        const auto origin = origin_host_;
        const auto origin_port = origin_port_;
        co_await close();
        auto reconnected = co_await connect(peer_host, peer_port, origin, origin_port);
        if (reconnected)
        {
            session = session_;
            connection = connection_;
            if (session && connection)
                result = co_await session->send_request(r, token);
        }
    }
    co_return result;
}

auto http3_client::send_request_streaming(const http3_request& r,
    streaming_client_response_handler handler)
    -> task<std::expected<http3_response, std::error_code>>
{
    cnetmod::cancel_token token;
    co_return co_await send_request_streaming(r, std::move(handler), token);
}

auto http3_client::send_request_streaming(const http3_request& r,
    streaming_client_response_handler handler, cnetmod::cancel_token& token)
    -> task<std::expected<http3_response, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
    auto session = session_;
    auto connection = connection_;
    if (!session || !connection)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    if ((!r.host.empty() && r.host != origin_host_) ||
        (r.port != 0U && r.port != origin_port_))
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    if (connection->early_data_status() == quic::early_data_state::pending &&
        !detail::is_replay_safe(r.method))
    {
        // Response streaming does not make a request replay-safe.  Complete
        // the handshake first so a POST/PUT callback cannot observe a result
        // for an operation that a peer may replay.
        const auto peer_host = host_;
        const auto peer_port = port_;
        const auto origin = origin_host_;
        const auto origin_port = origin_port_;
        co_await close();
        auto reconnected = co_await connect(peer_host, peer_port, origin, origin_port);
        if (!reconnected)
            co_return std::unexpected(reconnected.error());
        session = session_;
        connection = connection_;
        if (!session || !connection)
            co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
    }
    if (!session->accepting_requests())
    {
        if (!options_.retry_idempotent_requests || !detail::is_replay_safe(r.method))
            co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
        const auto peer_host = host_;
        const auto peer_port = port_;
        const auto origin = origin_host_;
        const auto origin_port = origin_port_;
        co_await close();
        auto reconnected = co_await connect(peer_host, peer_port, origin, origin_port);
        if (!reconnected)
            co_return std::unexpected(reconnected.error());
        session = session_;
        connection = connection_;
        if (!session || !connection)
            co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
    }
    auto result = co_await session->send_request_streaming(r, handler, token);
    // Do not replay a streaming callback after a rejected 0-RTT offer: the
    // callback may already have delivered a prefix to application code.  The
    // normal complete-body API retains its existing idempotent retry policy.
    co_return result;
}

auto http3_client::connect_webtransport(const http3_request& request)
    -> task<std::expected<webtransport_session, std::error_code>>
{
    auto session = session_;
    auto connection = connection_;
    if (!session || !connection)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    if ((!request.host.empty() && request.host != origin_host_) ||
        (request.port != 0U && request.port != origin_port_))
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    if (connection->early_data_status() == quic::early_data_state::pending)
    {
        const auto peer_host = host_;
        const auto peer_port = port_;
        const auto origin = origin_host_;
        const auto origin_port = origin_port_;
        co_await close();
        auto reconnected = co_await connect(peer_host, peer_port, origin, origin_port);
        if (!reconnected)
            co_return std::unexpected(reconnected.error());
        session = session_;
        connection = connection_;
        if (!session || !connection)
            co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
    }
    co_return co_await session->connect_webtransport(request);
}

auto http3_client::cancel_server_push(std::uint64_t push_id)
    -> task<std::expected<void, std::error_code>>
{
    auto session = session_;
    auto connection = connection_;
    if (!session || !connection)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    co_return co_await session->cancel_server_push(push_id);
}

auto http3_client::async_probe_path(std::uint32_t path_id, endpoint peer)
    -> task<std::expected<void, std::error_code>>
{
    auto connection = connection_;
    auto session = session_;
    if (!connection || !session)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    // Keep the transport alive across the suspension point. close() clears
    // the client's owning member after it joins its drivers, while a caller's
    // in-flight PATH_CHALLENGE still needs the connection state to observe
    // that cancellation and return safely.
    co_return co_await connection->async_probe_path(path_id, std::move(peer));
}

auto http3_client::async_probe_path(std::uint32_t path_id, endpoint peer,
    endpoint local_endpoint) -> task<std::expected<void, std::error_code>>
{
    // An independently-bound receiver and an in-flight probe must retain the
    // same transport ownership until both observe close/cancellation.
    auto connection = connection_;
    auto session = session_;
    if (!connection || !session)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));

    std::shared_ptr<detail::local_path_receiver> receiver;
    // Publishing the receiver and starting its driver is a single lifecycle
    // transition. A probe may return operation_in_progress until the peer's
    // PATH_NEW_CONNECTION_ID arrives; retain and reuse its bound socket for
    // that explicit retry instead of rejecting every later attempt because a
    // receiver already exists.
    co_await lifecycle_mutex_.lock();
    cnetmod::async_lock_guard lifecycle_guard{lifecycle_mutex_, std::adopt_lock};
    if (connection_ != connection || session_ != session || connection->is_closed())
        co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
    if (const auto existing = local_path_receivers_.find(path_id);
        existing != local_path_receivers_.end())
    {
        receiver = existing->second;
    }
    else
    {
        auto socket = std::make_shared<udp::udp_socket>(ctx_);
        const auto opened = socket->open(local_endpoint);
        if (!opened)
            co_return std::unexpected(opened.error());
        receiver = std::make_shared<detail::local_path_receiver>();
        receiver->socket = std::move(socket);
        local_path_receivers_.emplace(path_id, receiver);
        spawn(ctx_, detail::drive_local_path_receiver(ctx_, connection, receiver));
    }
    lifecycle_guard.release();
    lifecycle_mutex_.unlock();

    const auto validated = co_await connection->async_probe_path(path_id,
        std::move(peer), *receiver->socket);
    // Retain the receiver even after an unsuccessful validation: the core
    // connection borrows this socket for the Path ID, and retaining it keeps
    // a later explicit retry memory-safe. close() joins and releases it.
    co_return validated;
}

auto http3_client::async_abandon_path(std::uint32_t path_id,
    std::uint64_t error_code) -> task<std::expected<void, std::error_code>>
{
    auto connection = connection_;
    auto session = session_;
    if (!connection || !session)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    co_return co_await connection->async_abandon_path(path_id, error_code);
}

auto http3_client::set_path_backup(std::uint32_t path_id, bool backup)
    -> std::expected<void, std::error_code>
{
    if (!connection_ || !session_)
        return std::unexpected(std::make_error_code(std::errc::not_connected));
    return connection_->set_path_backup(path_id, backup);
}

auto http3_client::discovered_path_mtu(std::uint32_t path_id) const
    -> std::optional<std::size_t>
{
    return connection_ ? connection_->discovered_path_mtu(path_id) : std::nullopt;
}

auto http3_client::path_is_validated(std::uint32_t path_id) const noexcept -> bool
{
    return connection_ && connection_->path_is_validated(path_id);
}

auto http3_client::local_path_endpoint(std::uint32_t path_id)
    -> std::expected<endpoint, std::error_code>
{
    if (!connection_)
        return std::unexpected(std::make_error_code(std::errc::not_connected));
    return connection_->local_path_endpoint(path_id);
}

auto http3_client::send_request(const http3_request& r,
    cnetmod::deadline request_deadline)
    -> task<std::expected<http3_response, std::error_code>>
{
    co_return co_await cnetmod::with_deadline(ctx_, request_deadline,
        [&](cnetmod::cancel_token& token)
        {
            return send_request(r, token);
        });
}

auto http3_client::close() -> task<void>
{
    co_await lifecycle_mutex_.lock();
    cnetmod::async_lock_guard guard{lifecycle_mutex_, std::adopt_lock};
    ++lifecycle_generation_;
    if (driver_close_requested_)
        driver_close_requested_->store(true, std::memory_order_release);
    if (connection_)
        co_await connection_->async_close({}, "HTTP/3 client closed");
    co_await wait_for_connection_driver();
    co_await stop_local_path_receivers();
    session_.reset();
    connection_.reset();
    driver_close_requested_.reset();
    local_path_receivers_.clear();
    early_data_attempted_ = false;
    co_return;
}

auto http3_client::wait_for_connection_driver() -> task<void>
{
    co_await driver_join_mutex_.lock();
    cnetmod::async_lock_guard join_guard{driver_join_mutex_, std::adopt_lock};
    if (driver_joined_ || !driver_completion_)
        co_return;
    auto completion = driver_completion_;
    (void)co_await completion->receive();
    driver_joined_ = true;
    if (driver_completion_ == completion)
        driver_completion_.reset();
}

auto http3_client::stop_local_path_receivers() -> task<void>
{
    for (auto& [_, receiver] : local_path_receivers_)
    {
        receiver->cancellation.cancel();
        receiver->socket->close();
    }
    for (auto& [_, receiver] : local_path_receivers_)
        (void)co_await receiver->completion->receive();
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
    return session_ && session_->accepting_requests() && host == origin_host_ && port == origin_port_;
}

auto http3_client::take_resumption_ticket()
    -> std::expected<quic::session_ticket, std::error_code>
{
    if (!connection_)
        return std::unexpected(std::make_error_code(std::errc::not_connected));
    return connection_->take_resumption_ticket();
}

auto http3_client::early_data_status() const noexcept -> quic::early_data_state
{
    if (!connection_)
        return quic::early_data_state::disabled;
    return connection_->early_data_status();
}
} // namespace cnetmod::http::v3
