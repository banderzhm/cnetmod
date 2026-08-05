module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_SSL
    #include <openssl/bio.h>
    #include <openssl/err.h>
    #include <openssl/ssl.h>
    #include <openssl/x509.h>
    #include <cnetmod/detail/boringssl_compat.hpp>
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

    void ssl_global_init() noexcept
    {
        static const bool initialized = []
        {
            OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS |
                    OPENSSL_INIT_LOAD_CRYPTO_STRINGS,
                nullptr);
            return true;
        }();
        (void)initialized;
    }

    auto ssl_error_category_impl::name() const noexcept -> const char*
    {
        return "openssl";
    }

    auto ssl_error_category_impl::message(int ev) const -> std::string
    {
        if (ev == 0)
        {
            return "success";
        }

        char buffer[256]{};
        ERR_error_string_n(static_cast<unsigned long>(ev), buffer, sizeof(buffer));
        return buffer;
    }

    auto ssl_category() -> const std::error_category&
    {
        static const ssl_error_category_impl instance;
        return instance;
    }

} // namespace detail

namespace {

    #if defined(CNETMOD_PLATFORM_LINUX)
    auto wait_for_ssl_socket(io_context& context, socket& socket, bool writable)
        -> task<std::expected<void, std::error_code>>
    {
        if (writable)
        {
            co_return co_await async_wait_writable(context, socket);
        }
        co_return co_await async_wait_readable(context, socket);
    }
    #endif

} // namespace

auto make_ssl_error() -> std::error_code
{
    const auto error = ERR_get_error();
    if (error == 0)
    {
        return {};
    }
    return {static_cast<int>(error), detail::ssl_category()};
}

auto make_ssl_error(int ssl_err) -> std::error_code
{
    const auto error = ERR_get_error();
    return {error == 0 ? ssl_err : static_cast<int>(error),
        detail::ssl_category()};
}

    #ifdef CNETMOD_ENABLE_QUIC
struct ssl_context::ticket_aead_state
{
    ssl_ticket_aead_callbacks callbacks;
};

namespace {

    auto ticket_aead_ex_index() -> int
    {
        static const int index = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
        return index;
    }

    auto ticket_aead_state_for(SSL* ssl) noexcept -> ssl_context::ticket_aead_state*
    {
        auto* context = SSL_get_SSL_CTX(ssl);
        return context == nullptr ? nullptr : static_cast<ssl_context::ticket_aead_state*>(SSL_CTX_get_ex_data(context, ticket_aead_ex_index()));
    }

    auto ticket_aead_max_overhead(SSL* ssl) -> std::size_t
    {
        const auto* state = ticket_aead_state_for(ssl);
        return state == nullptr ? 0U : state->callbacks.max_overhead;
    }

    auto ticket_aead_seal(SSL* ssl, std::uint8_t* out, std::size_t* out_len,
        std::size_t max_out_len, const std::uint8_t* in, std::size_t in_len) -> int
    {
        const auto* state = ticket_aead_state_for(ssl);
        if (state == nullptr || !state->callbacks.seal)
            return 0;
        try
        {
            const auto sealed = state->callbacks.seal(std::as_bytes(std::span{in, in_len}));
            if (!sealed || sealed->size() > max_out_len)
                return 0;
            std::memcpy(out, sealed->data(), sealed->size());
            *out_len = sealed->size();
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    auto ticket_aead_open(SSL* ssl, std::uint8_t* out, std::size_t* out_len,
        std::size_t max_out_len, const std::uint8_t* in, std::size_t in_len)
        -> ssl_ticket_aead_result_t
    {
        const auto* state = ticket_aead_state_for(ssl);
        if (state == nullptr || !state->callbacks.open)
            return ssl_ticket_aead_ignore_ticket;
        try
        {
            const auto opened = state->callbacks.open(std::as_bytes(std::span{in, in_len}));
            if (!opened || opened->plaintext.size() > max_out_len)
                return ssl_ticket_aead_ignore_ticket;

            // A ticket may still resume a normal 1-RTT handshake repeatedly. It
            // becomes single-use only when BoringSSL has actually entered the
            // early-data path, so ordinary session resumption is not consumed.
            if (SSL_in_early_data(ssl) == 1 &&
                (!state->callbacks.consume_early_data || opened->identity.empty() ||
                    !state->callbacks.consume_early_data(opened->identity,
                        opened->early_data_expires_at)))
            {
                return ssl_ticket_aead_ignore_ticket;
            }

            std::memcpy(out, opened->plaintext.data(), opened->plaintext.size());
            *out_len = opened->plaintext.size();
            return ssl_ticket_aead_success;
        }
        catch (...)
        {
            return ssl_ticket_aead_ignore_ticket;
        }
    }

    const SSL_TICKET_AEAD_METHOD ticket_aead_method{
        ticket_aead_max_overhead,
        ticket_aead_seal,
        ticket_aead_open,
    };

} // namespace

auto ssl_context::configure_ticket_aead(ssl_ticket_aead_callbacks callbacks)
    -> std::expected<void, std::error_code>
{
    if (ctx_ == nullptr || !callbacks.seal || !callbacks.open ||
        callbacks.max_overhead == 0)
    {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    auto state = std::make_shared<ticket_aead_state>();
    state->callbacks = std::move(callbacks);
    if (SSL_CTX_set_ex_data(ctx_, ticket_aead_ex_index(), state.get()) != 1)
        return std::unexpected(make_ssl_error());

    ticket_aead_state_ = std::move(state);
    SSL_CTX_set_ticket_aead_method(ctx_, &ticket_aead_method);
    return {};
}
    #endif

ssl_context::~ssl_context()
{
    if (ctx_)
    {
        SSL_CTX_free(ctx_);
    }
}

ssl_context::ssl_context(ssl_context&& other) noexcept
    : ctx_(std::exchange(other.ctx_, nullptr)), alpn_wire_(std::move(other.alpn_wire_))
    #ifdef CNETMOD_ENABLE_QUIC
      ,
      ticket_aead_state_(std::move(other.ticket_aead_state_))
    #endif
      ,
      kernel_tls_enabled_(std::exchange(other.kernel_tls_enabled_, false))
{
    if (ctx_ && !alpn_wire_.empty())
    {
        SSL_CTX_set_alpn_select_cb(ctx_, alpn_select_cb, &alpn_wire_);
    }
}

auto ssl_context::operator=(ssl_context&& other) noexcept -> ssl_context&
{
    if (this == &other)
    {
        return *this;
    }

    if (ctx_)
    {
        SSL_CTX_free(ctx_);
    }
    ctx_ = std::exchange(other.ctx_, nullptr);
    alpn_wire_ = std::move(other.alpn_wire_);
    #ifdef CNETMOD_ENABLE_QUIC
    ticket_aead_state_ = std::move(other.ticket_aead_state_);
    #endif
    kernel_tls_enabled_ = std::exchange(other.kernel_tls_enabled_, false);
    if (ctx_ && !alpn_wire_.empty())
    {
        SSL_CTX_set_alpn_select_cb(ctx_, alpn_select_cb, &alpn_wire_);
    }
    return *this;
}

auto ssl_context::client() -> std::expected<ssl_context, std::error_code>
{
    detail::ssl_global_init();
    auto* context = SSL_CTX_new(TLS_client_method());
    if (!context)
    {
        return std::unexpected(make_ssl_error());
    }

    SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION);
    auto result = ssl_context{context};
    result.set_kernel_tls(true);
    return result;
}

auto ssl_context::server() -> std::expected<ssl_context, std::error_code>
{
    detail::ssl_global_init();
    auto* context = SSL_CTX_new(TLS_server_method());
    if (!context)
    {
        return std::unexpected(make_ssl_error());
    }

    SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION);
    auto result = ssl_context{context};
    result.set_kernel_tls(true);
    return result;
}

auto ssl_context::dtls_client() -> std::expected<ssl_context, std::error_code>
{
    detail::ssl_global_init();
    auto* context = SSL_CTX_new(DTLS_client_method());
    if (!context)
    {
        return std::unexpected(make_ssl_error());
    }

    SSL_CTX_set_min_proto_version(context, DTLS1_2_VERSION);
    return ssl_context{context};
}

auto ssl_context::dtls_server() -> std::expected<ssl_context, std::error_code>
{
    detail::ssl_global_init();
    auto* context = SSL_CTX_new(DTLS_server_method());
    if (!context)
    {
        return std::unexpected(make_ssl_error());
    }

    SSL_CTX_set_min_proto_version(context, DTLS1_2_VERSION);
    // BoringSSL does not expose OpenSSL's DTLS cookie callback API.
    #if !defined(CNETMOD_USING_BORINGSSL)
    SSL_CTX_set_cookie_generate_cb(context, dtls_generate_cookie);
    SSL_CTX_set_cookie_verify_cb(context, dtls_verify_cookie);
    #endif
    return ssl_context{context};
}

    #ifdef CNETMOD_ENABLE_QUIC
auto ssl_context::quic_client()
    -> std::expected<ssl_context, std::error_code>
{
    detail::ssl_global_init();
    auto* context = SSL_CTX_new(TLS_client_method());
    if (!context)
    {
        return std::unexpected(make_ssl_error());
    }

    // QUIC requires TLS 1.3 only
    SSL_CTX_set_min_proto_version(context, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(context, TLS1_3_VERSION);

    auto result = ssl_context{context};
    result.set_kernel_tls(false); // kTLS not supported with QUIC
    return result;
}

auto ssl_context::quic_server()
    -> std::expected<ssl_context, std::error_code>
{
    detail::ssl_global_init();
    auto* context = SSL_CTX_new(TLS_server_method());
    if (!context)
    {
        return std::unexpected(make_ssl_error());
    }

    // QUIC requires TLS 1.3 only
    SSL_CTX_set_min_proto_version(context, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(context, TLS1_3_VERSION);

    auto result = ssl_context{context};
    result.set_kernel_tls(false); // kTLS not supported with QUIC
    return result;
}
    #endif

auto ssl_context::load_cert_file(std::string_view path)
    -> std::expected<void, std::error_code>
{
    const std::string value(path);
    if (SSL_CTX_use_certificate_chain_file(ctx_, value.c_str()) != 1)
    {
        return std::unexpected(make_ssl_error());
    }
    return {};
}

auto ssl_context::load_key_file(std::string_view path)
    -> std::expected<void, std::error_code>
{
    const std::string value(path);
    if (SSL_CTX_use_PrivateKey_file(ctx_, value.c_str(), SSL_FILETYPE_PEM) != 1)
    {
        return std::unexpected(make_ssl_error());
    }
    return {};
}

auto ssl_context::load_ca_file(std::string_view path)
    -> std::expected<void, std::error_code>
{
    const std::string value(path);
    if (SSL_CTX_load_verify_locations(ctx_, value.c_str(), nullptr) != 1)
    {
        return std::unexpected(make_ssl_error());
    }
    return {};
}

auto ssl_context::set_default_ca() -> std::expected<void, std::error_code>
{
    if (SSL_CTX_set_default_verify_paths(ctx_) != 1)
    {
        return std::unexpected(make_ssl_error());
    }
    return {};
}

void ssl_context::set_verify_peer(bool verify) noexcept
{
    SSL_CTX_set_verify(ctx_, verify ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);
}

void ssl_context::set_kernel_tls(bool enabled) noexcept
{
    #if defined(CNETMOD_PLATFORM_LINUX) && defined(SSL_OP_ENABLE_KTLS) && !defined(OPENSSL_NO_KTLS)
    kernel_tls_enabled_ = enabled;
    if (enabled)
    {
        SSL_CTX_set_options(ctx_, SSL_OP_ENABLE_KTLS);
    }
    else
    {
        SSL_CTX_clear_options(ctx_, SSL_OP_ENABLE_KTLS);
    }
    #else
    (void)enabled;
    kernel_tls_enabled_ = false;
    #endif
}

void ssl_context::set_require_peer_certificate(bool require) noexcept
{
    SSL_CTX_set_verify(ctx_,
        require ? SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT
                : SSL_VERIFY_NONE,
        nullptr);
}

void ssl_context::configure_alpn_server(
    std::initializer_list<std::string_view> protocols)
{
    alpn_wire_.clear();
    for (const auto protocol : protocols)
    {
        alpn_wire_.push_back(static_cast<unsigned char>(protocol.size()));
        alpn_wire_.insert(
            alpn_wire_.end(),
            reinterpret_cast<const unsigned char*>(protocol.data()),
            reinterpret_cast<const unsigned char*>(protocol.data() + protocol.size()));
    }
    SSL_CTX_set_alpn_select_cb(ctx_, alpn_select_cb, &alpn_wire_);
}

void ssl_context::configure_alpn_client(
    std::initializer_list<std::string_view> protocols)
{
    std::vector<unsigned char> wire;
    for (const auto protocol : protocols)
    {
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
    const unsigned char* in, unsigned int inlen, void* arg) -> int
{
    auto* server_protos = static_cast<std::vector<unsigned char>*>(arg);
    unsigned char* selected = nullptr;
    unsigned char selected_len = 0;
    if (SSL_select_next_proto(
            &selected, &selected_len,
            server_protos->data(),
            static_cast<unsigned int>(server_protos->size()),
            in, inlen) != OPENSSL_NPN_NEGOTIATED)
    {
        return SSL_TLSEXT_ERR_NOACK;
    }

    *out = selected;
    *outlen = selected_len;
    return SSL_TLSEXT_ERR_OK;
}

auto ssl_context::dtls_generate_cookie(
    SSL*, unsigned char* cookie, unsigned int* cookie_len) -> int
{
    static constexpr std::string_view value = "cnetmod-dtls-cookie";
    std::memcpy(cookie, value.data(), value.size());
    *cookie_len = static_cast<unsigned int>(value.size());
    return 1;
}

auto ssl_context::dtls_verify_cookie(
    SSL*, const unsigned char* cookie, unsigned int cookie_len) -> int
{
    static constexpr std::string_view value = "cnetmod-dtls-cookie";
    return cookie_len == value.size() &&
        std::memcmp(cookie, value.data(), value.size()) == 0;
}

ssl_stream::ssl_stream(ssl_context& context, io_context& io, socket& socket)
    : io_ctx_(io), sock_(socket), ssl_(SSL_new(context.native()))
{
    // OpenSSL can install the Linux TLS ULP only while doing the handshake on
    // socket BIOs. Memory BIOs remain the portable fallback used everywhere
    // else, including Windows/IOCP.
    #if defined(CNETMOD_PLATFORM_LINUX) && defined(SSL_OP_ENABLE_KTLS) && !defined(OPENSSL_NO_KTLS)
    if (context.kernel_tls_enabled())
    {
        SSL_set_options(ssl_, SSL_OP_ENABLE_KTLS);
        rbio_ = BIO_new_socket(static_cast<int>(sock_.native_handle()), BIO_NOCLOSE);
        wbio_ = BIO_new_socket(static_cast<int>(sock_.native_handle()), BIO_NOCLOSE);
        if (rbio_ && wbio_)
        {
            direct_socket_bio_ = true;
            SSL_set_bio(ssl_, rbio_, wbio_);
            return;
        }
        if (rbio_)
            BIO_free(rbio_);
        if (wbio_)
            BIO_free(wbio_);
    }
    #endif
    rbio_ = BIO_new(BIO_s_mem());
    wbio_ = BIO_new(BIO_s_mem());
    SSL_set_bio(ssl_, rbio_, wbio_);
}

ssl_stream::~ssl_stream()
{
    if (ssl_)
    {
        SSL_free(ssl_);
    }
}

ssl_stream::ssl_stream(ssl_stream&& other) noexcept
    : io_ctx_(other.io_ctx_), sock_(other.sock_), ssl_(std::exchange(other.ssl_, nullptr)), rbio_(std::exchange(other.rbio_, nullptr)), wbio_(std::exchange(other.wbio_, nullptr)), direct_socket_bio_(std::exchange(other.direct_socket_bio_, false)), kernel_tls_active_(std::exchange(other.kernel_tls_active_, false)) {}

void ssl_stream::set_hostname(std::string_view hostname)
{
    const std::string value(hostname);
    auto* parameter = SSL_get0_param(ssl_);
    if (const auto ip = ip_address::from_string(value))
    {
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

void ssl_stream::set_connect_state() noexcept
{
    SSL_set_connect_state(ssl_);
}

void ssl_stream::set_accept_state() noexcept
{
    SSL_set_accept_state(ssl_);
}

auto ssl_stream::async_handshake()
    -> task<std::expected<void, std::error_code>>
{
    for (;;)
    {
        const int ret = SSL_do_handshake(ssl_);
        if (ret == 1)
        {
            if (!direct_socket_bio_)
            {
                auto flushed = co_await flush_wbio();
                if (!flushed)
                {
                    co_return std::unexpected(flushed.error());
                }
            }
    #if defined(CNETMOD_PLATFORM_LINUX) && defined(SSL_OP_ENABLE_KTLS) && !defined(OPENSSL_NO_KTLS)
            kernel_tls_active_ = direct_socket_bio_ && BIO_get_ktls_send(SSL_get_wbio(ssl_)) == 1;
    #endif
            co_return {};
        }

        switch (const int error = SSL_get_error(ssl_, ret))
        {
        case SSL_ERROR_WANT_WRITE:
        {
            if (direct_socket_bio_)
            {
    #if defined(CNETMOD_PLATFORM_LINUX)
                auto ready = co_await wait_for_ssl_socket(io_ctx_, sock_, true);
                if (!ready)
                    co_return std::unexpected(ready.error());
                break;
    #endif
            }
            auto flushed = co_await flush_wbio();
            if (!flushed)
            {
                co_return std::unexpected(flushed.error());
            }
            break;
        }
        case SSL_ERROR_WANT_READ:
        {
            if (direct_socket_bio_)
            {
    #if defined(CNETMOD_PLATFORM_LINUX)
                auto ready = co_await wait_for_ssl_socket(io_ctx_, sock_, false);
                if (!ready)
                    co_return std::unexpected(ready.error());
                break;
    #endif
            }
            auto flushed = co_await flush_wbio();
            if (!flushed)
            {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await fill_rbio();
            if (!filled)
            {
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
    -> task<std::expected<std::size_t, std::error_code>>
{
    for (;;)
    {
        const int ret = SSL_read(ssl_, buffer.data, static_cast<int>(buffer.size));
        if (ret > 0)
        {
            co_return static_cast<std::size_t>(ret);
        }

        switch (const int error = SSL_get_error(ssl_, ret))
        {
        case SSL_ERROR_WANT_READ:
        {
            if (direct_socket_bio_)
            {
    #if defined(CNETMOD_PLATFORM_LINUX)
                auto ready = co_await wait_for_ssl_socket(io_ctx_, sock_, false);
                if (!ready)
                    co_return std::unexpected(ready.error());
                break;
    #endif
            }
            auto flushed = co_await flush_wbio();
            if (!flushed)
            {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await fill_rbio();
            if (!filled)
            {
                co_return std::unexpected(filled.error());
            }
            break;
        }
        case SSL_ERROR_WANT_WRITE:
        {
            if (direct_socket_bio_)
            {
    #if defined(CNETMOD_PLATFORM_LINUX)
                auto ready = co_await wait_for_ssl_socket(io_ctx_, sock_, true);
                if (!ready)
                    co_return std::unexpected(ready.error());
                break;
    #endif
            }
            auto flushed = co_await flush_wbio();
            if (!flushed)
            {
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
    -> task<std::expected<std::size_t, std::error_code>>
{
    for (;;)
    {
        const int ret = SSL_write(ssl_, buffer.data, static_cast<int>(buffer.size));
        if (ret > 0)
        {
            if (!direct_socket_bio_)
            {
                auto flushed = co_await flush_wbio();
                if (!flushed)
                {
                    co_return std::unexpected(flushed.error());
                }
            }
            co_return static_cast<std::size_t>(ret);
        }

        switch (const int error = SSL_get_error(ssl_, ret))
        {
        case SSL_ERROR_WANT_WRITE:
        {
            if (direct_socket_bio_)
            {
    #if defined(CNETMOD_PLATFORM_LINUX)
                auto ready = co_await wait_for_ssl_socket(io_ctx_, sock_, true);
                if (!ready)
                    co_return std::unexpected(ready.error());
                break;
    #endif
            }
            auto flushed = co_await flush_wbio();
            if (!flushed)
            {
                co_return std::unexpected(flushed.error());
            }
            break;
        }
        case SSL_ERROR_WANT_READ:
        {
            if (direct_socket_bio_)
            {
    #if defined(CNETMOD_PLATFORM_LINUX)
                auto ready = co_await wait_for_ssl_socket(io_ctx_, sock_, false);
                if (!ready)
                    co_return std::unexpected(ready.error());
                break;
    #endif
            }
            auto flushed = co_await flush_wbio();
            if (!flushed)
            {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await fill_rbio();
            if (!filled)
            {
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
    -> task<std::expected<void, std::error_code>>
{
    const auto* data = static_cast<const std::byte*>(buffer.data);
    std::size_t written = 0;
    while (written < buffer.size)
    {
        auto result = co_await async_write(
            const_buffer{data + written, buffer.size - written});
        if (!result)
        {
            co_return std::unexpected(result.error());
        }
        if (*result == 0)
        {
            co_return std::unexpected(make_error_code(errc::broken_pipe));
        }
        written += *result;
    }
    co_return {};
}

auto ssl_stream::async_shutdown()
    -> task<std::expected<void, std::error_code>>
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const int ret = SSL_shutdown(ssl_);
        if (ret == 1)
        {
            if (!direct_socket_bio_)
            {
                auto flushed = co_await flush_wbio();
                if (!flushed)
                    co_return std::unexpected(flushed.error());
            }
            co_return {};
        }
        if (ret == 0)
        {
            if (direct_socket_bio_)
            {
    #if defined(CNETMOD_PLATFORM_LINUX)
                auto ready = co_await wait_for_ssl_socket(io_ctx_, sock_, false);
                if (!ready)
                    co_return std::unexpected(ready.error());
                continue;
    #endif
            }
            auto flushed = co_await flush_wbio();
            if (!flushed)
            {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await fill_rbio();
            if (!filled)
            {
                co_return std::unexpected(filled.error());
            }
            continue;
        }

        switch (SSL_get_error(ssl_, ret))
        {
        case SSL_ERROR_WANT_WRITE:
        {
            if (direct_socket_bio_)
            {
    #if defined(CNETMOD_PLATFORM_LINUX)
                auto ready = co_await wait_for_ssl_socket(io_ctx_, sock_, true);
                if (!ready)
                    co_return std::unexpected(ready.error());
                --attempt;
                break;
    #endif
            }
            auto flushed = co_await flush_wbio();
            if (!flushed)
            {
                co_return std::unexpected(flushed.error());
            }
            --attempt;
            break;
        }
        case SSL_ERROR_WANT_READ:
        {
            if (direct_socket_bio_)
            {
    #if defined(CNETMOD_PLATFORM_LINUX)
                auto ready = co_await wait_for_ssl_socket(io_ctx_, sock_, false);
                if (!ready)
                    co_return std::unexpected(ready.error());
                --attempt;
                break;
    #endif
            }
            auto flushed = co_await flush_wbio();
            if (!flushed)
            {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await fill_rbio();
            if (!filled)
            {
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

auto ssl_stream::get_alpn_selected() const noexcept -> std::string_view
{
    const unsigned char* data = nullptr;
    unsigned int length = 0;
    SSL_get0_alpn_selected(ssl_, &data, &length);
    return data && length
        ? std::string_view{reinterpret_cast<const char*>(data), length}
        : std::string_view{};
}

auto ssl_stream::kernel_tls_active() const noexcept -> bool
{
    return kernel_tls_active_;
}

auto ssl_stream::native() const noexcept -> SSL*
{
    return ssl_;
}

auto ssl_stream::flush_wbio()
    -> task<std::expected<void, std::error_code>>
{
    if (direct_socket_bio_)
    {
        // The socket BIO has already handed encrypted records to the kernel.
        co_return {};
    }
    for (;;)
    {
        char* encrypted = nullptr;
        const auto pending = BIO_get_mem_data(wbio_, &encrypted);
        if (pending <= 0)
        {
            break;
        }
        auto written = co_await cnetmod::async_write_all(
            io_ctx_, sock_,
            const_buffer{encrypted, static_cast<std::size_t>(pending)});
        if (!written)
        {
            co_return std::unexpected(written.error());
        }
        if (BIO_reset(wbio_) != 1)
        {
            co_return std::unexpected(make_ssl_error(SSL_ERROR_SSL));
        }
    }
    co_return {};
}

auto ssl_stream::fill_rbio()
    -> task<std::expected<void, std::error_code>>
{
    if (direct_socket_bio_)
    {
    #if defined(CNETMOD_PLATFORM_LINUX)
        co_return co_await wait_for_ssl_socket(io_ctx_, sock_, false);
    #else
        co_return std::unexpected(make_error_code(std::errc::not_supported));
    #endif
    }
    std::array<std::byte, 8192> buffer{};
    auto read = co_await cnetmod::async_read(
        io_ctx_, sock_, mutable_buffer{buffer.data(), buffer.size()});
    if (!read)
    {
        co_return std::unexpected(read.error());
    }
    if (*read == 0)
    {
        co_return std::unexpected(
            std::make_error_code(std::errc::connection_reset));
    }

    BIO_write(rbio_, buffer.data(), static_cast<int>(*read));
    co_return {};
}

#endif // CNETMOD_HAS_SSL

} // namespace cnetmod
