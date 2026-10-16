module;

#include <cnetmod/config.hpp>

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_PLATFORM_LINUX) && \
    defined(CNETMOD_USING_BORINGSSL)
    #include <cerrno>
    #include <linux/tls.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <openssl/hmac.h>
    #include <openssl/ssl.h>
    #include <sys/socket.h>
#endif

module cnetmod.core.ssl;

import std;
import cnetmod.core.address;
import cnetmod.core.socket;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;

namespace cnetmod::detail {

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_PLATFORM_LINUX) && \
    defined(CNETMOD_USING_BORINGSSL)
namespace {
    constexpr std::size_t tls_random_size = 32U;
    constexpr std::size_t tls_master_secret_size = 48U;
    constexpr std::size_t tls12_aes128_key_block_size = 40U;
    constexpr std::size_t tls12_aes256_key_block_size = 72U;

    struct cleanse_on_exit
    {
        void* data{};
        std::size_t size{};

        ~cleanse_on_exit()
        {
            OPENSSL_cleanse(data, size);
        }
    };

    template <std::size_t DigestSize>
    auto hmac_digest(const EVP_MD* digest,
        std::span<const std::uint8_t> key,
        std::span<const std::uint8_t> data,
        std::span<std::uint8_t, DigestSize> output) noexcept -> bool
    {
        unsigned int output_size = 0;
        return HMAC(digest, key.data(), key.size(), data.data(), data.size(),
                   output.data(), &output_size) != nullptr &&
            output_size == output.size();
    }

    template <std::size_t DigestSize>
    auto tls12_prf(const EVP_MD* digest,
        std::span<const std::uint8_t> secret,
        std::span<const std::uint8_t> seed,
        std::span<std::uint8_t> output) noexcept -> bool
    {
        std::array<std::uint8_t, DigestSize> a{};
        cleanse_on_exit cleanse_a{a.data(), a.size()};
        if (!hmac_digest<DigestSize>(digest, secret, seed,
                std::span<std::uint8_t, DigestSize>{a}))
            return false;

        std::array<std::uint8_t,
            DigestSize + 13U + tls_random_size * 2U> input{};
        cleanse_on_exit cleanse_input{input.data(), input.size()};
        std::size_t offset = 0;
        while (offset < output.size())
        {
            std::memcpy(input.data(), a.data(), a.size());
            std::memcpy(input.data() + a.size(), seed.data(), seed.size());

            std::array<std::uint8_t, DigestSize> block{};
            cleanse_on_exit cleanse_block{block.data(), block.size()};
            if (!hmac_digest<DigestSize>(digest, secret,
                    std::span<const std::uint8_t>{input.data(),
                        a.size() + seed.size()},
                    std::span<std::uint8_t, DigestSize>{block}))
            {
                return false;
            }
            const auto count = std::min(block.size(), output.size() - offset);
            std::memcpy(output.data() + offset, block.data(), count);
            offset += count;

            std::array<std::uint8_t, DigestSize> next{};
            cleanse_on_exit cleanse_next{next.data(), next.size()};
            if (!hmac_digest<DigestSize>(digest, secret,
                    std::span<const std::uint8_t>{a},
                    std::span<std::uint8_t, DigestSize>{next}))
                return false;
            a = next;
        }
        return true;
    }

    template <typename Cancellation>
    auto send_close_notify(io_context& context, socket& transport,
        Cancellation cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        const std::array<std::uint8_t, 2U> alert{1U, 0U};
        for (;;)
        {
            if constexpr (!std::is_same_v<Cancellation, std::nullptr_t>)
            {
                if (cancellation->is_cancelled())
                    co_return std::unexpected(
                        std::make_error_code(std::errc::operation_canceled));
            }

            iovec payload{
                .iov_base = const_cast<std::uint8_t*>(alert.data()),
                .iov_len = alert.size(),
            };
            std::array<std::byte, CMSG_SPACE(sizeof(std::uint8_t))> control{};
            msghdr message{};
            message.msg_iov = &payload;
            message.msg_iovlen = 1;
            message.msg_control = control.data();
            message.msg_controllen = control.size();
            auto* header = CMSG_FIRSTHDR(&message);
            header->cmsg_level = SOL_TLS;
            header->cmsg_type = TLS_SET_RECORD_TYPE;
            header->cmsg_len = CMSG_LEN(sizeof(std::uint8_t));
            *reinterpret_cast<std::uint8_t*>(CMSG_DATA(header)) = 21U;

            const auto sent = ::sendmsg(
                static_cast<int>(transport.native_handle()), &message,
                MSG_DONTWAIT | MSG_NOSIGNAL);
            if (sent == static_cast<ssize_t>(alert.size()))
                co_return {};
            if (sent < 0 && errno == EINTR)
                continue;
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                std::expected<void, std::error_code> ready;
                if constexpr (std::is_same_v<Cancellation, std::nullptr_t>)
                    ready = co_await async_wait_writable(context, transport);
                else
                    ready = co_await async_wait_writable(
                        context, transport, *cancellation);
                if (!ready)
                    co_return std::unexpected(ready.error());
                continue;
            }
            co_return std::unexpected(std::error_code{
                sent < 0 ? errno : EIO, std::system_category()});
        }
    }
} // namespace

auto enable_kernel_tls_tx(SSL* ssl, socket& transport) noexcept -> bool
{
    if (ssl == nullptr || SSL_version(ssl) != TLS1_2_VERSION)
        return false;

    const auto* cipher = SSL_get_current_cipher(ssl);
    if (cipher == nullptr)
        return false;
    const auto cipher_id = SSL_CIPHER_get_protocol_id(cipher);
    const bool aes128 = cipher_id == 0x009cU || cipher_id == 0xc02bU ||
        cipher_id == 0xc02fU;
    const bool aes256 = cipher_id == 0x009dU || cipher_id == 0xc02cU ||
        cipher_id == 0xc030U;
    if (!aes128 && !aes256)
        return false;

    std::array<std::uint8_t, tls_random_size> client_random{};
    std::array<std::uint8_t, tls_random_size> server_random{};
    std::array<std::uint8_t, tls_master_secret_size> master_secret{};
    cleanse_on_exit cleanse_master{master_secret.data(), master_secret.size()};
    auto* session = SSL_get_session(ssl);
    if (session == nullptr ||
        SSL_get_client_random(ssl, client_random.data(), client_random.size()) !=
            client_random.size() ||
        SSL_get_server_random(ssl, server_random.data(), server_random.size()) !=
            server_random.size() ||
        SSL_SESSION_get_master_key(session, master_secret.data(),
            master_secret.size()) != master_secret.size())
    {
        return false;
    }

    constexpr std::string_view label = "key expansion";
    std::array<std::uint8_t, label.size() + tls_random_size * 2U> seed{};
    std::memcpy(seed.data(), label.data(), label.size());
    std::memcpy(seed.data() + label.size(), server_random.data(),
        server_random.size());
    std::memcpy(seed.data() + label.size() + server_random.size(),
        client_random.data(), client_random.size());

    const bool server = SSL_is_server(ssl) == 1;
    const auto descriptor = static_cast<int>(transport.native_handle());
    constexpr std::string_view ulp = "tls";
    if (::setsockopt(descriptor, IPPROTO_TCP, TCP_ULP, ulp.data(),
            static_cast<socklen_t>(ulp.size())) != 0)
    {
        return false;
    }

    auto install = [&](auto& crypto, std::span<const std::uint8_t> block,
                       std::size_t key_size, std::size_t salt_size)
    {
        const auto key_offset = server ? key_size : 0U;
        const auto client_salt_offset = key_size * 2U;
        const auto salt_offset = server
            ? client_salt_offset + salt_size
            : client_salt_offset;
        std::memcpy(crypto.key, block.data() + key_offset,
            sizeof(crypto.key));
        std::memcpy(crypto.salt, block.data() + salt_offset,
            sizeof(crypto.salt));
        // Finished is record zero in the application traffic epoch. kTLS
        // continues with the next explicit nonce and record sequence.
        crypto.iv[sizeof(crypto.iv) - 1U] = 1U;
        crypto.rec_seq[sizeof(crypto.rec_seq) - 1U] = 1U;
        return ::setsockopt(descriptor, SOL_TLS, TLS_TX, &crypto,
                   sizeof(crypto)) == 0;
    };

    if (aes128)
    {
        std::array<std::uint8_t, tls12_aes128_key_block_size> key_block{};
        cleanse_on_exit cleanse_key_block{key_block.data(), key_block.size()};
        if (!tls12_prf<32U>(EVP_sha256(), master_secret, seed, key_block))
            return false;
        tls12_crypto_info_aes_gcm_128 crypto{};
        cleanse_on_exit cleanse_crypto{&crypto, sizeof(crypto)};
        crypto.info.version = TLS_1_2_VERSION;
        crypto.info.cipher_type = TLS_CIPHER_AES_GCM_128;
        return install(crypto, key_block, TLS_CIPHER_AES_GCM_128_KEY_SIZE,
            TLS_CIPHER_AES_GCM_128_SALT_SIZE);
    }

    std::array<std::uint8_t, tls12_aes256_key_block_size> key_block{};
    cleanse_on_exit cleanse_key_block{key_block.data(), key_block.size()};
    if (!tls12_prf<48U>(EVP_sha384(), master_secret, seed, key_block))
        return false;
    tls12_crypto_info_aes_gcm_256 crypto{};
    cleanse_on_exit cleanse_crypto{&crypto, sizeof(crypto)};
    crypto.info.version = TLS_1_2_VERSION;
    crypto.info.cipher_type = TLS_CIPHER_AES_GCM_256;
    return install(crypto, key_block, TLS_CIPHER_AES_GCM_256_KEY_SIZE,
        TLS_CIPHER_AES_GCM_256_SALT_SIZE);
}

auto async_kernel_tls_shutdown(io_context& context, socket& transport)
    -> task<std::expected<void, std::error_code>>
{
    co_return co_await send_close_notify(context, transport, nullptr);
}

auto async_kernel_tls_shutdown(io_context& context, socket& transport,
    cancel_token& cancellation) -> task<std::expected<void, std::error_code>>
{
    co_return co_await send_close_notify(
        context, transport, &cancellation);
}
#endif

} // namespace cnetmod::detail
