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
import cnetmod.executor.async_op;
import cnetmod.protocol.http.v2.frame;
import cnetmod.protocol.http.v2.settings;
import cnetmod.protocol.http.v2.header_compression;
import :semantics;
import :request;
import :response;
import :parser;
import :cookie;

#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cnetmod::http {

// =============================================================================
// http_version_preference — HTTP Version Preference
// =============================================================================

export enum class http_version_preference {
    http1_only,      // Only use HTTP/1.1
    http2_only,      // Only use HTTP/2 (requires ALPN)
    http2_preferred, // Prefer HTTP/2, fallback to HTTP/1.1
    http1_preferred, // Prefer HTTP/1.1, but accept HTTP/2
};

// =============================================================================
// client_options — Unified HTTP Client Configuration
// =============================================================================

export struct client_options {
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
    http_version_preference version_pref = http_version_preference::http2_preferred;
    std::uint32_t h2_max_concurrent_streams = 100;
    std::uint32_t h2_initial_window_size = 1 * 1024 * 1024;  // 1MB
    
    // Cookie options
    bool enable_cookies = true;  // Implementation note: cookies.
};

// =============================================================================
// client — Unified HTTP/HTTPS Client (HTTP/1.1 and HTTP/2)
// =============================================================================

export class client {
public:
    explicit client(io_context& ctx, client_options opts = {})
        : ctx_(&ctx), options_(std::move(opts)) {
#ifdef CNETMOD_HAS_SSL
        init_ssl_context();
#endif
    }

    ~client() {
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

    /// Send HTTP request to specific URL (async)
    [[nodiscard]] auto send(http_method method, std::string_view url,
                           std::string_view body = {})
        -> task<std::expected<response, std::error_code>>;

    /// Submit same-origin requests as concurrent HTTP/2 streams on one
    /// connection. HTTP/1.1 falls back to ordered requests. Redirect handling
    /// is intentionally not applied to a batch, because a redirect can change
    /// the origin and therefore cannot remain on the shared HTTP/2 connection.
    [[nodiscard]] auto send_batch(std::span<const request> requests)
        -> task<std::vector<std::expected<response, std::error_code>>>;

    /// GET request (async)
    [[nodiscard]] auto get(std::string_view url)
        -> task<std::expected<response, std::error_code>> {
        return send(http_method::GET, url);
    }

    /// POST request (async)
    [[nodiscard]] auto post(std::string_view url, std::string_view body)
        -> task<std::expected<response, std::error_code>> {
        return send(http_method::POST, url, body);
    }

    /// PUT request (async)
    [[nodiscard]] auto put(std::string_view url, std::string_view body)
        -> task<std::expected<response, std::error_code>> {
        return send(http_method::PUT, url, body);
    }

    /// DELETE request (async)
    [[nodiscard]] auto delete_(std::string_view url)
        -> task<std::expected<response, std::error_code>> {
        return send(http_method::DELETE_, url);
    }

    /// PATCH request (async)
    [[nodiscard]] auto patch(std::string_view url, std::string_view body)
        -> task<std::expected<response, std::error_code>> {
        return send(http_method::PATCH, url, body);
    }

    /// Close connection
    void close() noexcept;

    /// Get client options
    [[nodiscard]] auto options() const noexcept -> const client_options& {
        return options_;
    }

    /// Set client options
    auto& set_options(client_options opts) noexcept {
        options_ = std::move(opts);
        return *this;
    }
    
    /// Get cookie jar
    [[nodiscard]] auto cookies() -> cookie_jar& {
        return cookies_;
    }
    
    [[nodiscard]] auto cookies() const -> const cookie_jar& {
        return cookies_;
    }
    
    /// Set a cookie (convenience method)
    auto& set_cookie(std::string_view name, std::string_view value,
                    std::string_view domain = {}, std::string_view path = "/") {
        cookie c;
        c.name = std::string(name);
        c.value = std::string(value);
        if (!domain.empty()) c.domain = std::string(domain);
        c.path = std::string(path);
        cookies_.add(c);
        return *this;
    }
    
    /// Clear all cookies
    auto& clear_cookies() {
        cookies_.clear();
        return *this;
    }
    
    /// Release the underlying connection for WebSocket upgrade
    /// After calling this, the client is no longer usable
    /// The returned socket can be used with ws::connection
    [[nodiscard]] auto release_connection() -> std::optional<socket> {
        if (!state_ || !state_->conn) {
            return std::nullopt;
        }
        
        auto sock = std::move(state_->conn->native_socket());
        state_.reset();
        return sock;
    }
    
    /// Check if connection is open and can be released
    [[nodiscard]] auto has_connection() const noexcept -> bool {
        return state_ && state_->conn && state_->conn->is_open();
    }

private:
    enum class protocol_type {
        http1,
        http2
    };

    struct connection_state {
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
    cookie_jar cookies_;  // Implementation note: Cookie.
    
#ifdef CNETMOD_HAS_SSL
    std::optional<ssl_context> ssl_ctx_;
    
    void init_ssl_context();
#endif

    /// Connect to host:port with protocol negotiation (async)
    [[nodiscard]] auto connect(std::string_view host, std::uint16_t port, bool use_ssl)
        -> task<std::expected<void, std::error_code>>;

    /// Send HTTP/1.1 request (async)
    [[nodiscard]] auto send_http1(const request& req)
        -> task<std::expected<response, std::error_code>>;

    [[nodiscard]] auto send_http2(const request& req)
        -> task<std::expected<response, std::error_code>>;

    [[nodiscard]] auto send_http2_batch(std::span<const request> requests)
        -> task<std::vector<std::expected<response, std::error_code>>>;

    /// Send with redirect handling (async)
    [[nodiscard]] auto send_with_redirects(const request& req, std::size_t redirect_count)
        -> task<std::expected<response, std::error_code>>;

    /// Low-level I/O helpers (async)
    [[nodiscard]] auto write_data(std::string_view data)
        -> task<std::expected<void, std::error_code>>;
    
    [[nodiscard]] auto read_data(void* buffer, std::size_t size)
        -> task<std::expected<std::size_t, std::error_code>>;
};

} // namespace cnetmod::http
