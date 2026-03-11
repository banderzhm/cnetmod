module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_NGHTTP2
#define NGHTTP2_NO_SSIZE_T
#include <nghttp2/nghttp2.h>
#endif

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
import :types;
import :request;
import :response;
import :parser;
import :cookie;

#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

#ifdef CNETMOD_HAS_NGHTTP2
import :h2_types;
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
    bool enable_cookies = true;  // 自动管理 cookies
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

    ~client() = default;

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
        
#ifdef CNETMOD_HAS_SSL
        std::optional<ssl_stream> ssl;
#endif

#ifdef CNETMOD_HAS_NGHTTP2
        nghttp2_session* h2_session = nullptr;
        std::int32_t next_stream_id = 1;
        
        struct h2_stream_data {
            int status_code = 0;
            header_map headers;
            std::string body;
            bool complete = false;
        };
        std::unordered_map<std::int32_t, h2_stream_data> h2_streams;
#endif
    };

    io_context* ctx_;
    client_options options_;
    std::optional<connection_state> state_;
    cookie_jar cookies_;  // Cookie 存储
    
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

    /// Send with redirect handling (async)
    [[nodiscard]] auto send_with_redirects(const request& req, std::size_t redirect_count)
        -> task<std::expected<response, std::error_code>>;

#ifdef CNETMOD_HAS_NGHTTP2
    /// Send HTTP/2 request (async)
    [[nodiscard]] auto send_http2(const request& req)
        -> task<std::expected<response, std::error_code>>;
    
    /// Initialize HTTP/2 session (async)
    [[nodiscard]] auto init_h2_session() -> task<std::expected<void, std::error_code>>;
    
    /// HTTP/2 callbacks
    static auto h2_on_header_callback(
        nghttp2_session* session, const nghttp2_frame* frame,
        const uint8_t* name, size_t namelen,
        const uint8_t* value, size_t valuelen,
        uint8_t flags, void* user_data) -> int;
    
    static auto h2_on_data_chunk_recv_callback(
        nghttp2_session* session, uint8_t flags, int32_t stream_id,
        const uint8_t* data, size_t len, void* user_data) -> int;
    
    static auto h2_on_frame_recv_callback(
        nghttp2_session* session, const nghttp2_frame* frame,
        void* user_data) -> int;
    
    static auto h2_on_stream_close_callback(
        nghttp2_session* session, int32_t stream_id,
        uint32_t error_code, void* user_data) -> int;
#endif

    /// Low-level I/O helpers (async)
    [[nodiscard]] auto write_data(std::string_view data)
        -> task<std::expected<void, std::error_code>>;
    
    [[nodiscard]] auto read_data(void* buffer, std::size_t size)
        -> task<std::expected<std::size_t, std::error_code>>;
};

} // namespace cnetmod::http
