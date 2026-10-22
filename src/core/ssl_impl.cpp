module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_SSL
    #include <openssl/bio.h>
    #include <openssl/err.h>
    #include <openssl/ssl.h>
    #include <openssl/x509.h>
#endif
#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_PLATFORM_WINDOWS)
    #include <windows.h>
#endif

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_PLATFORM_WINDOWS)
    #include <wincrypt.h>
#endif
#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_PLATFORM_LINUX)
    #include <cerrno>
    #include <sys/socket.h>
#endif

module cnetmod.core.ssl;

import std;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.core.socket;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;

namespace cnetmod {

#ifdef CNETMOD_HAS_SSL

namespace detail {

    #if defined(CNETMOD_PLATFORM_LINUX) && defined(CNETMOD_USING_BORINGSSL)
    auto enable_kernel_tls_tx(SSL* ssl, socket& transport) noexcept -> bool;
    auto async_kernel_tls_shutdown(io_context& context, socket& transport)
        -> task<std::expected<void, std::error_code>>;
    auto async_kernel_tls_shutdown(io_context& context, socket& transport,
        cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>;
    #endif

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

    #if defined(CNETMOD_PLATFORM_WINDOWS)
    auto load_windows_root_certificates(SSL_CTX* context)
        -> std::expected<void, std::error_code>
    {
        auto* certificate_store = ::CertOpenSystemStoreW(0, L"ROOT");
        if (certificate_store == nullptr)
        {
            return std::unexpected(std::error_code{
                static_cast<int>(::GetLastError()), std::system_category()});
        }

        auto* target_store = SSL_CTX_get_cert_store(context);
        PCCERT_CONTEXT certificate = nullptr;
        std::size_t imported = 0;
        while ((certificate = ::CertEnumCertificatesInStore(
                    certificate_store, certificate)) != nullptr)
        {
            const auto* encoded = certificate->pbCertEncoded;
            auto* parsed = d2i_X509(
                nullptr, &encoded, certificate->cbCertEncoded);
            if (parsed == nullptr)
            {
                ERR_clear_error();
                continue;
            }

            if (X509_STORE_add_cert(target_store, parsed) == 1)
                ++imported;
            else
                ERR_clear_error();
            X509_free(parsed);
        }
        ::CertCloseStore(certificate_store, 0);

        if (imported == 0)
            return std::unexpected(
                std::make_error_code(std::errc::no_such_file_or_directory));
        return {};
    }
    #endif

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

    auto wait_for_ssl_socket(io_context& context, socket& socket, bool writable,
        cancel_token& token) -> task<std::expected<void, std::error_code>>
    {
        if (writable)
            co_return co_await async_wait_writable(context, socket, token);
        co_return co_await async_wait_readable(context, socket, token);
    }
    #endif

    // Byte FIFO backing the transport BIOs of ssl_stream. The library memory
    // BIO zeroes its whole capacity on every BIO_reset and memmoves the unread
    // tail after every partial BIO_read; for a stream that flushes large
    // responses that is a full extra pass over each record. This queue only
    // moves offsets, keeps its capacity across flushes and compacts lazily.
    // It carries TLS ciphertext, so no cleansing is required on release.
    class byte_queue
    {
    public:
        byte_queue() = default;
        byte_queue(const byte_queue&) = delete;
        auto operator=(const byte_queue&) -> byte_queue& = delete;

        ~byte_queue()
        {
            ::operator delete(data_, std::nothrow);
        }

        [[nodiscard]] auto readable() const noexcept -> std::span<const std::byte>
        {
            return {data_ + begin_, end_ - begin_};
        }

        auto append(const std::byte* bytes, std::size_t size) noexcept -> bool
        {
            if (size > capacity_ - end_ && !make_room(size))
                return false;
            std::memcpy(data_ + end_, bytes, size);
            end_ += size;
            return true;
        }

        auto read(std::byte* out, std::size_t size) noexcept -> std::size_t
        {
            const auto count = std::min(size, end_ - begin_);
            std::memcpy(out, data_ + begin_, count);
            consume(count);
            return count;
        }

        /// Socket the owner may write to directly; null keeps every record
        /// queued. The handle is read at each write, never cached, so a
        /// closed socket whose descriptor number was reused is not written.
        const cnetmod::socket* direct_socket = nullptr;

        void consume(std::size_t size) noexcept
        {
            begin_ += std::min(size, end_ - begin_);
            if (begin_ != end_)
                return;
            begin_ = end_ = 0;
            // Keep the storage for the next record batch, but do not let one
            // unusually large response pin its peak footprint per connection.
            if (capacity_ > retained_capacity)
            {
                ::operator delete(data_, std::nothrow);
                data_ = nullptr;
                capacity_ = 0;
            }
        }

    private:
        static constexpr std::size_t retained_capacity = 256U * 1024U;

        auto make_room(std::size_t size) noexcept -> bool
        {
            const auto used = end_ - begin_;
            if (size > std::numeric_limits<std::size_t>::max() - used)
                return false;
            const auto required = used + size;
            if (required <= capacity_ && begin_ >= used)
            {
                // Compact only when the unread bytes are no larger than the
                // consumed prefix, so the move stays amortized and disjoint.
                std::memcpy(data_, data_ + begin_, used);
                begin_ = 0;
                end_ = used;
                return true;
            }
            auto capacity = std::max<std::size_t>(capacity_, 16U * 1024U);
            while (capacity < required)
                capacity = capacity > std::numeric_limits<std::size_t>::max() / 2U
                    ? required
                    : capacity * 2U;
            auto* grown = static_cast<std::byte*>(::operator new(capacity, std::nothrow));
            if (grown == nullptr)
                return false;
            if (used != 0)
                std::memcpy(grown, data_ + begin_, used);
            ::operator delete(data_, std::nothrow);
            data_ = grown;
            capacity_ = capacity;
            begin_ = 0;
            end_ = used;
            return true;
        }

        std::byte* data_ = nullptr;
        std::size_t begin_ = 0;
        std::size_t end_ = 0;
        std::size_t capacity_ = 0;
    };

    auto queue_of(BIO* bio) noexcept -> byte_queue*
    {
        return static_cast<byte_queue*>(BIO_get_data(bio));
    }

    auto queue_bio_create(BIO* bio) -> int
    {
        auto* queue = new (std::nothrow) byte_queue;
        if (queue == nullptr)
            return 0;
        BIO_set_data(bio, queue);
        BIO_set_init(bio, 1);
        return 1;
    }

    auto queue_bio_destroy(BIO* bio) -> int
    {
        delete queue_of(bio);
        BIO_set_data(bio, nullptr);
        return 1;
    }

    /// Records at least this large are sent from the BIO, together with
    /// whatever is queued ahead of them: the ciphertext then crosses to the
    /// kernel once instead of being copied into the queue first. Smaller
    /// records keep batching so one gathered write carries a burst of frames.
    constexpr std::size_t direct_send_threshold = 8U * 1024U;

    auto queue_bio_write(BIO* bio, const char* in, int size) -> int
    {
        BIO_clear_retry_flags(bio);
        if (size <= 0)
            return 0;
        auto* const queue = queue_of(bio);
        auto bytes = reinterpret_cast<const std::byte*>(in);
        auto count = static_cast<std::size_t>(size);
        #if defined(CNETMOD_PLATFORM_LINUX)
        // The stream serializes SSL_write and the flush that follows it, and
        // the queue is sent first below, so the order of bytes on the wire is
        // preserved.
        if (queue->direct_socket != nullptr && count >= direct_send_threshold &&
            queue->direct_socket->is_open())
        {
            // Records queued ahead of this one (typically a small HTTP/2
            // HEADERS record) go out in the same call, in order, followed by
            // this record straight from the caller's buffer.
            const auto queued = queue->readable();
            iovec vectors[2];
            vectors[0] = {const_cast<std::byte*>(queued.data()), queued.size()};
            vectors[1] = {const_cast<std::byte*>(bytes), count};
            msghdr message{};
            message.msg_iov = vectors;
            message.msg_iovlen = 2;
            const auto sent = ::sendmsg(
                static_cast<int>(queue->direct_socket->native_handle()), &message,
                MSG_DONTWAIT | MSG_NOSIGNAL);
            if (sent > 0)
            {
                auto remaining = static_cast<std::size_t>(sent);
                const auto from_queue = std::min(remaining, queued.size());
                queue->consume(from_queue);
                remaining -= from_queue;
                bytes += remaining;
                count -= remaining;
            }
            // EAGAIN or an error: queue the record; the asynchronous write
            // that follows reports a real socket failure.
        }
        #endif
        if (count == 0)
            return size;
        return queue->append(bytes, count) ? size : -1;
    }

    auto queue_bio_read(BIO* bio, char* out, int size) -> int
    {
        BIO_clear_retry_flags(bio);
        if (size <= 0)
            return 0;
        const auto count = queue_of(bio)->read(
            reinterpret_cast<std::byte*>(out), static_cast<std::size_t>(size));
        if (count != 0)
            return static_cast<int>(count);
        // Same contract as a memory BIO with eof_return -1: an empty queue
        // means "no ciphertext yet", surfaced to SSL as SSL_ERROR_WANT_READ.
        BIO_set_retry_read(bio);
        return -1;
    }

    auto queue_bio_ctrl(BIO* bio, int command, long, void*) -> long
    {
        switch (command)
        {
        case BIO_CTRL_PENDING:
            return static_cast<long>(queue_of(bio)->readable().size());
        case BIO_CTRL_WPENDING:
            return 0;
        case BIO_CTRL_FLUSH:
            return 1;
        default:
            return 0;
        }
    }

    struct queue_bio_registration
    {
        BIO_METHOD* method = nullptr;
        int type = 0;
    };

    auto queue_bio() noexcept -> const queue_bio_registration&
    {
        static const queue_bio_registration registration = []() noexcept
        {
            const auto index = BIO_get_new_index();
            // A private type number lets the stream recognise its own BIOs;
            // without one the fallback memory BIO path stays in use.
            if (index <= 0)
                return queue_bio_registration{};
            const int type = index | BIO_TYPE_SOURCE_SINK;
            auto* created = BIO_meth_new(type, "cnetmod-queue");
            if (created == nullptr)
                return queue_bio_registration{};
            if (BIO_meth_set_create(created, queue_bio_create) != 1 ||
                BIO_meth_set_destroy(created, queue_bio_destroy) != 1 ||
                BIO_meth_set_write(created, queue_bio_write) != 1 ||
                BIO_meth_set_read(created, queue_bio_read) != 1 ||
                BIO_meth_set_ctrl(created, queue_bio_ctrl) != 1)
            {
                BIO_meth_free(created);
                return queue_bio_registration{};
            }
            return queue_bio_registration{created, type};
        }();
        return registration;
    }

    auto is_queue_bio(BIO* bio) noexcept -> bool
    {
        const auto& registration = queue_bio();
        return registration.method != nullptr && bio != nullptr &&
            BIO_method_type(bio) == registration.type;
    }

    auto new_transport_bio() noexcept -> BIO*
    {
        if (const auto* method = queue_bio().method)
        {
            if (auto* bio = BIO_new(method))
                return bio;
        }
        return BIO_new(BIO_s_mem());
    }

    /// Ciphertext waiting in the outbound transport BIO.
    auto pending_output(BIO* bio) noexcept -> std::span<const std::byte>
    {
        if (is_queue_bio(bio))
            return queue_of(bio)->readable();
        char* data = nullptr;
        const auto size = BIO_get_mem_data(bio, &data);
        return size <= 0
            ? std::span<const std::byte>{}
            : std::span<const std::byte>{reinterpret_cast<const std::byte*>(data),
                  static_cast<std::size_t>(size)};
    }

    /// Drop `size` flushed bytes from the outbound transport BIO.
    auto consume_output(BIO* bio, std::size_t size) noexcept -> bool
    {
        if (is_queue_bio(bio))
        {
            queue_of(bio)->consume(size);
            return true;
        }
        // A memory BIO cannot drop a prefix; callers flush it completely.
        return BIO_reset(bio) == 1;
    }

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

            // BoringSSL invokes ticket AEAD open while selecting the session,
            // before SSL_in_early_data() exposes the final offer/acceptance
            // state.  Do not mistake that transient false value for a 1-RTT
            // resume: accepting the ticket first and trying to consume it
            // later leaves a replay window.  Consume every successful
            // application-owned resumption ticket instead.  This is the
            // conservative, auditable policy: a normal resumption also uses
            // one ticket, while the server can safely accept at most one
            // 0-RTT attempt for that identity.
            if (!state->callbacks.consume_early_data || opened->identity.empty() ||
                !state->callbacks.consume_early_data(opened->identity,
                    opened->early_data_expires_at))
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
    return ssl_context{context};
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
    return ssl_context{context};
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
    #if defined(CNETMOD_PLATFORM_WINDOWS)
    return load_windows_root_certificates(ctx_);
    #else
    if (SSL_CTX_set_default_verify_paths(ctx_) != 1)
    {
        return std::unexpected(make_ssl_error());
    }
    return {};
    #endif
}

void ssl_context::set_verify_peer(bool verify) noexcept
{
    SSL_CTX_set_verify(ctx_, verify ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);
}

void ssl_context::set_kernel_tls(bool enabled) noexcept
{
    #if defined(CNETMOD_PLATFORM_LINUX) && defined(CNETMOD_USING_BORINGSSL)
    kernel_tls_enabled_ = enabled;
    // BoringSSL does not expose its record keys for TLS 1.3. Its supported
    // TLS 1.2 AES-GCM state is exported to Linux TLS ULP after the handshake.
    // Make that protocol choice explicit only when the application requests
    // kTLS; the normal context continues to negotiate TLS 1.3.
    SSL_CTX_set_max_proto_version(ctx_, enabled ? TLS1_2_VERSION : 0);
    #elif defined(CNETMOD_PLATFORM_LINUX) && defined(SSL_OP_ENABLE_KTLS) && !defined(OPENSSL_NO_KTLS)
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
    : io_ctx_(io), sock_(socket), ssl_(SSL_new(context.native())),
      kernel_tls_requested_(context.kernel_tls_enabled())
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
    rbio_ = new_transport_bio();
    wbio_ = new_transport_bio();
    SSL_set_bio(ssl_, rbio_, wbio_);
    #if defined(CNETMOD_PLATFORM_LINUX)
    if (is_queue_bio(wbio_))
        queue_of(wbio_)->direct_socket = std::addressof(sock_);
    #endif
}

ssl_stream::~ssl_stream()
{
    if (ssl_)
    {
        SSL_free(ssl_);
    }
}

ssl_stream::ssl_stream(ssl_stream&& other) noexcept
    : io_ctx_(other.io_ctx_), sock_(other.sock_),
      ssl_(std::exchange(other.ssl_, nullptr)),
      rbio_(std::exchange(other.rbio_, nullptr)),
      wbio_(std::exchange(other.wbio_, nullptr)),
      direct_socket_bio_(std::exchange(other.direct_socket_bio_, false)),
      kernel_tls_requested_(
          std::exchange(other.kernel_tls_requested_, false)),
      kernel_tls_active_(std::exchange(other.kernel_tls_active_, false))
{
}

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
    cancel_token token;
    co_return co_await async_handshake(token);
}

auto ssl_stream::async_handshake(cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    for (;;)
    {
        const int ret = SSL_do_handshake(ssl_);
        if (ret == 1)
        {
            if (!direct_socket_bio_)
            {
                auto flushed = co_await flush_wbio(token);
                if (!flushed)
                {
                    co_return std::unexpected(flushed.error());
                }
            }
    #if defined(CNETMOD_PLATFORM_LINUX) && defined(CNETMOD_USING_BORINGSSL)
            if (!direct_socket_bio_ && kernel_tls_requested_ &&
                !kernel_tls_active_)
            {
                kernel_tls_active_ =
                    detail::enable_kernel_tls_tx(ssl_, sock_);
            }
    #endif
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
                auto ready = co_await wait_for_ssl_socket(io_ctx_, sock_, true, token);
                if (!ready)
                    co_return std::unexpected(ready.error());
                break;
    #endif
            }
            auto flushed = co_await flush_wbio(token);
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
                auto ready = co_await wait_for_ssl_socket(io_ctx_, sock_, false, token);
                if (!ready)
                    co_return std::unexpected(ready.error());
                break;
    #endif
            }
            auto flushed = co_await flush_wbio(token);
            if (!flushed)
            {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await fill_rbio(token);
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
    #ifdef CNETMOD_PLATFORM_WINDOWS
    // The no-token API must remain on the no-token IOCP path. Creating a
    // throwaway cancel_token here forces every TLS record through cancellation
    // registration and several atomic stores even though the caller cannot
    // cancel it. Windows always uses memory BIOs, so this loop has no direct
    // socket-BIO readiness branch.
    for (;;)
    {
        co_await engine_mutex_.lock();
        const int ret = SSL_read(ssl_, buffer.data, static_cast<int>(buffer.size));
        if (ret > 0)
        {
            engine_mutex_.unlock();
            co_return static_cast<std::size_t>(ret);
        }

        switch (const int error = SSL_get_error(ssl_, ret))
        {
        case SSL_ERROR_WANT_READ:
        {
            auto flushed = co_await flush_wbio();
            engine_mutex_.unlock();
            if (!flushed)
                co_return std::unexpected(flushed.error());
            auto filled = co_await fill_rbio();
            if (!filled)
                co_return std::unexpected(filled.error());
            break;
        }
        case SSL_ERROR_WANT_WRITE:
        {
            auto flushed = co_await flush_wbio();
            engine_mutex_.unlock();
            if (!flushed)
                co_return std::unexpected(flushed.error());
            break;
        }
        case SSL_ERROR_ZERO_RETURN:
            engine_mutex_.unlock();
            co_return static_cast<std::size_t>(0);
        default:
            engine_mutex_.unlock();
            co_return std::unexpected(make_ssl_error(error));
        }
    }
    #else
    cancel_token token;
    co_return co_await async_read(buffer, token);
    #endif
}

auto ssl_stream::async_read(mutable_buffer buffer, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    for (;;)
    {
        co_await engine_mutex_.lock();
        const int ret = SSL_read(ssl_, buffer.data, static_cast<int>(buffer.size));
        if (ret > 0)
        {
            engine_mutex_.unlock();
            co_return static_cast<std::size_t>(ret);
        }

        switch (const int error = SSL_get_error(ssl_, ret))
        {
        case SSL_ERROR_WANT_READ:
        {
            if (direct_socket_bio_)
            {
                engine_mutex_.unlock();
    #if defined(CNETMOD_PLATFORM_LINUX)
                auto ready = co_await wait_for_ssl_socket(io_ctx_, sock_, false, token);
                if (!ready)
                    co_return std::unexpected(ready.error());
                break;
    #endif
            }
            auto flushed = co_await flush_wbio(token);
            engine_mutex_.unlock();
            if (!flushed)
            {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await fill_rbio(token);
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
                engine_mutex_.unlock();
    #if defined(CNETMOD_PLATFORM_LINUX)
                auto ready = co_await wait_for_ssl_socket(io_ctx_, sock_, true, token);
                if (!ready)
                    co_return std::unexpected(ready.error());
                break;
    #endif
            }
            auto flushed = co_await flush_wbio(token);
            engine_mutex_.unlock();
            if (!flushed)
            {
                co_return std::unexpected(flushed.error());
            }
            break;
        }
        case SSL_ERROR_ZERO_RETURN:
            engine_mutex_.unlock();
            co_return static_cast<std::size_t>(0);
        default:
            engine_mutex_.unlock();
            co_return std::unexpected(make_ssl_error(error));
        }
    }
}

auto ssl_stream::async_write(const_buffer buffer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    #ifdef CNETMOD_PLATFORM_WINDOWS
    for (;;)
    {
        co_await engine_mutex_.lock();
        const int ret = SSL_write(ssl_, buffer.data, static_cast<int>(buffer.size));
        if (ret > 0)
        {
            auto flushed = co_await flush_wbio();
            if (!flushed)
            {
                engine_mutex_.unlock();
                co_return std::unexpected(flushed.error());
            }
            engine_mutex_.unlock();
            co_return static_cast<std::size_t>(ret);
        }

        switch (const int error = SSL_get_error(ssl_, ret))
        {
        case SSL_ERROR_WANT_WRITE:
        {
            auto flushed = co_await flush_wbio();
            engine_mutex_.unlock();
            if (!flushed)
                co_return std::unexpected(flushed.error());
            break;
        }
        case SSL_ERROR_WANT_READ:
        {
            auto flushed = co_await flush_wbio();
            engine_mutex_.unlock();
            if (!flushed)
                co_return std::unexpected(flushed.error());
            auto filled = co_await fill_rbio();
            if (!filled)
                co_return std::unexpected(filled.error());
            break;
        }
        default:
            engine_mutex_.unlock();
            co_return std::unexpected(make_ssl_error(error));
        }
    }
    #else
    cancel_token token;
    co_return co_await async_write(buffer, token);
    #endif
}

auto ssl_stream::async_write(const_buffer buffer, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    #if defined(CNETMOD_PLATFORM_LINUX) && defined(CNETMOD_USING_BORINGSSL)
    if (kernel_tls_active_)
    {
        co_return co_await cnetmod::async_write(io_ctx_, sock_, buffer, token);
    }
    #endif
    for (;;)
    {
        co_await engine_mutex_.lock();
        const int ret = SSL_write(ssl_, buffer.data, static_cast<int>(buffer.size));
        if (ret > 0)
        {
            if (!direct_socket_bio_)
            {
                auto flushed = co_await flush_wbio(token);
                if (!flushed)
                {
                    engine_mutex_.unlock();
                    co_return std::unexpected(flushed.error());
                }
            }
            engine_mutex_.unlock();
            co_return static_cast<std::size_t>(ret);
        }

        switch (const int error = SSL_get_error(ssl_, ret))
        {
        case SSL_ERROR_WANT_WRITE:
        {
            if (direct_socket_bio_)
            {
                engine_mutex_.unlock();
    #if defined(CNETMOD_PLATFORM_LINUX)
                auto ready = co_await wait_for_ssl_socket(io_ctx_, sock_, true, token);
                if (!ready)
                    co_return std::unexpected(ready.error());
                break;
    #endif
            }
            auto flushed = co_await flush_wbio(token);
            engine_mutex_.unlock();
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
                engine_mutex_.unlock();
    #if defined(CNETMOD_PLATFORM_LINUX)
                auto ready = co_await wait_for_ssl_socket(io_ctx_, sock_, false, token);
                if (!ready)
                    co_return std::unexpected(ready.error());
                break;
    #endif
            }
            auto flushed = co_await flush_wbio(token);
            engine_mutex_.unlock();
            if (!flushed)
            {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await fill_rbio(token);
            if (!filled)
            {
                co_return std::unexpected(filled.error());
            }
            break;
        }
        default:
            engine_mutex_.unlock();
            co_return std::unexpected(make_ssl_error(error));
        }
    }
}

auto ssl_stream::async_write_all(const_buffer buffer)
    -> task<std::expected<void, std::error_code>>
{
    #if defined(CNETMOD_PLATFORM_LINUX) && defined(CNETMOD_USING_BORINGSSL)
    if (kernel_tls_active_)
    {
        const auto* data = static_cast<const std::byte*>(buffer.data);
        std::size_t written = 0;
        while (written < buffer.size)
        {
            auto result = co_await cnetmod::async_write(io_ctx_, sock_,
                const_buffer{data + written, buffer.size - written});
            if (!result)
                co_return std::unexpected(result.error());
            if (*result == 0)
                co_return std::unexpected(make_error_code(errc::broken_pipe));
            written += *result;
        }
        co_return {};
    }
    #endif

    #ifdef CNETMOD_PLATFORM_WINDOWS
    const auto* data = static_cast<const std::byte*>(buffer.data);
    std::size_t written = 0;
    while (written < buffer.size)
    {
        auto result = co_await async_write(
            const_buffer{data + written, buffer.size - written});
        if (!result)
            co_return std::unexpected(result.error());
        if (*result == 0)
            co_return std::unexpected(make_error_code(errc::broken_pipe));
        written += *result;
    }
    co_return {};
    #else
    cancel_token token;
    co_return co_await async_write_all(buffer, token);
    #endif
}

auto ssl_stream::async_write_all(const_buffer buffer, cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    const auto* data = static_cast<const std::byte*>(buffer.data);
    std::size_t written = 0;
    #if defined(CNETMOD_PLATFORM_LINUX) && defined(CNETMOD_USING_BORINGSSL)
    if (kernel_tls_active_)
    {
        while (written < buffer.size)
        {
            auto result = co_await cnetmod::async_write(io_ctx_, sock_,
                const_buffer{data + written, buffer.size - written}, token);
            if (!result)
                co_return std::unexpected(result.error());
            if (*result == 0)
                co_return std::unexpected(make_error_code(errc::broken_pipe));
            written += *result;
        }
        co_return {};
    }
    #endif

    while (written < buffer.size)
    {
        auto result = co_await async_write(
            const_buffer{data + written, buffer.size - written}, token);
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

auto ssl_stream::async_write_gather(std::span<const const_buffer> buffers,
    cancel_token& token) -> task<std::expected<void, std::error_code>>
{
    if (kernel_tls_active_ || direct_socket_bio_)
    {
        // The kernel (or the socket BIO) already frames each write.
        for (const auto& buffer : buffers)
        {
            auto written = co_await async_write_all(buffer, token);
            if (!written)
                co_return written;
        }
        co_return std::expected<void, std::error_code>{};
    }

    co_await engine_mutex_.lock();
    std::size_t index = 0;
    std::size_t offset = 0;
    // A memory/queue BIO accepts every record, so SSL_write either consumes
    // the whole chunk or reports a real error. Seal everything first.
    while (index < buffers.size())
    {
        const auto& buffer = buffers[index];
        const auto remaining = buffer.size - offset;
        if (remaining == 0)
        {
            ++index;
            offset = 0;
            continue;
        }
        const auto chunk = static_cast<int>(std::min<std::size_t>(remaining,
            static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const int ret = SSL_write(ssl_,
            static_cast<const std::byte*>(buffer.data) + offset, chunk);
        if (ret <= 0)
            break;
        offset += static_cast<std::size_t>(ret);
    }
    auto flushed = co_await flush_wbio(token);
    engine_mutex_.unlock();
    if (!flushed)
        co_return std::unexpected(flushed.error());
    // Anything SSL could not seal in one pass (renegotiation-style retries or
    // a fatal error) takes the general path, which reports the error.
    for (; index < buffers.size(); ++index, offset = 0)
    {
        const auto& buffer = buffers[index];
        if (buffer.size <= offset)
            continue;
        auto written = co_await async_write_all(
            const_buffer{static_cast<const std::byte*>(buffer.data) + offset,
                buffer.size - offset},
            token);
        if (!written)
            co_return written;
    }
    co_return std::expected<void, std::error_code>{};
}

template <typename Cancellation>
auto ssl_stream::shutdown_impl(Cancellation cancellation)
    -> task<std::expected<void, std::error_code>>
{
    #if defined(CNETMOD_PLATFORM_LINUX) && defined(CNETMOD_USING_BORINGSSL)
    if (kernel_tls_active_)
    {
        // TLS_TX owns record framing; the platform implementation emits a
        // close_notify record without feeding ciphertext back through kTLS.
        if constexpr (std::is_same_v<Cancellation, std::nullptr_t>)
            co_return co_await detail::async_kernel_tls_shutdown(
                io_ctx_, sock_);
        else
            co_return co_await detail::async_kernel_tls_shutdown(
                io_ctx_, sock_, *cancellation);
    }
    #endif

    auto flush = [&]()
    {
        if constexpr (std::is_same_v<Cancellation, std::nullptr_t>)
            return flush_wbio();
        else
            return flush_wbio(*cancellation);
    };
    auto fill = [&]()
    {
        if constexpr (std::is_same_v<Cancellation, std::nullptr_t>)
            return fill_rbio();
        else
            return fill_rbio(*cancellation);
    };
    #if defined(CNETMOD_PLATFORM_LINUX)
    auto ready_for = [&](bool writable)
    {
        if constexpr (std::is_same_v<Cancellation, std::nullptr_t>)
            return wait_for_ssl_socket(io_ctx_, sock_, writable);
        else
            return wait_for_ssl_socket(io_ctx_, sock_, writable, *cancellation);
    };
    #endif
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        if constexpr (!std::is_same_v<Cancellation, std::nullptr_t>)
        {
            if (cancellation->is_cancelled())
                co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        }
        const int ret = SSL_shutdown(ssl_);
        if (ret == 1)
        {
            if (!direct_socket_bio_)
            {
                auto flushed = co_await flush();
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
                auto ready = co_await ready_for(false);
                if (!ready)
                    co_return std::unexpected(ready.error());
                continue;
    #endif
            }
            auto flushed = co_await flush();
            if (!flushed)
            {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await fill();
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
                auto ready = co_await ready_for(true);
                if (!ready)
                    co_return std::unexpected(ready.error());
                --attempt;
                break;
    #endif
            }
            auto flushed = co_await flush();
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
                auto ready = co_await ready_for(false);
                if (!ready)
                    co_return std::unexpected(ready.error());
                --attempt;
                break;
    #endif
            }
            auto flushed = co_await flush();
            if (!flushed)
            {
                co_return std::unexpected(flushed.error());
            }
            auto filled = co_await fill();
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

auto ssl_stream::async_shutdown() -> task<std::expected<void, std::error_code>>
{
    return shutdown_impl(nullptr);
}

auto ssl_stream::async_shutdown(cancel_token& token) -> task<std::expected<void, std::error_code>>
{
    return shutdown_impl(&token);
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
    #ifdef CNETMOD_PLATFORM_WINDOWS
    for (;;)
    {
        const auto pending = pending_output(wbio_);
        if (pending.empty())
            break;
        auto written = co_await cnetmod::async_write_all(
            io_ctx_, sock_, const_buffer{pending.data(), pending.size()});
        if (!written)
            co_return std::unexpected(written.error());
        if (!consume_output(wbio_, pending.size()))
            co_return std::unexpected(make_ssl_error(SSL_ERROR_SSL));
    }
    co_return {};
    #else
    cancel_token token;
    co_return co_await flush_wbio(token);
    #endif
}

auto ssl_stream::flush_wbio(cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    if (direct_socket_bio_)
    {
        // The socket BIO has already handed encrypted records to the kernel.
        co_return {};
    }
    for (;;)
    {
        const auto pending = pending_output(wbio_);
        if (pending.empty())
        {
            break;
        }
        auto written = co_await cnetmod::async_write_all(
            io_ctx_, sock_, const_buffer{pending.data(), pending.size()}, token);
        if (!written)
        {
            co_return std::unexpected(written.error());
        }
        if (!consume_output(wbio_, pending.size()))
        {
            co_return std::unexpected(make_ssl_error(SSL_ERROR_SSL));
        }
    }
    co_return {};
}

auto ssl_stream::fill_rbio()
    -> task<std::expected<void, std::error_code>>
{
    #ifdef CNETMOD_PLATFORM_WINDOWS
    // Only the completed range reaches BIO_write. Avoid both cancellation
    // registration and needless initialization for this record scratch area.
    std::array<std::byte, 8192> buffer;
    auto read = co_await cnetmod::async_read(
        io_ctx_, sock_, mutable_buffer{buffer.data(), buffer.size()});
    if (!read)
        co_return std::unexpected(read.error());
    if (*read == 0)
    {
        co_return std::unexpected(
            std::make_error_code(std::errc::connection_reset));
    }
    BIO_write(rbio_, buffer.data(), static_cast<int>(*read));
    co_return {};
    #else
    cancel_token token;
    co_return co_await fill_rbio(token);
    #endif
}

auto ssl_stream::fill_rbio(cancel_token& token)
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
    // async_read initializes exactly the returned byte range before it is
    // passed to BIO_write below. Clearing the complete scratch buffer first
    // would be pure write traffic for the usual small-record TLS path.
    std::array<std::byte, 8192> buffer;
    auto read = co_await cnetmod::async_read(
        io_ctx_, sock_, mutable_buffer{buffer.data(), buffer.size()}, token);
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
