module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http.v3.server;

import std;
import cnetmod.core.ssl;
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.coro.task;
import cnetmod.protocol.quic;
import cnetmod.protocol.http.v3.session;

namespace cnetmod::http::v3 {

/// Lock-free listener counters suitable for periodic metrics collection.
/// Values are snapshots and may change while they are being read.
export struct http3_server_statistics
{
    std::uint64_t inbox_enqueued_datagrams{};
    std::uint64_t inbox_dropped_datagrams{};
    std::size_t active_connection_inboxes{};
    /// Datagram count currently retained by listener inboxes.
    std::uint64_t inbox_queued_datagrams{};
    /// Peak queued datagrams since start; useful for sizing without guessing.
    std::uint64_t inbox_peak_queued_datagrams{};
    /// Drops caused by the global listener memory budget, distinct from a
    /// single connection's full queue.
    std::uint64_t inbox_budget_dropped_datagrams{};
    /// Drops caused by a specific connection's bounded queue being full.
    std::uint64_t inbox_capacity_dropped_datagrams{};
    /// Drops before an inbox could be created because the active-inbox budget
    /// was exhausted (usually an Initial-packet flood).
    std::uint64_t inbox_creation_dropped_datagrams{};
    /// Receive buffers handed to QUIC without a per-packet heap allocation.
    std::uint64_t pooled_receive_datagrams{};
    /// Oversized receive buffers that bypassed the bounded UDP pool.
    std::uint64_t heap_receive_datagrams{};
    /// Windows RIO completions; zero when RIO is unavailable or not selected.
    std::uint64_t rio_receive_datagrams{};
    std::uint64_t rio_send_datagrams{};
    std::uint64_t rio_receive_queue_dropped_datagrams{};
    std::uint64_t rio_fallbacks{};
};

/// Bounded listener-side buffering. These limits apply before a UDP datagram
/// is retained by any per-connection inbox, so peer traffic cannot turn a
/// packet burst into unbounded process memory.
export struct http3_inbox_limits
{
    // A QUIC handshake and the first request can arrive as a burst across
    // hundreds of connections.  32 slots/1024 globally drops valid packets
    // before QUIC flow control can react (especially on IOCP).  Keep the
    // queues bounded, but leave enough burst headroom for the default
    // 256-connection benchmark and real fan-in listeners.
    std::size_t per_connection_datagrams{256U};
    std::size_t max_queued_datagrams{32768U};
    std::size_t max_connection_inboxes{2048U};
};

/// HTTP/3 UDP listener.  Its implementation owns Retry validation, CID
/// routing and socket lifetime; only this stable public contract is exported.
export class http3_server
{
public:
    http3_server(io_context& context, ssl_context& tls, endpoint listen_endpoint,
        server_request_handler handler);
    http3_server(io_context& context, ssl_context& tls, endpoint listen_endpoint,
        async_server_request_handler handler);
    http3_server(io_context& context, ssl_context& tls, endpoint listen_endpoint,
        streaming_server_request_handler handler);
    http3_server(io_context& context, ssl_context& tls, endpoint listen_endpoint,
        async_webtransport_handler handler);
    http3_server(io_context& context, ssl_context& tls, endpoint listen_endpoint,
        http3_server_handlers handlers);
    http3_server(server_context& context, ssl_context& tls,
        endpoint listen_endpoint, server_request_handler handler);
    http3_server(server_context& context, ssl_context& tls,
        endpoint listen_endpoint, async_server_request_handler handler);
    http3_server(server_context& context, ssl_context& tls,
        endpoint listen_endpoint, streaming_server_request_handler handler);
    http3_server(server_context& context, ssl_context& tls,
        endpoint listen_endpoint, async_webtransport_handler handler);
    http3_server(server_context& context, ssl_context& tls,
        endpoint listen_endpoint, http3_server_handlers handlers);
    ~http3_server();
    http3_server(const http3_server&) = delete;
    auto operator=(const http3_server&) -> http3_server& = delete;
    [[nodiscard]] auto start() -> std::expected<void, std::error_code>;
    /// Enable RFC 9221 transport negotiation before `start()`.
    /// A zero size disables HTTP Datagrams/WebTransport support.
    auto set_max_datagram_frame_size(std::uint64_t bytes) -> std::expected<void, std::error_code>;
    /// Enable the draft Multipath QUIC transport parameter before start().
    /// All HTTP/3 workers inherit this setting; clients must opt in too.
    auto set_multipath_initial_max_path_id(std::uint32_t maximum_path_id)
        -> std::expected<void, std::error_code>;
    /// Opt into per-path Datagram PLPMTUD for newly accepted connections.
    /// A path never exceeds `maximum_payload` until its padded PING probe has
    /// been acknowledged by the peer.
    auto set_path_mtu_discovery(std::uint64_t maximum_payload = 1452,
        std::chrono::milliseconds probe_interval = std::chrono::seconds{1},
        std::chrono::milliseconds initial_probe_delay = {})
        -> std::expected<void, std::error_code>;
    /// Opt in to QPACK dynamic-table compression for newly accepted HTTP/3
    /// connections.  Both values must be zero to retain the static-table-only
    /// default; a non-zero capacity requires a non-zero blocked-stream limit.
    auto set_qpack_settings(std::uint64_t max_table_capacity,
        std::uint64_t max_blocked_streams) -> std::expected<void, std::error_code>;
    /// Configure bounded per-connection and listener-wide UDP inbox memory
    /// before start(). Values are rounded up by the lock-free queue itself.
    auto set_inbox_limits(http3_inbox_limits limits)
        -> std::expected<void, std::error_code>;
    /// Explicitly enable server 0-RTT with an application-owned ticket
    /// implementation and shared anti-replay cache. Empty callbacks disable
    /// early data; the context must bind QUIC transport parameters and H3
    /// SETTINGS and must be identical across every worker process.
    auto set_early_data_tickets(
        std::shared_ptr<quic::server_early_data_ticket_callbacks> callbacks,
        std::vector<std::byte> context) -> std::expected<void, std::error_code>;
    [[nodiscard]] auto stop() -> task<void>;
    [[nodiscard]] auto is_running() const noexcept -> bool;
    [[nodiscard]] auto statistics() const noexcept -> http3_server_statistics;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

export auto make_http3_server(io_context& ctx, ssl_context& tls, endpoint ep,
    server_request_handler handler) -> std::unique_ptr<http3_server>;
export auto make_http3_server(io_context& ctx, ssl_context& tls, endpoint ep,
    async_server_request_handler handler) -> std::unique_ptr<http3_server>;
export auto make_http3_server(io_context& ctx, ssl_context& tls, endpoint ep,
    streaming_server_request_handler handler) -> std::unique_ptr<http3_server>;
export auto make_http3_server(io_context& ctx, ssl_context& tls, endpoint ep,
    async_webtransport_handler handler) -> std::unique_ptr<http3_server>;
export auto make_http3_server(io_context& ctx, ssl_context& tls, endpoint ep,
    http3_server_handlers handlers) -> std::unique_ptr<http3_server>;
export auto make_http3_server(server_context& ctx, ssl_context& tls, endpoint ep,
    server_request_handler handler) -> std::unique_ptr<http3_server>;
export auto make_http3_server(server_context& ctx, ssl_context& tls, endpoint ep,
    async_server_request_handler handler) -> std::unique_ptr<http3_server>;
export auto make_http3_server(server_context& ctx, ssl_context& tls, endpoint ep,
    streaming_server_request_handler handler) -> std::unique_ptr<http3_server>;
export auto make_http3_server(server_context& ctx, ssl_context& tls, endpoint ep,
    async_webtransport_handler handler) -> std::unique_ptr<http3_server>;
export auto make_http3_server(server_context& ctx, ssl_context& tls, endpoint ep,
    http3_server_handlers handlers) -> std::unique_ptr<http3_server>;

} // namespace cnetmod::http::v3
