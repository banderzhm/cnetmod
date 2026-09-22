module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http:client;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.protocol.tcp;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.coro.spawn;
import cnetmod.coro.channel;
import cnetmod.coro.wait_group;
import cnetmod.coro.semaphore;
import cnetmod.coro.mutex;
import cnetmod.executor.async_op;
import cnetmod.protocol.http.v2.frame;
import cnetmod.protocol.http.v2.settings;
import cnetmod.protocol.http.v2.header_compression;
import cnetmod.protocol.http.semantics;
import :request;
import :response;
import :parser;
import :cookie;

#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
import cnetmod.protocol.http.v3.session;
#endif

namespace cnetmod::http {

// =============================================================================
// http_version_preference — HTTP Version Preference
// =============================================================================

export enum class http_version_preference
{
    http1_only,      // Only use HTTP/1.1
    http2_only,      // Only use HTTP/2 (requires ALPN)
    http2_preferred, // Prefer HTTP/2, fallback to HTTP/1.1
    http1_preferred, // Prefer HTTP/1.1, but accept HTTP/2
    http3_only,      // Require HTTP/3 over QUIC
    http3_preferred, // Prefer HTTP/3, then use the configured TCP preference
};

// =============================================================================
// client_options — Unified HTTP Client Configuration
// =============================================================================

export struct client_options
{
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds request_timeout{30000};
    bool follow_redirects = true;
    std::size_t max_redirects = 10;
    bool keep_alive = true;
    std::string user_agent = "cnetmod-http-client/1.0";

    // SSL/TLS options
    bool verify_peer = true;
    std::string ca_file;
    std::string cert_file;
    std::string key_file;

    // HTTP/2 options
    http_version_preference version_pref =
        http_version_preference::http2_preferred;
    std::uint32_t h2_max_concurrent_streams = 100;
    std::uint32_t h2_initial_window_size = 1 * 1024 * 1024; // 1MB

    // HTTP/3 options. TCP fallback is opt-in and restricted to replay-safe
    // requests; `http3_only` never falls back.
    std::uint64_t h3_qpack_max_table_capacity = 64 * 1024;
    std::uint64_t h3_qpack_blocked_streams = 100;
    std::uint32_t h3_max_concurrent_streams = 100;
    bool http3_fallback_to_tcp = false;
    bool enable_alt_svc_http3 = true;
    /// Optional small cache shared by client instances/processes. Empty keeps
    /// the existing in-memory-only behavior.
    std::string alt_svc_cache_file;
    /// Optional cross-process TLS 1.3 ticket cache for HTTP/3. The file is
    /// treated as sensitive state; callers should place it in a private
    /// directory with restrictive permissions. Empty disables persistence.
    std::string http3_resumption_ticket_file;
    /// Explicitly permit replay-safe HTTP/3 requests to use a persisted TLS
    /// ticket before the handshake completes. Disabled by default.
    bool enable_http3_early_data = false;
    /// RFC 9114 server push is off unless this limit is present. The callback
    /// is invoked on the owning io_context after the pushed response reaches
    /// FIN; it never runs inline on the UDP receive path.
#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
    std::optional<std::uint64_t> http3_max_push_id;
    v3::server_push_handler on_http3_push;
#endif

    // Cookie options
    bool enable_cookies = true; // Implementation note: cookies.
    /**
     * @brief Optional HTTP/1.1 response-body budget enforced before accumulation.
     *
     * Zero preserves the existing policy. A nonzero budget also bounds chunk
     * framing and closes failed response connections. HTTP/2 and HTTP/3 are not
     * covered; bounded consumers must explicitly select http1_only.
     */
    std::size_t http1_response_body_limit = 0;
};

// =============================================================================
// client — Unified HTTP/HTTPS Client (HTTP/1.1, HTTP/2, and HTTP/3)
// =============================================================================

export class client
{
public:
    explicit client(io_context& ctx, client_options opts = {})
        : ctx_(&ctx), options_(std::move(opts))
    {
#ifdef CNETMOD_HAS_SSL
        init_ssl_context();
#endif
        load_alt_svc_cache();
    }

    ~client()
    {
        close();
    }

    // Non-copyable
    client(const client&) = delete;
    auto operator=(const client&) -> client& = delete;

    // Movable
    client(client&&) noexcept = default;
    auto operator=(client&&) noexcept -> client& = delete;

    /// Send HTTP request and receive response (async)
    [[nodiscard]] auto send(const request& req)
        -> task<std::expected<response, std::error_code>>;

    /// Send with cancellation propagated through request I/O and TLS records.
    [[nodiscard]] auto send(const request& req, cnetmod::cancel_token& token)
        -> task<std::expected<response, std::error_code>>;

    /// Enforce an absolute deadline and cancel the active request I/O.
    [[nodiscard]] auto send(const request& req, cnetmod::deadline deadline)
        -> task<std::expected<response, std::error_code>>;

    /// Send HTTP request to specific URL (async)
    [[nodiscard]] auto send(http_method method, std::string_view url,
        std::string_view body = {})
        -> task<std::expected<response, std::error_code>>;

    /// Submit same-origin requests as concurrent HTTP/2 or HTTP/3 streams on one
    /// connection. HTTP/1.1 falls back to ordered requests. Redirect handling
    /// is intentionally not applied to a batch, because a redirect can change
    /// the origin and therefore cannot remain on the shared HTTP/2 connection.
    [[nodiscard]] auto send_batch(std::span<const request> requests)
        -> task<std::vector<std::expected<response, std::error_code>>>;

    /// GET request (async)
    [[nodiscard]] auto get(std::string_view url)
        -> task<std::expected<response, std::error_code>>
    {
        return send(http_method::GET, url);
    }

    /// POST request (async)
    [[nodiscard]] auto post(std::string_view url, std::string_view body)
        -> task<std::expected<response, std::error_code>>
    {
        return send(http_method::POST, url, body);
    }

    /// PUT request (async)
    [[nodiscard]] auto put(std::string_view url, std::string_view body)
        -> task<std::expected<response, std::error_code>>
    {
        return send(http_method::PUT, url, body);
    }

    /// DELETE request (async)
    [[nodiscard]] auto delete_(std::string_view url)
        -> task<std::expected<response, std::error_code>>
    {
        return send(http_method::DELETE_, url);
    }

    /// PATCH request (async)
    [[nodiscard]] auto patch(std::string_view url, std::string_view body)
        -> task<std::expected<response, std::error_code>>
    {
        return send(http_method::PATCH, url, body);
    }

    /// Close connection
    void close() noexcept;

    /// Gracefully close the active HTTP/3 QUIC connection before releasing it.
    /// HTTP/1.1/2 close synchronously as before.
    [[nodiscard]] auto close_async() -> task<void>;

    /// Get client options
    [[nodiscard]] auto options() const noexcept -> const client_options&
    {
        return options_;
    }

    /// Set client options
    auto& set_options(client_options opts) noexcept
    {
        options_ = std::move(opts);
#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
        raced_use_http3_.reset();
#endif
        h3_alt_svc_.clear();
        load_alt_svc_cache();
        return *this;
    }

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
    /// Whether a valid persisted/in-memory Alt-Svc entry exists for an origin.
    [[nodiscard]] auto has_http3_alt_svc(std::string_view host,
        std::uint16_t port) const -> bool;
#endif

    /// Get cookie jar
    [[nodiscard]] auto cookies() -> cookie_jar&
    {
        return cookies_;
    }

    [[nodiscard]] auto cookies() const -> const cookie_jar&
    {
        return cookies_;
    }

    /// Set a cookie (convenience method)
    auto& set_cookie(std::string_view name, std::string_view value,
        std::string_view domain = {}, std::string_view path = "/")
    {
        cookie c;
        c.name = std::string(name);
        c.value = std::string(value);
        if (!domain.empty())
            c.domain = std::string(domain);
        c.path = std::string(path);
        cookies_.add(c);
        return *this;
    }

    /// Clear all cookies
    auto& clear_cookies()
    {
        cookies_.clear();
        return *this;
    }

    /// Release the underlying connection for WebSocket upgrade
    /// After calling this, the client is no longer usable
    /// The returned socket can be used with ws::connection
    [[nodiscard]] auto release_connection() -> std::optional<socket>
    {
        if (!state_ || !state_->conn)
        {
            return std::nullopt;
        }

        auto sock = std::move(state_->conn->native_socket());
        state_.reset();
        return sock;
    }

    /// Check if connection is open and can be released
    [[nodiscard]] auto has_connection() const noexcept -> bool
    {
        return state_ && state_->conn && state_->conn->is_open();
    }

private:
    enum class protocol_type
    {
        http1,
        http2
    };

    struct connection_state
    {
        std::optional<tcp::connection> conn;
        std::string host;
        std::uint16_t port = 0;
        bool is_ssl = false;
        protocol_type protocol = protocol_type::http1;
        std::string request_buffer;
        std::string read_buffer;
        std::string body_buffer;
        v2::header_compression h2_encoder;
        v2::header_compression h2_decoder;
        std::uint32_t h2_next_stream_id = 1;
        bool h2_initialized = false;

#ifdef CNETMOD_HAS_SSL
        std::optional<ssl_stream> ssl;
#endif
    };

    io_context* ctx_;
    client_options options_;
    std::optional<connection_state> state_;
    cookie_jar cookies_; // Implementation note: Cookie.

    struct alt_svc_entry
    {
        std::chrono::steady_clock::time_point expires_at;
        std::uint16_t peer_port{};
    };

    std::unordered_map<std::string, alt_svc_entry> h3_alt_svc_;
    // Result of the one-time safe first-request H3/TCP race.  Keeping the
    // protocol decision avoids racing every subsequent request while the
    // winning candidate connection is intentionally closed after handoff.
    std::optional<bool> raced_use_http3_;

#ifdef CNETMOD_HAS_SSL
    std::optional<ssl_context> ssl_ctx_;
    #ifdef CNETMOD_ENABLE_QUIC
    std::optional<ssl_context> h3_ssl_ctx_;
    // Keep the HTTP/3 implementation out of this public module partition's
    // BMI.  The concrete v3 client is recovered only in http_client.cpp,
    // where its lifetime is still owned by this shared control block.
    std::shared_ptr<void> h3_client_;
    // HTTP/3 requests may share one connection, but creation/replacement of
    // the pooled client must be serialized.  Without this guard two batch
    // items can both observe a missing/non-reusable connection and replace
    // the same client while the other coroutine is still using it.
    async_mutex h3_lifecycle_mutex_;
    #endif

    void init_ssl_context();
#endif

    void load_alt_svc_cache();
    void persist_alt_svc_cache() const;

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
    [[nodiscard]] auto send_http3_tcp_race(const request& req,
        cnetmod::cancel_token& token)
        -> task<std::expected<response, std::error_code>>;
    [[nodiscard]] auto send_http3(const request& req, cnetmod::cancel_token& token,
        std::uint16_t peer_port = 0)
        -> task<std::expected<response, std::error_code>>;
    [[nodiscard]] auto send_http3_batch(std::span<const request> requests)
        -> task<std::vector<std::expected<response, std::error_code>>>;
    [[nodiscard]] auto send_http3_batch_item(std::span<const request> requests,
        std::vector<std::expected<response, std::error_code>>& results,
        async_wait_group& completed, async_semaphore& permits, std::size_t index)
        -> task<void>;
    [[nodiscard]] auto http3_alt_svc_port(std::string_view host, std::uint16_t port) const
        -> std::optional<std::uint16_t>;
    void remember_http3_alt_svc(std::string_view host, std::uint16_t port,
        std::string_view value);
#endif

    /// Connect to host:port with protocol negotiation (async)
    [[nodiscard]] auto connect(std::string_view host, std::uint16_t port,
        bool use_ssl)
        -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto connect(std::string_view host, std::uint16_t port,
        bool use_ssl, cnetmod::cancel_token& token)
        -> task<std::expected<void, std::error_code>>;

    /// Send HTTP/1.1 request (async)
    [[nodiscard]] auto send_http1(const request& req)
        -> task<std::expected<response, std::error_code>>;
    [[nodiscard]] auto send_http1(const request& req, cnetmod::cancel_token& token)
        -> task<std::expected<response, std::error_code>>;

    /**
     * Specializes response decoding so unlimited clients do not execute
     * bounded-response checks in their receive loops.
     */
    template <bool Bounded>
    [[nodiscard]] auto send_http1_impl(const request& req, cnetmod::cancel_token& token)
        -> task<std::expected<response, std::error_code>>;

    [[nodiscard]] auto send_http2(const request& req)
        -> task<std::expected<response, std::error_code>>;
    [[nodiscard]] auto send_http2(const request& req, cnetmod::cancel_token& token)
        -> task<std::expected<response, std::error_code>>;

    [[nodiscard]] auto send_http2_batch(std::span<const request> requests)
        -> task<std::vector<std::expected<response, std::error_code>>>;

    /// Send with redirect handling (async)
    [[nodiscard]] auto send_with_redirects(const request& req,
        std::size_t redirect_count)
        -> task<std::expected<response, std::error_code>>;
    [[nodiscard]] auto send_with_redirects(const request& req,
        std::size_t redirect_count, cnetmod::cancel_token& token)
        -> task<std::expected<response, std::error_code>>;

    /// Low-level I/O helpers (async)
    [[nodiscard]] auto write_data(std::string_view data)
        -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto write_data(std::string_view data, cnetmod::cancel_token& token)
        -> task<std::expected<void, std::error_code>>;

    [[nodiscard]] auto read_data(void* buffer, std::size_t size)
        -> task<std::expected<std::size_t, std::error_code>>;
    [[nodiscard]] auto read_data(void* buffer, std::size_t size,
        cnetmod::cancel_token& token)
        -> task<std::expected<std::size_t, std::error_code>>;
};

} // namespace cnetmod::http
