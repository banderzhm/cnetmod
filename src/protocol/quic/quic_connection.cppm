module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_SSL

    #ifdef CNETMOD_ENABLE_QUIC

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

    // =========================================================================
    // Public API - Stream Operations
    // =========================================================================

    /// Send data on a stream (creates stream if needed for client-initiated)
    [[nodiscard]] auto async_send(
        stream_id sid,
        std::span<const std::byte> data,
        bool fin = false)
        -> task<std::expected<void, std::error_code>>;

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
        const endpoint& destination)
        -> task<std::expected<std::size_t, std::error_code>>;

    /// Flow control helpers
    [[nodiscard]] auto can_write_to_stream(stream_id sid) noexcept -> bool;
    /// Send queued frames
    auto flush_send_queue() -> task<void>;
    /// Serialize application packets according to the controller's current
    /// pacing rate.  Control/path-validation packets are not delayed here.
    auto await_application_pacing(std::size_t packet_size) -> task<void>;
};

} // namespace cnetmod::quic

    #endif // CNETMOD_ENABLE_QUIC
#endif     // CNETMOD_HAS_SSL
