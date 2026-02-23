module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#endif

export module cnetmod.core.ssl;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;

namespace cnetmod {

#ifdef CNETMOD_HAS_SSL

// =============================================================================
// OpenSSL Global Initialization (thread-safe, executes once)
// =============================================================================

namespace detail {

inline void ssl_global_init() noexcept {
    static bool done = [] {
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS |
                         OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
        return true;
    }();
    (void)done;
}

} // namespace detail

// =============================================================================
// ssl_error_category — Map OpenSSL Errors to std::error_code
// =============================================================================

namespace detail {

class ssl_error_category_impl : public std::error_category {
public:
    auto name() const noexcept -> const char* override {
        return "openssl";
    }
    auto message(int ev) const -> std::string override {
        if (ev == 0) return "success";
        char buf[256]{};
        ERR_error_string_n(static_cast<unsigned long>(ev), buf, sizeof(buf));
        return buf;
    }
};

inline auto ssl_category() -> const std::error_category& {
    static const ssl_error_category_impl instance;
    return instance;
}

} // namespace detail

/// Get error_code from OpenSSL error stack
export inline auto make_ssl_error() -> std::error_code {
    auto e = ERR_get_error();
    if (e == 0) return {};
    return {static_cast<int>(e), detail::ssl_category()};
}

/// Create error_code from SSL_get_error result
export inline auto make_ssl_error(int ssl_err) -> std::error_code {
    // Should not reach here for WANT_READ/WANT_WRITE
    // For other errors, get detailed info from error stack
    auto e = ERR_get_error();
    if (e != 0)
        return {static_cast<int>(e), detail::ssl_category()};
    // Fallback to ssl_err itself
    return {ssl_err, detail::ssl_category()};
}

// =============================================================================
// ssl_context — RAII wrapper for SSL_CTX
// =============================================================================

export class ssl_context {
public:
    ~ssl_context() {
        if (ctx_) SSL_CTX_free(ctx_);
    }

    // Non-copyable
    ssl_context(const ssl_context&) = delete;
    auto operator=(const ssl_context&) -> ssl_context& = delete;

    // Movable
    ssl_context(ssl_context&& o) noexcept
        : ctx_(std::exchange(o.ctx_, nullptr))
        , alpn_wire_(std::move(o.alpn_wire_))
    {
        // Re-register ALPN callback with new alpn_wire_ address
        if (ctx_ && !alpn_wire_.empty()) {
            SSL_CTX_set_alpn_select_cb(ctx_, alpn_select_cb, &alpn_wire_);
        }
    }
    auto operator=(ssl_context&& o) noexcept -> ssl_context& {
        if (this != &o) {
            if (ctx_) SSL_CTX_free(ctx_);
            ctx_ = std::exchange(o.ctx_, nullptr);
            alpn_wire_ = std::move(o.alpn_wire_);
            if (ctx_ && !alpn_wire_.empty()) {
                SSL_CTX_set_alpn_select_cb(ctx_, alpn_select_cb, &alpn_wire_);
            }
        }
        return *this;
    }

    /// Create TLS client context
    [[nodiscard]] static auto client() -> std::expected<ssl_context, std::error_code> {
        detail::ssl_global_init();
        auto* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) return std::unexpected(make_ssl_error());
        // Set reasonable minimum TLS version
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        return ssl_context{ctx};
    }

    /// Create TLS server context
    [[nodiscard]] static auto server() -> std::expected<ssl_context, std::error_code> {
        detail::ssl_global_init();
        auto* ctx = SSL_CTX_new(TLS_server_method());
        if (!ctx) return std::unexpected(make_ssl_error());
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        return ssl_context{ctx};
    }

    /// Load certificate file (PEM)
    [[nodiscard]] auto load_cert_file(std::string_view path)
        -> std::expected<void, std::error_code>
    {
        std::string p(path);
        if (SSL_CTX_use_certificate_chain_file(ctx_, p.c_str()) != 1)
            return std::unexpected(make_ssl_error());
        return {};
    }

    /// Load private key file (PEM)
    [[nodiscard]] auto load_key_file(std::string_view path)
        -> std::expected<void, std::error_code>
    {
        std::string p(path);
        if (SSL_CTX_use_PrivateKey_file(ctx_, p.c_str(), SSL_FILETYPE_PEM) != 1)
            return std::unexpected(make_ssl_error());
        return {};
    }

    /// Load CA certificate file
    [[nodiscard]] auto load_ca_file(std::string_view path)
        -> std::expected<void, std::error_code>
    {
        std::string p(path);
        if (SSL_CTX_load_verify_locations(ctx_, p.c_str(), nullptr) != 1)
            return std::unexpected(make_ssl_error());
        return {};
    }

    /// Load system default CA certificates
    [[nodiscard]] auto set_default_ca()
        -> std::expected<void, std::error_code>
    {
        if (SSL_CTX_set_default_verify_paths(ctx_) != 1)
            return std::unexpected(make_ssl_error());
        return {};
    }

    /// Set whether to verify peer certificate
    void set_verify_peer(bool verify) noexcept {
        SSL_CTX_set_verify(ctx_,
            verify ? SSL_VERIFY_PEER : SSL_VERIFY_NONE,
            nullptr);
    }

    // =========================================================================
    // ALPN (Application-Layer Protocol Negotiation)
    // =========================================================================

    /// Configure server-side ALPN protocol selection
    /// @param protos Protocols in server preference order (e.g., {"h2", "http/1.1"})
    void configure_alpn_server(std::initializer_list<std::string_view> protos) {
        alpn_wire_.clear();
        for (auto p : protos) {
            alpn_wire_.push_back(static_cast<unsigned char>(p.size()));
            alpn_wire_.insert(alpn_wire_.end(),
                reinterpret_cast<const unsigned char*>(p.data()),
                reinterpret_cast<const unsigned char*>(p.data() + p.size()));
        }
        SSL_CTX_set_alpn_select_cb(ctx_, alpn_select_cb, &alpn_wire_);
    }

    /// Configure client-side ALPN protocol offers
    /// @param protos Protocols the client supports (e.g., {"h2", "http/1.1"})
    void configure_alpn_client(std::initializer_list<std::string_view> protos) {
        std::vector<unsigned char> wire;
        for (auto p : protos) {
            wire.push_back(static_cast<unsigned char>(p.size()));
            wire.insert(wire.end(),
                reinterpret_cast<const unsigned char*>(p.data()),
                reinterpret_cast<const unsigned char*>(p.data() + p.size()));
        }
        SSL_CTX_set_alpn_protos(ctx_, wire.data(),
            static_cast<unsigned int>(wire.size()));
    }

    /// Get native SSL_CTX pointer
    [[nodiscard]] auto native() const noexcept -> SSL_CTX* { return ctx_; }

private:
    explicit ssl_context(SSL_CTX* ctx) noexcept : ctx_(ctx) {}

    /// ALPN server-side selection callback
    static auto alpn_select_cb(
        SSL*, const unsigned char** out, unsigned char* outlen,
        const unsigned char* in, unsigned int inlen,
        void* arg) -> int
    {
        auto* server_protos = static_cast<std::vector<unsigned char>*>(arg);
        unsigned char* selected = nullptr;
        unsigned char selected_len = 0;
        if (SSL_select_next_proto(
                &selected, &selected_len,
                server_protos->data(),
                static_cast<unsigned int>(server_protos->size()),
                in, inlen) != OPENSSL_NPN_NEGOTIATED) {
            return SSL_TLSEXT_ERR_NOACK;
        }
        *out = selected;
        *outlen = selected_len;
        return SSL_TLSEXT_ERR_OK;
    }

    SSL_CTX* ctx_ = nullptr;
    std::vector<unsigned char> alpn_wire_;  // Server-side ALPN wire format
};

// =============================================================================
// ssl_stream — Async SSL stream based on Memory BIO
// =============================================================================

export class ssl_stream {
public:
    /// Construct ssl_stream, bind to existing ssl_context, io_context and socket
    /// Socket must already be connected via async_connect (client) or obtained from async_accept (server)
    ssl_stream(ssl_context& ssl_ctx, io_context& io_ctx, socket& sock)
        : io_ctx_(io_ctx), sock_(sock)
    {
        ssl_ = SSL_new(ssl_ctx.native());

        // Create memory BIO pair
        rbio_ = BIO_new(BIO_s_mem());
        wbio_ = BIO_new(BIO_s_mem());

        // Associate with SSL: SSL reads from rbio_ (network data in), writes to wbio_ (encrypted data out)
        // SSL_set_bio takes ownership of BIOs
        SSL_set_bio(ssl_, rbio_, wbio_);
    }

    ~ssl_stream() {
        if (ssl_) {
            // SSL_free also releases associated BIOs
            SSL_free(ssl_);
        }
    }

    // Non-copyable
    ssl_stream(const ssl_stream&) = delete;
    auto operator=(const ssl_stream&) -> ssl_stream& = delete;

    // Movable
    ssl_stream(ssl_stream&& o) noexcept
        : io_ctx_(o.io_ctx_), sock_(o.sock_)
        , ssl_(std::exchange(o.ssl_, nullptr))
        , rbio_(std::exchange(o.rbio_, nullptr))
        , wbio_(std::exchange(o.wbio_, nullptr))
    {}

    /// Set SNI hostname (must be called before handshake)
    void set_hostname(std::string_view hostname) {
        std::string h(hostname);
        SSL_set_tlsext_host_name(ssl_, h.c_str());
        // Also set hostname for certificate verification
        auto* param = SSL_get0_param(ssl_);
        X509_VERIFY_PARAM_set1_host(param, h.c_str(), h.size());
    }

    /// Set to client mode (connect)
    void set_connect_state() noexcept { SSL_set_connect_state(ssl_); }

    /// Set to server mode (accept)
    void set_accept_state() noexcept { SSL_set_accept_state(ssl_); }

    // =========================================================================
    // Async operations
    // =========================================================================

    /// Async TLS handshake
    auto async_handshake() -> task<std::expected<void, std::error_code>> {
        for (;;) {
            int ret = SSL_do_handshake(ssl_);
            if (ret == 1) {
                // Handshake complete, flush any remaining data in wbio_
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                co_return {};
            }

            int err = SSL_get_error(ssl_, ret);
            switch (err) {
            case SSL_ERROR_WANT_WRITE: {
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                break;
            }
            case SSL_ERROR_WANT_READ: {
                // Flush output first, then read input
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                auto rr = co_await fill_rbio();
                if (!rr) co_return std::unexpected(rr.error());
                break;
            }
            default:
                co_return std::unexpected(make_ssl_error(err));
            }
        }
    }

    /// Async read decrypted plaintext
    auto async_read(mutable_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>
    {
        for (;;) {
            int ret = SSL_read(ssl_, buf.data, static_cast<int>(buf.size));
            if (ret > 0) {
                co_return static_cast<std::size_t>(ret);
            }

            int err = SSL_get_error(ssl_, ret);
            switch (err) {
            case SSL_ERROR_WANT_READ: {
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                auto rr = co_await fill_rbio();
                if (!rr) co_return std::unexpected(rr.error());
                break;
            }
            case SSL_ERROR_WANT_WRITE: {
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                break;
            }
            case SSL_ERROR_ZERO_RETURN:
                // Peer has closed TLS
                co_return static_cast<std::size_t>(0);
            default:
                co_return std::unexpected(make_ssl_error(err));
            }
        }
    }

    /// Async write plaintext (SSL encrypts then sends)
    auto async_write(const_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>
    {
        for (;;) {
            int ret = SSL_write(ssl_, buf.data, static_cast<int>(buf.size));
            if (ret > 0) {
                // Encrypted data in wbio_, flush to socket
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                co_return static_cast<std::size_t>(ret);
            }

            int err = SSL_get_error(ssl_, ret);
            switch (err) {
            case SSL_ERROR_WANT_WRITE: {
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                break;
            }
            case SSL_ERROR_WANT_READ: {
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                auto rr = co_await fill_rbio();
                if (!rr) co_return std::unexpected(rr.error());
                break;
            }
            default:
                co_return std::unexpected(make_ssl_error(err));
            }
        }
    }

    /// Async TLS shutdown
    auto async_shutdown() -> task<std::expected<void, std::error_code>> {
        // SSL_shutdown needs to be called twice: send close_notify + receive close_notify
        for (int attempt = 0; attempt < 2; ++attempt) {
            int ret = SSL_shutdown(ssl_);
            if (ret == 1) {
                // Fully closed
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                co_return {};
            }
            if (ret == 0) {
                // Sent close_notify, waiting for peer's
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                auto rr = co_await fill_rbio();
                if (!rr) co_return std::unexpected(rr.error());
                continue;
            }

            int err = SSL_get_error(ssl_, ret);
            switch (err) {
            case SSL_ERROR_WANT_WRITE: {
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                --attempt; // Retry
                break;
            }
            case SSL_ERROR_WANT_READ: {
                auto fr = co_await flush_wbio();
                if (!fr) co_return std::unexpected(fr.error());
                auto rr = co_await fill_rbio();
                if (!rr) co_return std::unexpected(rr.error());
                --attempt; // Retry
                break;
            }
            default:
                // Errors during shutdown can usually be ignored
                co_return {};
            }
        }
        co_return {};
    }

    /// Get ALPN-negotiated protocol after handshake
    [[nodiscard]] auto get_alpn_selected() const noexcept -> std::string_view {
        const unsigned char* data = nullptr;
        unsigned int len = 0;
        SSL_get0_alpn_selected(ssl_, &data, &len);
        if (data && len > 0) {
            return {reinterpret_cast<const char*>(data), len};
        }
        return {};
    }

    /// Get native SSL pointer
    [[nodiscard]] auto native() const noexcept -> SSL* { return ssl_; }

private:
    // =========================================================================
    // BIO flush/fill helper coroutines
    // =========================================================================

    /// Write encrypted data from wbio_ to socket
    auto flush_wbio() -> task<std::expected<void, std::error_code>> {
        char tmp[8192];
        for (;;) {
            auto pending = static_cast<int>(BIO_ctrl_pending(wbio_));
            if (pending <= 0) break;

            int n = BIO_read(wbio_, tmp, std::min(pending, static_cast<int>(sizeof(tmp))));
            if (n <= 0) break;

            // Write all to socket
            std::size_t written = 0;
            while (written < static_cast<std::size_t>(n)) {
                auto w = co_await cnetmod::async_write(io_ctx_, sock_,
                    const_buffer{tmp + written, static_cast<std::size_t>(n) - written});
                if (!w) co_return std::unexpected(w.error());
                written += *w;
            }
        }
        co_return {};
    }

    /// Read data from socket and write to rbio_
    auto fill_rbio() -> task<std::expected<void, std::error_code>> {
        std::array<std::byte, 8192> tmp{};
        auto r = co_await cnetmod::async_read(io_ctx_, sock_,
            mutable_buffer{tmp.data(), tmp.size()});
        if (!r) co_return std::unexpected(r.error());
        if (*r == 0) {
            co_return std::unexpected(
                std::make_error_code(std::errc::connection_reset));
        }
        BIO_write(rbio_, tmp.data(), static_cast<int>(*r));
        co_return {};
    }

    io_context& io_ctx_;
    socket&     sock_;
    SSL*        ssl_  = nullptr;
    BIO*        rbio_ = nullptr;  // Network -> SSL (receive direction)
    BIO*        wbio_ = nullptr;  // SSL -> Network (send direction)
};

#endif // CNETMOD_HAS_SSL

} // namespace cnetmod
