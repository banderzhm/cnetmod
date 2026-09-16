module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.quic:connection;

import std;

import cnetmod.core.ssl;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.coro.cancel;
import cnetmod.coro.channel;
import cnetmod.coro.mutex;
import cnetmod.coro.shared_mutex;
import cnetmod.protocol.udp;
import :types;
import :frame;
import :packet;
import :crypto;
import :varint;
import :loss_detection;
import :congestion_control;

namespace cnetmod::quic {

// =============================================================================
// Connection States (RFC 9000 §10)
// =============================================================================

export enum class connection_state
{
    idle,        // No activity
    handshaking, // TLS handshake in progress
    connected,   // Handshake complete, data transfer
    closing,     // Closing initiated locally
    draining,    // Draining in progress (no new connections)
    closed       // Closed completely
};

/// Weighted RFC 9218 service budget used by the connection-local stream
/// scheduler. Urgency 0 keeps its first-packet preference, while every
/// non-empty lower-priority class receives service in the same bounded epoch.
/// It contains no synchronization: one QUIC connection owns it through its
/// serialized protocol execution domain.
export class priority_service_budget
{
public:
    static constexpr std::array<std::uint8_t, 8> quantum{
        16U, 8U, 4U, 2U, 1U, 1U, 1U, 1U};

    [[nodiscard]] auto take_next(const std::array<bool, 8>& ready)
        -> std::optional<std::uint8_t>
    {
        for (unsigned epoch{}; epoch < 2U; ++epoch)
        {
            for (std::uint8_t urgency{}; urgency < credits_.size(); ++urgency)
            {
                if (ready[urgency] && credits_[urgency] != 0U)
                {
                    --credits_[urgency];
                    return urgency;
                }
            }
            if (!std::ranges::any_of(ready, [](bool value)
                    {
                        return value;
                    }))
                return std::nullopt;
            credits_ = quantum;
        }
        return std::nullopt;
    }

    /// Return a reservation when packet construction did not consume it.
    void restore(std::uint8_t urgency) noexcept
    {
        urgency = std::min<std::uint8_t>(urgency, 7U);
        credits_[urgency] = std::min<std::uint8_t>(quantum[urgency],
            static_cast<std::uint8_t>(credits_[urgency] + 1U));
    }

    void reset() noexcept
    {
        credits_ = quantum;
    }

private:
    std::array<std::uint8_t, 8> credits_{quantum};
};

// =============================================================================
// QUIC Connection - Main async coordinator
// =============================================================================

export class quic_connection
{
public:
    /// Constructor - creates UDP socket internally or uses provided one
    explicit quic_connection(
        io_context& ctx,
        udp::udp_socket&& sock,
        endpoint peer,
        quic_role role,
        quic_config config = {});

    /// Construct with an application-owned TLS context.  Servers must use
    /// this overload so their certificate, private key and ALPN policy are
    /// retained by the QUIC TLS session.
    quic_connection(
        io_context& ctx,
        udp::udp_socket&& sock,
        endpoint peer,
        quic_role role,
        ssl_context& tls_context,
        quic_config config = {});

    /// Server-side construction over a listener-owned UDP socket.  The
    /// connection never closes the borrowed socket.
    quic_connection(io_context& ctx, udp::udp_socket& shared_socket,
        endpoint peer, quic_role role, ssl_context& tls_context,
        quic_config config = {});

    /// Server-side construction over a listener-owned UDP socket whose I/O
    /// completion context differs from the connection's protocol executor.
    /// Datagram completions are returned to `ctx` before connection state is
    /// touched, preserving per-worker affinity on IOCP.
    quic_connection(io_context& ctx, io_context& socket_context,
        udp::udp_socket& shared_socket, endpoint peer, quic_role role,
        ssl_context& tls_context, quic_config config = {});

    ~quic_connection();

    /// Cannot copy/move
    quic_connection(const quic_connection&) = delete;
    quic_connection& operator=(const quic_connection&) = delete;

    /// Start QUIC handshake / handle incoming Initial
    [[nodiscard]] auto run() -> task<std::expected<void, std::error_code>>;

    /// Feed an already-demultiplexed UDP datagram into this connection.  A
    /// listener owning a shared UDP socket uses this instead of calling run()
    /// per connection; the sender endpoint is retained for path validation.
    [[nodiscard]] auto process_datagram(std::span<const std::byte> datagram,
        const endpoint& sender) -> task<std::expected<void, std::error_code>>;

    /// Advance PTO, idle and draining timers for a connection driven by a
    /// shared listener socket. Dedicated-socket connections do this in run().
    [[nodiscard]] auto async_poll_timers() -> task<void>;

    /// Earliest listener-owned timer deadline. `nullopt` means the shared
    /// listener has no timer work for this connection until another packet or
    /// application write changes its state.
    [[nodiscard]] auto next_timer_deadline() const
        -> std::optional<std::chrono::steady_clock::time_point>;

    // =========================================================================
    // Public API - Stream Operations
    // =========================================================================

    /// Send data on a stream (creates stream if needed for client-initiated)
    [[nodiscard]] auto async_send(
        stream_id sid,
        std::span<const std::byte> data,
        bool fin = false)
        -> task<std::expected<void, std::error_code>>;

    /// Transfers an already-owned wire buffer into the packet-owner queue
    /// without a second payload copy.
    [[nodiscard]] auto async_send(
        stream_id sid,
        std::vector<std::byte>&& data,
        bool fin = false)
        -> task<std::expected<void, std::error_code>>;

    /// Update the RFC 9218 scheduling policy for subsequent data on one
    /// stream. Calls are serialized by the connection's I/O domain; urgency
    /// is clamped to the protocol range [0, 7].
    /// Returns false only when the bounded packet-owner command queue is
    /// saturated; callers may retry without racing QUIC stream state.
    [[nodiscard]] auto set_stream_priority(stream_id sid, std::uint8_t urgency,
        bool incremental) noexcept -> bool;

    /// Receive data from a stream
    [[nodiscard]] auto async_recv(
        stream_id sid,
        mutable_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>;

    /// Wait until a stream receives additional contiguous data, reaches its
    /// remote FIN/reset, or the connection closes.  `async_recv` deliberately
    /// remains non-blocking so callers that need polling retain that option;
    /// coroutine consumers should await this operation after would_block.
    [[nodiscard]] auto async_wait_readable(stream_id sid)
        -> task<std::expected<void, std::error_code>>;

    /// Send one unreliable RFC 9221 QUIC DATAGRAM. Both endpoints must have
    /// advertised a non-zero `max_datagram_frame_size`; congestion control
    /// still applies, but loss is intentionally never retransmitted.
    [[nodiscard]] auto async_send_datagram(std::span<const std::byte> data)
        -> task<std::expected<void, std::error_code>>;

    /// Await the next peer DATAGRAM. A full receive queue drops new datagrams
    /// rather than stalling packet processing, as required for unreliable data.
    [[nodiscard]] auto async_receive_datagram()
        -> task<std::expected<std::vector<std::byte>, std::error_code>>;

    /// Cancellable stream-readiness wait. Cancellation affects only this
    /// waiter; use async_cancel_stream to notify the peer as well.
    [[nodiscard]] auto async_wait_readable(stream_id sid, cancel_token& token)
        -> task<std::expected<void, std::error_code>>;

    /// Abort one stream without closing the multiplexed QUIC connection.
    /// Sends RESET_STREAM and STOP_SENDING with the application error code.
    [[nodiscard]] auto async_cancel_stream(stream_id sid,
        std::uint64_t application_error_code = 0x010c)
        -> task<std::expected<void, std::error_code>>;

    /**
     * Binds an application cancellation token to one peer stream.
     *
     * A remote RESET_STREAM or STOP_SENDING cancels the token on the
     * connection's I/O context, including when cancellation arrived before
     * registration. Register only for the dynamic lifetime of a request
     * handler.
     */
    void register_stream_cancellation(stream_id sid, cancel_token& token) noexcept;
    void unregister_stream_cancellation(stream_id sid) noexcept;

    /// Open a new stream
    [[nodiscard]] auto async_open_stream(
        bool bidirectional = true)
        -> task<std::expected<stream_id, std::error_code>>;

    /// Wait for the next stream opened by the peer. The returned identifier
    /// is immediately usable with async_recv/async_send (where its direction
    /// permits it). A closed connection returns not_connected.
    [[nodiscard]] auto async_accept_stream()
        -> task<std::expected<stream_id, std::error_code>>;

    /// Release the buffers and readiness state of a fully closed stream while
    /// retaining the final sizes needed to validate delayed duplicate frames.
    [[nodiscard]] auto retire_stream(stream_id sid)
        -> std::expected<void, std::error_code>;

    /// Install a serialized TLS 1.3 ticket before `run()`. Client only.
    [[nodiscard]] auto set_resumption_ticket(const session_ticket& ticket)
        -> std::expected<void, std::error_code>;

    /// Opt into replay-safe client 0-RTT before `run()`. Server 0-RTT remains
    /// disabled unless its TLS ticket implementation provides anti-replay
    /// enforcement; an early-data context alone is not replay protection.
    [[nodiscard]] auto enable_early_data() -> std::expected<void, std::error_code>;

    [[nodiscard]] auto take_resumption_ticket()
        -> std::expected<session_ticket, std::error_code>;

    [[nodiscard]] auto early_data_status() const noexcept -> early_data_state;

    /// True only after TLS has installed the client 0-RTT write secret. A
    /// caller may queue replay-safe bytes once this is true; merely enabling
    /// early data is not sufficient because the Initial flight is still being
    /// created on the transport driver.
    [[nodiscard]] auto early_data_write_ready() const noexcept -> bool;

    /// Request an RFC 9001 1-RTT key update.  The new write generation is
    /// used by the next short-header packet; handshake keys are unaffected.
    [[nodiscard]] auto initiate_key_update() -> std::expected<void, std::error_code>;

    /// Close connection with error
    [[nodiscard]] auto async_close(
        std::error_code ec,
        std::string_view reason = {})
        -> task<void>;

    /// Check if connection is closed
    [[nodiscard]] auto is_closed() const noexcept -> bool;

    /// Get current connection state
    [[nodiscard]] auto state() const noexcept -> connection_state;

    /// Start validation of an additional peer endpoint for a negotiated
    /// draft-ietf-quic-multipath-12 Path ID.  The caller supplies the remote
    /// endpoint; the connection keeps using its existing UDP socket, so this
    /// also works for NAT rebinding and multi-homed peers. The returned task
    /// succeeds only after a matching PATH_RESPONSE authenticates the tuple;
    /// it retries the same challenge for at most three PTOs. Application data
    /// is never scheduled on the path before that boundary.
    [[nodiscard]] auto async_probe_path(std::uint32_t path_id, endpoint peer)
        -> task<std::expected<void, std::error_code>>;

    /// Validate a path through an additional application-owned UDP socket.
    /// The caller must keep the socket alive and feed its received datagrams
    /// back through process_datagram() until the path is abandoned or the
    /// connection closes. This overload gives a Multipath path a distinct
    /// local address/port without creating an unowned receive coroutine.
    [[nodiscard]] auto async_probe_path(std::uint32_t path_id, endpoint peer,
        udp::udp_socket& local_socket)
        -> task<std::expected<void, std::error_code>>;

    /// Advertise the local scheduling preference for one negotiated path.
    /// A backup path remains validated and available for PTO/explicit use,
    /// but the automatic scheduler selects available paths first.
    [[nodiscard]] auto set_path_backup(std::uint32_t path_id, bool backup)
        -> std::expected<void, std::error_code>;

    /// Stop using one negotiated non-zero Path ID and notify the peer with
    /// PATH_ABANDON.  Path IDs are never reused; remaining validated paths
    /// continue to carry the connection.
    [[nodiscard]] auto async_abandon_path(std::uint32_t path_id,
        std::uint64_t error_code = 0)
        -> task<std::expected<void, std::error_code>>;

    /// Current ACK-validated Datagram PLPMTUD ceiling for one path. A value
    /// of 1200 is the RFC 9000 baseline; larger values are published only
    /// after a padded PING probe has been acknowledged.
    [[nodiscard]] auto discovered_path_mtu(std::uint32_t path_id) const
        -> std::optional<std::size_t>;

    /// Whether PATH_RESPONSE has authenticated the peer tuple for this Path
    /// ID. Application scheduling never uses a false path.
    [[nodiscard]] auto path_is_validated(std::uint32_t path_id) const noexcept -> bool;

    /// Return the currently bound local UDP endpoint for one Path ID. A
    /// non-zero path can use an application-owned socket supplied to
    /// async_probe_path().
    [[nodiscard]] auto local_path_endpoint(std::uint32_t path_id)
        -> std::expected<endpoint, std::error_code>;

    /// Whether this endpoint advertised RFC 9221 DATAGRAM support locally.
    [[nodiscard]] auto datagrams_configured() const noexcept -> bool;

    /// Executor that owns this connection. Protocol sessions use it to
    /// service peer-opened streams independently.
    [[nodiscard]] auto context() noexcept -> io_context&;

    // =========================================================================
    // CID Management for Multiplexing
    // =========================================================================

    /// Register CID for this connection (for multiplexing support)
    [[nodiscard]] auto register_cid(connection_id cid)
        -> std::expected<void, std::error_code>;

    /// Unregister CID
    [[nodiscard]] auto unregister_cid(connection_id cid)
        -> task<void>;

    /// Get local CID
    [[nodiscard]] auto local_cid() const noexcept -> const connection_id*;

    /// Snapshot every currently routable local CID.  Shared-socket listeners
    /// use this after processing a datagram to refresh their demultiplex map.
    [[nodiscard]] auto local_cids() const -> std::vector<connection_id>;

    struct local_cid_route
    {
        connection_id cid;
        std::array<std::byte, 16> stateless_reset_token;
    };

    /// Snapshot every CID currently routable by a listener, including the
    /// stateless-reset token that was advertised for that CID.
    [[nodiscard]] auto local_cid_routes() const -> std::vector<local_cid_route>;

    /// Monotonically changes whenever the listener-visible local CID set
    /// changes. Shared-socket HTTP/3 listeners use it to avoid rebuilding
    /// their route table for every received UDP packet.
    [[nodiscard]] auto local_cid_route_generation() const noexcept -> std::uint64_t;

    /// Drain CIDs retired by the peer since the preceding call.  A shared
    /// listener retains their tokens for a bounded period so a delayed packet
    /// can receive a valid RFC 9000 stateless reset.
    [[nodiscard]] auto take_retired_local_cid_routes() -> std::vector<local_cid_route>;

    /// Server Retry context.  Must be supplied before processing the validated
    /// post-Retry Initial so later transport-parameter validation can retain
    /// the client's original destination CID.
    [[nodiscard]] auto set_original_destination_connection_id(connection_id cid)
        -> std::expected<void, std::error_code>;

    // =========================================================================
    // Accessors
    // =========================================================================

    /// Get underlying UDP socket
    [[nodiscard]] auto native_socket() -> udp::udp_socket&;

    /// Get peer endpoint
    [[nodiscard]] auto peer_endpoint() const noexcept -> const endpoint&;

    /// Get role
    [[nodiscard]] auto role() const noexcept -> quic_role;

private:
    struct received_datagram
    {
        std::span<const std::byte> bytes;
        endpoint sender;
    };

    /// Implementation Pimpl structure
    struct quic_connection_impl;

    std::unique_ptr<quic_connection_impl> impl_;

    // Private implementation methods
    [[nodiscard]] auto do_run() -> task<std::expected<void, std::error_code>>;

    /// Receive datagram from UDP socket
    [[nodiscard]] auto recv_datagram()
        -> task<std::expected<received_datagram, std::error_code>>;

    /// Process received packet
    [[nodiscard]] auto process_packet(
        std::span<const std::byte> packet, const endpoint& sender)
        -> task<std::expected<void, std::error_code>>;

    /// Handle long header packet
    [[nodiscard]] auto handle_long_header_packet(
        long_header hdr)
        -> task<std::expected<void, std::error_code>>;

    /// Handle short header packet
    [[nodiscard]] auto handle_short_header_packet(
        short_header hdr)
        -> task<std::expected<void, std::error_code>>;

    /// Process frames from packet
    [[nodiscard]] auto process_frames(const quic_frame_variant& frame)
        -> task<void>;

    /// Frame handlers
    [[nodiscard]] auto process_ack_frame(const ack_frame& frame) -> task<void>;
    [[nodiscard]] auto process_stream_frame(const stream_frame& frame) -> task<void>;
    [[nodiscard]] auto process_reset_stream_frame(const reset_stream_frame& frame) -> task<void>;
    [[nodiscard]] auto process_stop_sending_frame(const stop_sending_frame& frame) -> task<void>;
    [[nodiscard]] auto process_crypto_frame(const crypto_frame& frame) -> task<void>;
    [[nodiscard]] auto process_connection_close_frame(
        const connection_close_frame& frame) -> task<void>;
    [[nodiscard]] auto process_ping_frame(const ping_frame&) -> task<void>;
    [[nodiscard]] auto process_path_challenge_frame(
        const path_challenge_frame& frame) -> task<void>;
    [[nodiscard]] auto process_path_response_frame(
        const path_response_frame& frame) -> task<void>;
    [[nodiscard]] auto process_new_connection_id_frame(
        const new_connection_id_frame& frame) -> task<void>;
    [[nodiscard]] auto process_retire_connection_id_frame(
        const retire_connection_id_frame& frame) -> task<void>;
    [[nodiscard]] auto process_datagram_frame(const datagram_frame& frame) -> task<void>;

    [[nodiscard]] auto validate_peer_transport_parameters()
        -> std::expected<void, std::error_code>;

    /// Issue replacement/parallel CIDs only after the peer's authenticated
    /// transport parameters provide its active CID limit.
    [[nodiscard]] auto issue_parallel_local_connection_ids()
        -> std::expected<void, std::error_code>;

    /// Wake every pending stream-read waiter when the connection can no
    /// longer receive application data.
    auto close_stream_readiness() noexcept -> void;

    /// Pack and send packets
    [[nodiscard]] auto pack_and_send_packet() -> task<void>;
    /// Packet-owner fast path for the common single-path case. It batches only
    /// datagrams already admitted by the congestion controller and pacer.
    [[nodiscard]] auto pack_and_send_packets() -> task<void>;
    [[nodiscard]] auto pack_initial_packet() -> std::vector<std::byte>;
    [[nodiscard]] auto pack_zero_rtt_packet() -> std::vector<std::byte>;
    [[nodiscard]] auto pack_handshake_packet() -> std::vector<std::byte>;
    [[nodiscard]] auto pack_one_rtt_packet(bool pto_probe = false)
        -> std::vector<std::byte>;
    [[nodiscard]] auto pack_path_validation_packet(std::span<const std::byte> frame)
        -> std::vector<std::byte>;

    /// Timer management
    void schedule_idle_timeout();
    void handle_idle_timeout();
    [[nodiscard]] auto handle_pto() -> task<void>;

    [[nodiscard]] auto send_datagram(std::span<const std::byte> datagram,
        const endpoint& destination, udp::udp_socket* path_socket = nullptr)
        -> task<std::expected<std::size_t, std::error_code>>;
    [[nodiscard]] auto send_datagram_batch(
        std::span<const udp_send_datagram> datagrams)
        -> task<std::expected<std::size_t, std::error_code>>;

    [[nodiscard]] auto async_probe_path_impl(std::uint32_t path_id,
        endpoint peer, udp::udp_socket* path_socket)
        -> task<std::expected<void, std::error_code>>;

    /// Flow control helpers
    [[nodiscard]] auto can_write_to_stream(stream_id sid) noexcept -> bool;
    /// Executed only by the connection packet-owner after a producer command
    /// has been dequeued.  It is deliberately separate from async_send(), so
    /// callers never mutate stream or flow-control state directly.
    [[nodiscard]] auto apply_stream_write(stream_id sid, std::vector<std::byte> data,
        bool fin) -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto apply_application_datagram(std::vector<std::byte> data)
        -> task<std::expected<void, std::error_code>>;
    /// Send queued frames
    auto flush_send_queue() -> task<void>;
    /// Serialize application packets according to the controller's current
    /// pacing rate.  Control/path-validation packets are not delayed here.
    auto await_application_pacing(std::size_t packet_size) -> task<void>;
    /// Reserve immediately available pacing credit without sleeping. This is
    /// used only by packet batches; a false result leaves pacing state intact.
    [[nodiscard]] auto try_reserve_application_pacing(std::size_t packet_size)
        -> bool;
};

} // namespace cnetmod::quic
