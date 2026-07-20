module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_SSL
#include <openssl/ssl.h>
#include <openssl/bio.h>
#endif

export module cnetmod.core.ssl;

import std;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.task;

namespace cnetmod {

#ifdef CNETMOD_HAS_SSL

// =============================================================================
// OpenSSL Global Initialization (thread-safe, executes once)
// =============================================================================

namespace detail {

void ssl_global_init() noexcept;

} // namespace detail

// =============================================================================
// ssl_error_category — Map OpenSSL Errors to std::error_code
// =============================================================================

namespace detail {

class ssl_error_category_impl : public std::error_category {
public:
    auto name() const noexcept -> const char* override;
    auto message(int ev) const -> std::string override;
};

auto ssl_category() -> const std::error_category&;

} // namespace detail

/// Get error_code from OpenSSL error stack
export auto make_ssl_error() -> std::error_code;

/// Create error_code from SSL_get_error result
export auto make_ssl_error(int ssl_err) -> std::error_code;

// =============================================================================
// ssl_context — RAII wrapper for SSL_CTX
// =============================================================================

export class ssl_context {
public:
    ~ssl_context();

    // Non-copyable
    ssl_context(const ssl_context&) = delete;
    auto operator=(const ssl_context&) -> ssl_context& = delete;

    // Movable
    ssl_context(ssl_context&& other) noexcept;
    auto operator=(ssl_context&& other) noexcept -> ssl_context&;

    /// Create TLS client context
    [[nodiscard]] static auto client() -> std::expected<ssl_context, std::error_code>;

    /// Create TLS server context
    [[nodiscard]] static auto server() -> std::expected<ssl_context, std::error_code>;

    /// Create DTLS client context for datagram protocols such as CoAPS.
    [[nodiscard]] static auto dtls_client() -> std::expected<ssl_context, std::error_code>;

    /// Create DTLS server context for datagram protocols such as CoAPS.
    [[nodiscard]] static auto dtls_server() -> std::expected<ssl_context, std::error_code>;

    /// Load certificate file (PEM)
    [[nodiscard]] auto load_cert_file(std::string_view path) -> std::expected<void, std::error_code>;

    /// Load private key file (PEM)
    [[nodiscard]] auto load_key_file(std::string_view path) -> std::expected<void, std::error_code>;

    /// Load CA certificate file
    [[nodiscard]] auto load_ca_file(std::string_view path) -> std::expected<void, std::error_code>;

    /// Load system default CA certificates
    [[nodiscard]] auto set_default_ca() -> std::expected<void, std::error_code>;

    /// Set whether to verify peer certificate
    void set_verify_peer(bool verify) noexcept;

    /// Request Linux kernel TLS when the runtime OpenSSL and kernel support it.
    /// Unsupported cipher suites or kernels transparently retain user-space TLS.
    void set_kernel_tls(bool enabled) noexcept;

    [[nodiscard]] auto kernel_tls_enabled() const noexcept -> bool {
        return kernel_tls_enabled_;
    }

    /// Server-side mTLS mode: verify and require a client certificate.
    void set_require_peer_certificate(bool require) noexcept;

    // =========================================================================
    // ALPN (Application-Layer Protocol Negotiation)
    // =========================================================================

    /// Configure server-side ALPN protocol selection
    /// @param protos Protocols in server preference order (e.g., {"h2", "http/1.1"})
    void configure_alpn_server(std::initializer_list<std::string_view> protos);

    /// Configure client-side ALPN protocol offers
    /// @param protos Protocols the client supports (e.g., {"h2", "http/1.1"})
    void configure_alpn_client(std::initializer_list<std::string_view> protos);

    /// Get native SSL_CTX pointer
    [[nodiscard]] auto native() const noexcept -> SSL_CTX* { return ctx_; }

private:
    explicit ssl_context(SSL_CTX* ctx) noexcept : ctx_(ctx) {}

    /// ALPN server-side selection callback
    static auto alpn_select_cb(
        SSL*, const unsigned char** out, unsigned char* outlen,
        const unsigned char* in, unsigned int inlen,
        void* arg) -> int;

    static auto dtls_generate_cookie(SSL*, unsigned char* cookie,
                                     unsigned int* cookie_len) -> int;

    static auto dtls_verify_cookie(SSL*, const unsigned char* cookie,
                                   unsigned int cookie_len) -> int;

    SSL_CTX* ctx_ = nullptr;
    std::vector<unsigned char> alpn_wire_;  // Server-side ALPN wire format
    bool kernel_tls_enabled_ = false;
};

// =============================================================================
// ssl_stream — Async SSL stream based on Memory BIO
// =============================================================================

export class ssl_stream {
public:
    /// Construct ssl_stream, bind to existing ssl_context, io_context and socket
    /// Socket must already be connected via async_connect (client) or obtained from async_accept (server)
    ssl_stream(ssl_context& ssl_ctx, io_context& io_ctx, socket& sock);

    ~ssl_stream();

    // Non-copyable
    ssl_stream(const ssl_stream&) = delete;
    auto operator=(const ssl_stream&) -> ssl_stream& = delete;

    // Movable
    ssl_stream(ssl_stream&& other) noexcept;

    /// Set TLS peer identity (must be called before handshake).
    /// DNS names use SNI + hostname verification; IP literals skip SNI and verify IP SAN.
    void set_hostname(std::string_view hostname);

    /// Set to client mode (connect)
    void set_connect_state() noexcept;

    /// Set to server mode (accept)
    void set_accept_state() noexcept;

    // =========================================================================
    // Async operations
    // =========================================================================

    /// Async TLS handshake
    auto async_handshake() -> task<std::expected<void, std::error_code>>;

    /// Async read decrypted plaintext
    auto async_read(mutable_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>;

    /// Async write plaintext (SSL encrypts then sends)
    auto async_write(const_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>;

    /// Async write all plaintext bytes
    auto async_write_all(const_buffer buf)
        -> task<std::expected<void, std::error_code>>;

    /// Async TLS shutdown
    auto async_shutdown() -> task<std::expected<void, std::error_code>>;

    /// Get ALPN-negotiated protocol after handshake
    [[nodiscard]] auto get_alpn_selected() const noexcept -> std::string_view;

    /// True only when the current Linux connection is actually using kTLS TX.
    [[nodiscard]] auto kernel_tls_active() const noexcept -> bool;

    /// Get native SSL pointer
    [[nodiscard]] auto native() const noexcept -> SSL*;

private:
    // =========================================================================
    // BIO flush/fill helper coroutines
    // =========================================================================

    /// Write encrypted data from wbio_ to socket
    auto flush_wbio() -> task<std::expected<void, std::error_code>>;

    /// Read data from socket and write to rbio_
    auto fill_rbio() -> task<std::expected<void, std::error_code>>;

    io_context& io_ctx_;
    socket&     sock_;
    SSL*        ssl_  = nullptr;
    BIO*        rbio_ = nullptr;  // Network -> SSL (receive direction)
    BIO*        wbio_ = nullptr;  // SSL -> Network (send direction)
    bool        direct_socket_bio_ = false;
    bool        kernel_tls_active_ = false;
};

#endif // CNETMOD_HAS_SSL

} // namespace cnetmod
