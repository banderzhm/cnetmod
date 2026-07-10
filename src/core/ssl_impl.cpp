module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_SSL
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#endif

module cnetmod.core.ssl;

import std;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.core.socket;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;

namespace cnetmod {

#ifdef CNETMOD_HAS_SSL

namespace detail {

void ssl_global_init() noexcept {
    static const bool initialized = [] {
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS |
                             OPENSSL_INIT_LOAD_CRYPTO_STRINGS,
                         nullptr);
        return true;
    }();
    (void)initialized;
}

auto ssl_error_category_impl::name() const noexcept -> const char* {
    return "openssl";
}

auto ssl_error_category_impl::message(int ev) const -> std::string {
    if (ev == 0) {
        return "success";
    }

    char buffer[256]{};
    ERR_error_string_n(static_cast<unsigned long>(ev), buffer, sizeof(buffer));
    return buffer;
}

auto ssl_category() -> const std::error_category& {
    static const ssl_error_category_impl instance;
    return instance;
}

} // namespace detail

auto make_ssl_error() -> std::error_code {
    const auto error = ERR_get_error();
    if (error == 0) {
        return {};
    }
    return {static_cast<int>(error), detail::ssl_category()};
}

auto make_ssl_error(int ssl_err) -> std::error_code {
    const auto error = ERR_get_error();
    return {error == 0 ? ssl_err : static_cast<int>(error),
            detail::ssl_category()};
}

ssl_context::~ssl_context() {
    if (ctx_) {
        SSL_CTX_free(ctx_);
    }
}

ssl_context::ssl_context(ssl_context&& other) noexcept
    : ctx_(std::exchange(other.ctx_, nullptr))
    , alpn_wire_(std::move(other.alpn_wire_)) {
    if (ctx_ && !alpn_wire_.empty()) {
        SSL_CTX_set_alpn_select_cb(ctx_, alpn_select_cb, &alpn_wire_);
    }
}

auto ssl_context::operator=(ssl_context&& other) noexcept -> ssl_context& {
    if (this == &other) {
        return *this;
    }

    if (ctx_) {
        SSL_CTX_free(ctx_);
    }
    ctx_ = std::exchange(other.ctx_, nullptr);
    alpn_wire_ = std::move(other.alpn_wire_);
    if (ctx_ && !alpn_wire_.empty()) {
        SSL_CTX_set_alpn_select_cb(ctx_, alpn_select_cb, &alpn_wire_);
    }
    return *this;
}

auto ssl_context::client() -> std::expected<ssl_context, std::error_code> {
    detail::ssl_global_init();
    auto* context = SSL_CTX_new(TLS_client_method());
    if (!context) {
        return std::unexpected(make_ssl_error());
    }

    SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION);
    return ssl_context{context};
}

auto ssl_context::server() -> std::expected<ssl_context, std::error_code> {
    detail::ssl_global_init();
    auto* context = SSL_CTX_new(TLS_server_method());
    if (!context) {
        return std::unexpected(make_ssl_error());
    }

    SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION);
    return ssl_context{context};
}

auto ssl_context::dtls_client() -> std::expected<ssl_context, std::error_code> {
    detail::ssl_global_init();
    auto* context = SSL_CTX_new(DTLS_client_method());
    if (!context) {
        return std::unexpected(make_ssl_error());
    }

    SSL_CTX_set_min_proto_version(context, DTLS1_2_VERSION);
    return ssl_context{context};
}

auto ssl_context::dtls_server() -> std::expected<ssl_context, std::error_code> {
    detail::ssl_global_init();
    auto* context = SSL_CTX_new(DTLS_server_method());
    if (!context) {
        return std::unexpected(make_ssl_error());
    }

    SSL_CTX_set_min_proto_version(context, DTLS1_2_VERSION);
    SSL_CTX_set_cookie_generate_cb(context, dtls_generate_cookie);
    SSL_CTX_set_cookie_verify_cb(context, dtls_verify_cookie);
    return ssl_context{context};
}

auto ssl_context::load_cert_file(std::string_view path)
    -> std::expected<void, std::error_code> {
    const std::string value(path);
    if (SSL_CTX_use_certificate_chain_file(ctx_, value.c_str()) != 1) {
        return std::unexpected(make_ssl_error());
    }
    return {};
}

auto ssl_context::load_key_file(std::string_view path)
    -> std::expected<void, std::error_code> {
    const std::string value(path);
    if (SSL_CTX_use_PrivateKey_file(ctx_, value.c_str(), SSL_FILETYPE_PEM) != 1) {
        return std::unexpected(make_ssl_error());
    }
    return {};
}

auto ssl_context::load_ca_file(std::string_view path)
    -> std::expected<void, std::error_code> {
    const std::string value(path);
    if (SSL_CTX_load_verify_locations(ctx_, value.c_str(), nullptr) != 1) {
        return std::unexpected(make_ssl_error());
    }
    return {};
}

auto ssl_context::set_default_ca() -> std::expected<void, std::error_code> {
    if (SSL_CTX_set_default_verify_paths(ctx_) != 1) {
        return std::unexpected(make_ssl_error());
    }
    return {};
}

void ssl_context::set_verify_peer(bool verify) noexcept {
    SSL_CTX_set_verify(ctx_, verify ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);
}

void ssl_context::set_require_peer_certificate(bool require) noexcept {
    SSL_CTX_set_verify(ctx_,
                       require ? SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT
                               : SSL_VERIFY_NONE,
                       nullptr);
}

void ssl_context::configure_alpn_server(
    std::initializer_list<std::string_view> protocols) {
    alpn_wire_.clear();
    for (const auto protocol : protocols) {
        alpn_wire_.push_back(static_cast<unsigned char>(protocol.size()));
        alpn_wire_.insert(
            alpn_wire_.end(),
            reinterpret_cast<const unsigned char*>(protocol.data()),
            reinterpret_cast<const unsigned char*>(protocol.data() + protocol.size()));
    }
    SSL_CTX_set_alpn_select_cb(ctx_, alpn_select_cb, &alpn_wire_);
}

void ssl_context::configure_alpn_client(
    std::initializer_list<std::string_view> protocols) {
    std::vector<unsigned char> wire;
    for (const auto protocol : protocols) {
        wire.push_back(static_cast<unsigned char>(protocol.size()));
        wire.insert(
            wire.end(),
            reinterpret_cast<const unsigned char*>(protocol.data()),
            reinterpret_cast<const unsigned char*>(protocol.data() + protocol.size()));
    }
    SSL_CTX_set_alpn_protos(ctx_, wire.data(), static_cast<unsigned int>(wire.size()));
}

auto ssl_context::alpn_select_cb(
    SSL*, const unsigned char** out, unsigned char* outlen,
    const unsigned char* in, unsigned int inlen, void* arg) -> int {
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

auto ssl_context::dtls_generate_cookie(
    SSL*, unsigned char* cookie, unsigned int* cookie_len) -> int {
    static constexpr std::string_view value = "cnetmod-dtls-cookie";
    std::memcpy(cookie, value.data(), value.size());
    *cookie_len = static_cast<unsigned int>(value.size());
    return 1;
}

auto ssl_context::dtls_verify_cookie(
    SSL*, const unsigned char* cookie, unsigned int cookie_len) -> int {
    static constexpr std::string_view value = "cnetmod-dtls-cookie";
    return cookie_len == value.size() &&
           std::memcmp(cookie, value.data(), value.size()) == 0;
}

ssl_stream::ssl_stream(ssl_context& context, io_context& io, socket& socket)
    : io_ctx_(io)
    , sock_(socket)
    , ssl_(SSL_new(context.native())) {
    rbio_ = BIO_new(BIO_s_mem());
    wbio_ = BIO_new(BIO_s_mem());
    SSL_set_bio(ssl_, rbio_, wbio_);
}

ssl_stream::~ssl_stream() {
    if (ssl_) {
        SSL_free(ssl_);
    }
}

ssl_stream::ssl_stream(ssl_stream&& other) noexcept
    : io_ctx_(other.io_ctx_)
    , sock_(other.sock_)
    , ssl_(std::exchange(other.ssl_, nullptr))
    , rbio_(std::exchange(other.rbio_, nullptr))
    , wbio_(std::exchange(other.wbio_, nullptr)) {}

void ssl_stream::set_hostname(std::string_view hostname) {
    const std::string value(hostname);
    auto* parameter = SSL_get0_param(ssl_);
    if (const auto ip = ip_address::from_string(value)) {
        const auto literal = ip->to_string();
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
        X509_VERIFY_PARAM_set1_ip_asc(parameter, literal.c_str());
#else
        X509_VERIFY_PARAM_set1_host(parameter, literal.c_str(), literal.size());
#endif
        return;
    }

    SSL_set_tlsext_host_name(ssl_, value.c_str());
    X509_VERIFY_PARAM_set1_host(parameter, value.c_str(), value.size());
}

void ssl_stream::set_connect_state() noexcept {
    SSL_set_connect_state(ssl_);
}

void ssl_stream::set_accept_state() noexcept {
    SSL_set_accept_state(ssl_);
}

auto ssl_stream::async_handshake()
    -> task<std::expected<void, std::error_code>> {
    for (;;) {
        const int ret = SSL_do_handshake(ssl_);
        if (ret == 1) {
            auto flushed = co_await flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            co_return {};
        }

        switch (const int error = SSL_get_error(ssl_, ret)) {
        case SSL_ERROR_WANT_WRITE: {
            auto flushed = co_await flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            break;
        }
        case SSL_ERROR_WANT_READ: {
            auto flushed = co_await flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await fill_rbio();
            if (!filled) {
                co_return std::unexpected(filled.error());
            }
            break;
        }
        default:
            co_return std::unexpected(make_ssl_error(error));
        }
    }
}

auto ssl_stream::async_read(mutable_buffer buffer)
    -> task<std::expected<std::size_t, std::error_code>> {
    for (;;) {
        const int ret = SSL_read(ssl_, buffer.data, static_cast<int>(buffer.size));
        if (ret > 0) {
            co_return static_cast<std::size_t>(ret);
        }

        switch (const int error = SSL_get_error(ssl_, ret)) {
        case SSL_ERROR_WANT_READ: {
            auto flushed = co_await flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await fill_rbio();
            if (!filled) {
                co_return std::unexpected(filled.error());
            }
            break;
        }
        case SSL_ERROR_WANT_WRITE: {
            auto flushed = co_await flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            break;
        }
        case SSL_ERROR_ZERO_RETURN:
            co_return static_cast<std::size_t>(0);
        default:
            co_return std::unexpected(make_ssl_error(error));
        }
    }
}

auto ssl_stream::async_write(const_buffer buffer)
    -> task<std::expected<std::size_t, std::error_code>> {
    for (;;) {
        const int ret = SSL_write(ssl_, buffer.data, static_cast<int>(buffer.size));
        if (ret > 0) {
            auto flushed = co_await flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            co_return static_cast<std::size_t>(ret);
        }

        switch (const int error = SSL_get_error(ssl_, ret)) {
        case SSL_ERROR_WANT_WRITE: {
            auto flushed = co_await flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            break;
        }
        case SSL_ERROR_WANT_READ: {
            auto flushed = co_await flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await fill_rbio();
            if (!filled) {
                co_return std::unexpected(filled.error());
            }
            break;
        }
        default:
            co_return std::unexpected(make_ssl_error(error));
        }
    }
}

auto ssl_stream::async_write_all(const_buffer buffer)
    -> task<std::expected<void, std::error_code>> {
    const auto* data = static_cast<const std::byte*>(buffer.data);
    std::size_t written = 0;
    while (written < buffer.size) {
        auto result = co_await async_write(
            const_buffer{data + written, buffer.size - written});
        if (!result) {
            co_return std::unexpected(result.error());
        }
        if (*result == 0) {
            co_return std::unexpected(make_error_code(errc::broken_pipe));
        }
        written += *result;
    }
    co_return {};
}

auto ssl_stream::async_shutdown()
    -> task<std::expected<void, std::error_code>> {
    for (int attempt = 0; attempt < 2; ++attempt) {
        const int ret = SSL_shutdown(ssl_);
        if (ret == 1) {
            auto flushed = co_await flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            co_return {};
        }
        if (ret == 0) {
            auto flushed = co_await flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await fill_rbio();
            if (!filled) {
                co_return std::unexpected(filled.error());
            }
            continue;
        }

        switch (const int error = SSL_get_error(ssl_, ret)) {
        case SSL_ERROR_WANT_WRITE: {
            auto flushed = co_await flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            --attempt;
            break;
        }
        case SSL_ERROR_WANT_READ: {
            auto flushed = co_await flush_wbio();
            if (!flushed) {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await fill_rbio();
            if (!filled) {
                co_return std::unexpected(filled.error());
            }
            --attempt;
            break;
        }
        default:
            co_return {};
        }
    }
    co_return {};
}

auto ssl_stream::get_alpn_selected() const noexcept -> std::string_view {
    const unsigned char* data = nullptr;
    unsigned int length = 0;
    SSL_get0_alpn_selected(ssl_, &data, &length);
    return data && length
               ? std::string_view{reinterpret_cast<const char*>(data), length}
               : std::string_view{};
}

auto ssl_stream::native() const noexcept -> SSL* {
    return ssl_;
}

auto ssl_stream::flush_wbio()
    -> task<std::expected<void, std::error_code>> {
    char buffer[8192];
    for (;;) {
        const int pending = static_cast<int>(BIO_ctrl_pending(wbio_));
        if (pending <= 0) {
            break;
        }

        const int read = BIO_read(
            wbio_, buffer, std::min(pending, static_cast<int>(sizeof(buffer))));
        if (read <= 0) {
            break;
        }

        auto written = co_await cnetmod::async_write_all(
            io_ctx_, sock_, const_buffer{buffer, static_cast<std::size_t>(read)});
        if (!written) {
            co_return std::unexpected(written.error());
        }
    }
    co_return {};
}

auto ssl_stream::fill_rbio()
    -> task<std::expected<void, std::error_code>> {
    std::array<std::byte, 8192> buffer{};
    auto read = co_await cnetmod::async_read(
        io_ctx_, sock_, mutable_buffer{buffer.data(), buffer.size()});
    if (!read) {
        co_return std::unexpected(read.error());
    }
    if (*read == 0) {
        co_return std::unexpected(
            std::make_error_code(std::errc::connection_reset));
    }

    BIO_write(rbio_, buffer.data(), static_cast<int>(*read));
    co_return {};
}

#endif // CNETMOD_HAS_SSL

} // namespace cnetmod
