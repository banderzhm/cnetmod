module;
#include <cnetmod/config.hpp>
#ifdef CNETMOD_ENABLE_QUIC
    #ifdef CNETMOD_HAS_SSL
export module cnetmod.protocol.http.v3.client;
import std;
import cnetmod.core.ssl;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;
import cnetmod.coro.channel;
import cnetmod.coro.mutex;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.protocol.quic;
import cnetmod.protocol.http.v3.session;

namespace cnetmod::http::v3 {
namespace detail {
    struct local_path_receiver;
}

export struct http3_client_options
{
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds request_timeout{30000};
    std::uint64_t h3_initial_max_data{1048576};
    std::uint64_t h3_initial_max_stream_data{262144};
    std::uint64_t h3_max_header_list_size{80};
    std::uint64_t h3_qpack_max_table_capacity{65536};
    std::uint64_t h3_qpack_blocked_streams{100};
    /// Non-zero enables RFC 9221 QUIC DATAGRAM transport negotiation.
    std::uint64_t max_datagram_frame_size{};
    /// Opt into per-path Datagram PLPMTUD. The transport remains at the
    /// RFC-mandated 1200-byte baseline until a padded PING probe is ACKed.
    bool enable_path_mtu_discovery{};
    std::uint64_t max_path_mtu{1452};
    std::chrono::milliseconds path_mtu_probe_interval{1000};
    /// Delay the first probe on each newly validated path. Zero preserves
    /// immediate RFC 9000-compatible PLPMTUD probing.
    std::chrono::milliseconds path_mtu_initial_probe_delay{};
    /// Explicit experimental Multipath QUIC opt-in.  Both HTTP/3 endpoints
    /// must advertise the same draft transport extension; nullopt preserves
    /// ordinary RFC 9000 single-path behavior.
    std::optional<std::uint32_t> multipath_initial_max_path_id;
    bool verify_certificate{true};
    std::string tls_sni_host;
    /// Optional application-owned TLS 1.3 ticket loaded before connect().
    /// The ticket is opaque and must be persisted securely by the caller.
    std::optional<quic::session_ticket> resumption_ticket;
    /// Explicit transport opt-in. HTTP-layer automatic 0-RTT remains off;
    /// callers must only send replay-safe requests when using this flag.
    bool enable_early_data{false};
    /// Automatic retries are restricted to replay-safe methods.  This also
    /// applies when a future resumption ticket enables 0-RTT.
    bool retry_idempotent_requests{true};
    /// Server push is opt-in. `nullopt` (the default) sends no MAX_PUSH_ID
    /// and rejects all pushes; zero permits exactly Push ID 0.
    std::optional<std::uint64_t> max_push_id;
    /// Invoked after a parent response has delivered a valid PUSH_PROMISE and
    /// before a streaming Push body is produced. Returning an error sends
    /// CANCEL_PUSH without affecting the parent response.
    server_push_promise_handler on_server_push_promise;
    server_push_handler on_server_push;
};

export class http3_client
{
public:
    http3_client(io_context& context, ssl_context& tls, http3_client_options options = {});
    /// Connect to a QUIC peer. `origin_host`/`origin_port` preserve the HTTP
    /// authority when the peer was selected through Alt-Svc.
    [[nodiscard]] auto connect(std::string_view host, std::uint16_t port,
        std::string_view origin_host = {}, std::uint16_t origin_port = 0)
        -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto send_request(const http3_request& request)
        -> task<std::expected<http3_response, std::error_code>>;
    [[nodiscard]] auto send_request(const http3_request& request,
        cnetmod::cancel_token& token)
        -> task<std::expected<http3_response, std::error_code>>;
    /// Explicit opt-in response streaming.  The callback starts after
    /// response HEADERS and receives DATA with bounded back-pressure.
    [[nodiscard]] auto send_request_streaming(const http3_request& request,
        streaming_client_response_handler handler)
        -> task<std::expected<http3_response, std::error_code>>;
    [[nodiscard]] auto send_request_streaming(const http3_request& request,
        streaming_client_response_handler handler, cnetmod::cancel_token& token)
        -> task<std::expected<http3_response, std::error_code>>;
    [[nodiscard]] auto send_request(const http3_request& request,
        cnetmod::deadline deadline)
        -> task<std::expected<http3_response, std::error_code>>;
    /// Establish a persistent WebTransport session over the current HTTP/3
    /// origin.  This never falls back to TCP because WebTransport requires
    /// QUIC/HTTP/3 semantics.
    [[nodiscard]] auto connect_webtransport(const http3_request& request)
        -> task<std::expected<webtransport_session, std::error_code>>;
    /// Explicitly reject a previously announced server Push. The ID is
    /// provided to `http3_client_options::on_server_push_promise`.
    [[nodiscard]] auto cancel_server_push(std::uint64_t push_id)
        -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto close() -> task<void>;
    [[nodiscard]] auto is_connected() const noexcept -> bool;
    [[nodiscard]] auto peer_host() const noexcept -> std::string_view;
    [[nodiscard]] auto peer_port() const noexcept -> std::uint16_t;
    [[nodiscard]] auto can_reuse_origin(std::string_view host, std::uint16_t port) const noexcept -> bool;
    /// Export the newest TLS resumption ticket after post-handshake processing.
    [[nodiscard]] auto take_resumption_ticket()
        -> std::expected<quic::session_ticket, std::error_code>;
    [[nodiscard]] auto early_data_status() const noexcept -> quic::early_data_state;
    /// Validate an additional remote UDP endpoint on an already connected
    /// Multipath QUIC transport.  Requests remain HTTP/3 requests; only the
    /// transport path selection changes after validation succeeds. The task
    /// succeeds only after the peer has returned a matching PATH_RESPONSE.
    [[nodiscard]] auto async_probe_path(std::uint32_t path_id, endpoint peer)
        -> task<std::expected<void, std::error_code>>;
    /// Validate a path through a separately bound local UDP endpoint. The
    /// client owns and joins its receiver before the QUIC connection can be
    /// released, so this safely supports multiple local interfaces/sockets.
    [[nodiscard]] auto async_probe_path(std::uint32_t path_id, endpoint peer,
        endpoint local_endpoint)
        -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto async_abandon_path(std::uint32_t path_id,
        std::uint64_t error_code = 0)
        -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto set_path_backup(std::uint32_t path_id, bool backup)
        -> std::expected<void, std::error_code>;
    /// Return the ACK-validated Datagram PLPMTUD ceiling for a QUIC path.
    [[nodiscard]] auto discovered_path_mtu(std::uint32_t path_id) const
        -> std::optional<std::size_t>;
    [[nodiscard]] auto path_is_validated(std::uint32_t path_id) const noexcept -> bool;
    [[nodiscard]] auto local_path_endpoint(std::uint32_t path_id)
        -> std::expected<endpoint, std::error_code>;

private:
    auto wait_for_connection_driver() -> task<void>;
    auto stop_local_path_receivers() -> task<void>;

    io_context& ctx_;
    ssl_context& tls_;
    http3_client_options options_;
    std::string host_;
    std::uint16_t port_{};
    std::string origin_host_;
    std::uint16_t origin_port_{};
    std::shared_ptr<quic::quic_connection> connection_;
    std::shared_ptr<channel<std::monostate>> driver_completion_;
    std::shared_ptr<std::atomic_bool> driver_close_requested_;
    // Serializes close() callers. A second closer must wait for the first to
    // join the driver and local path receivers, then observe an idempotently
    // closed client instead of consuming the same completion signal.
    async_mutex lifecycle_mutex_;
    // Protected by lifecycle_mutex_.  A connect attempt captures this before
    // DNS and may publish its transport only if no close() has linearized in
    // the meantime.
    std::uint64_t lifecycle_generation_{};
    // A connection attempt owns several staged resources (DNS result, UDP
    // socket, QUIC driver and HTTP/3 control streams).  Keep competing
    // connect() calls out of those stages, while close() remains independent
    // so it can promptly cancel an in-progress attempt.
    async_mutex connect_mutex_;
    // Completion is a one-shot channel.  Multiple shutdown paths (a
    // handshake timeout and a concurrent close, for example) must elect one
    // waiter rather than consuming it twice.
    async_mutex driver_join_mutex_;
    bool driver_joined_{};
    std::map<std::uint32_t, std::shared_ptr<detail::local_path_receiver>>
        local_path_receivers_;
    // Individual requests, peer-stream consumers and WebTransport setup can
    // legitimately outlive the public client handle while close() is waking
    // their QUIC I/O.  Shared ownership makes that cancellation path safe
    // without serializing independent request streams behind lifecycle_mutex_.
    std::shared_ptr<http3_client_session> session_;
    // Per-connection rather than per-request: the transport can close a
    // rejected 0-RTT connection before it publishes the final TLS state.
    // Keep enough provenance to safely replay an eligible request once.
    bool early_data_attempted_{};
};
} // namespace cnetmod::http::v3
    #endif
#endif
