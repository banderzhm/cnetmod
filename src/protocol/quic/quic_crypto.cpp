module;

// This translation unit only uses BoringSSL's QUIC/TLS primitives.  Do not
// include platform socket headers here: Windows SDK certificate macros (for
// example X509_NAME) collide with BoringSSL's generated stack declarations.

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_SSL
    #ifdef CNETMOD_ENABLE_QUIC

        #include <openssl/aead.h>
        #include <openssl/aes.h>
        #include <openssl/chacha.h>
        #include <openssl/err.h>
        #include <openssl/evp.h>
        #include <openssl/hkdf.h>
        #include <openssl/ssl.h>

    #endif // CNETMOD_ENABLE_QUIC
#endif     // CNETMOD_HAS_SSL

module cnetmod.protocol.quic;

import std;

#ifdef CNETMOD_HAS_SSL
    #ifdef CNETMOD_ENABLE_QUIC

import cnetmod.core.ssl;
import cnetmod.core.buffer;
import :crypto;

namespace cnetmod::quic {

// =============================================================================
// Helper: Convert between encryption levels (RFC 9001)
// =============================================================================

inline auto to_ssl_level(encryption_level level) noexcept -> int
{
    switch (level)
    {
    case encryption_level::initial:
        return static_cast<int>(ssl_encryption_initial);
    case encryption_level::early_data:
        return static_cast<int>(ssl_encryption_early_data);
    case encryption_level::handshake:
        return static_cast<int>(ssl_encryption_handshake);
    case encryption_level::application:
        return static_cast<int>(ssl_encryption_application);
    default:
        return static_cast<int>(ssl_encryption_application);
    }
}

inline auto from_ssl_level(int level) noexcept -> encryption_level
{
    switch (level)
    {
    case static_cast<int>(ssl_encryption_initial):
        return encryption_level::initial;
    case static_cast<int>(ssl_encryption_early_data):
        return encryption_level::early_data;
    case static_cast<int>(ssl_encryption_handshake):
        return encryption_level::handshake;
    case static_cast<int>(ssl_encryption_application):
        return encryption_level::application;
    default:
        return encryption_level::application;
    }
}

// =============================================================================
// Detail helpers: varint, cipher traits, key derivation
// =============================================================================

namespace detail {

    /// QUIC variable-length integer encoding (RFC 9000 §16)
    [[nodiscard]] inline auto encode_varint_vec(std::uint64_t value)
        -> std::vector<std::byte>
    {
        std::vector<std::byte> out;
        if (value <= 0x3f)
        {
            out.push_back(static_cast<std::byte>(value));
        }
        else if (value <= 0x3fff)
        {
            out.push_back(static_cast<std::byte>(0x40 | (value >> 8)));
            out.push_back(static_cast<std::byte>(value & 0xff));
        }
        else if (value <= 0x3fffffff)
        {
            out.push_back(static_cast<std::byte>(0x80 | (value >> 24)));
            out.push_back(static_cast<std::byte>((value >> 16) & 0xff));
            out.push_back(static_cast<std::byte>((value >> 8) & 0xff));
            out.push_back(static_cast<std::byte>(value & 0xff));
        }
        else
        {
            for (int shift = 56; shift >= 0; shift -= 8)
            {
                out.push_back(static_cast<std::byte>((value >> shift) & 0xff));
            }
            out[0] |= static_cast<std::byte>(0xc0);
        }
        return out;
    }

    /// Decode a QUIC varint; returns (value, consumed) or an error
    [[nodiscard]] inline auto decode_varint_view(std::span<const std::byte> data)
        -> std::expected<std::pair<std::uint64_t, std::size_t>, std::error_code>
    {
        if (data.empty())
        {
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        }

        const auto prefix = std::to_integer<int>(data[0]) >> 6;
        const std::size_t len = std::size_t{1} << prefix;
        if (data.size() < len)
        {
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        }

        std::uint64_t value = std::to_integer<std::uint64_t>(data[0]) & 0x3f;
        for (std::size_t i = 1; i < len; ++i)
        {
            value = (value << 8) | std::to_integer<std::uint64_t>(data[i]);
        }
        return std::pair{value, len};
    }

    /// Encode a 64-bit integer in network byte order
    [[nodiscard]] inline auto encode_uint64_be(std::uint64_t value)
        -> std::array<std::byte, 8>
    {
        std::array<std::byte, 8> out{};
        for (int i = 0; i < 8; ++i)
        {
            out[i] = static_cast<std::byte>((value >> (56 - 8 * i)) & 0xff);
        }
        return out;
    }

    /// Parse a 64-bit big-endian integer
    [[nodiscard]] inline auto read_uint64_be(const std::byte* data) noexcept
        -> std::uint64_t
    {
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i)
        {
            value = (value << 8) | std::to_integer<std::uint64_t>(data[i]);
        }
        return value;
    }

    /// AEAD/digest parameters of a TLS 1.3 cipher suite (RFC 9001 §5)
    struct cipher_traits
    {
        const EVP_AEAD* aead;
        const EVP_MD* digest;
        std::size_t key_len;
        std::size_t tag_len;
    };

    [[nodiscard]] inline auto lookup_cipher_traits(std::uint32_t cipher_id)
        -> std::optional<cipher_traits>
    {
        switch (cipher_id)
        {
        case SSL_CIPHER_AES_128_GCM_SHA256:
            return cipher_traits{
                EVP_aead_aes_128_gcm(), EVP_sha256(), 16, 16};
        case SSL_CIPHER_AES_256_GCM_SHA384:
            return cipher_traits{
                EVP_aead_aes_256_gcm(), EVP_sha384(), 32, 16};
        case SSL_CIPHER_CHACHA20_POLY1305_SHA256:
            return cipher_traits{
                EVP_aead_chacha20_poly1305(), EVP_sha256(), 32, 16};
        default:
            return std::nullopt;
        }
    }

} // namespace detail

// =============================================================================
// HKDF-Expand-Label (RFC 8446 §7.1)
// =============================================================================

auto quic_tls_session::hkdf_expand_label(
    std::span<const std::uint8_t> secret,
    std::string_view label,
    std::span<const std::uint8_t> context,
    std::size_t output_len,
    const void* digest)
    -> std::vector<std::uint8_t>
{
    // struct {
    //     uint16 length = output_len;
    //     opaque label<7..255>;  // "tls13 " + label
    //     opaque context<0..255>;
    // } HkdfLabel;
    std::vector<std::uint8_t> info;
    info.reserve(2 + 1 + 6 + label.size() + 1 + context.size());

    info.push_back(static_cast<std::uint8_t>(output_len >> 8));
    info.push_back(static_cast<std::uint8_t>(output_len & 0xff));

    const std::size_t full_label_len = 6 + label.size(); // "tls13 " prefix
    info.push_back(static_cast<std::uint8_t>(full_label_len));
    constexpr std::string_view kTls13Prefix = "tls13 ";
    info.insert(info.end(), kTls13Prefix.begin(), kTls13Prefix.end());
    info.insert(info.end(), label.begin(), label.end());

    info.push_back(static_cast<std::uint8_t>(context.size()));
    info.insert(info.end(), context.begin(), context.end());

    std::vector<std::uint8_t> output(output_len);
    const auto* md = static_cast<const EVP_MD*>(digest);
    if (md == nullptr)
    {
        md = EVP_sha256();
    }

    if (HKDF_expand(output.data(), output_len, md,
            secret.data(), secret.size(),
            info.data(), info.size()) != 1)
    {
        output.clear();
    }
    return output;
}

namespace {

    auto make_initial_level_keys(std::span<const std::uint8_t> secret,
        std::string_view label_prefix)
        -> std::expected<quic_level_keys, std::error_code>
    {
        constexpr std::size_t key_length = 16;
        quic_level_keys keys;
        keys.cipher_id = SSL_CIPHER_AES_128_GCM_SHA256;
        keys.aead = EVP_aead_aes_128_gcm();
        keys.digest = EVP_sha256();
        keys.tag_len = 16;
        keys.secret.assign(secret.begin(), secret.end());
        keys.aead_key = quic_tls_session::hkdf_expand_label(
            secret, std::format("{} key", label_prefix), {}, key_length, keys.digest);
        keys.aead_iv = quic_tls_session::hkdf_expand_label(
            secret, std::format("{} iv", label_prefix), {}, 12, keys.digest);
        keys.hp_key = quic_tls_session::hkdf_expand_label(
            secret, std::format("{} hp", label_prefix), {}, key_length, keys.digest);
        if (keys.aead_key.size() != key_length || keys.aead_iv.size() != 12 ||
            keys.hp_key.size() != key_length)
        {
            return std::unexpected(std::make_error_code(std::errc::io_error));
        }
        return keys;
    }

} // namespace

auto derive_initial_keys(quic_version version, const connection_id& destination_cid)
    -> std::expected<quic_initial_keys, std::error_code>
{
    // RFC 9001 §5.2 defines a different salt per QUIC version.  Do not fall
    // back silently: using a v1 salt for another version breaks packet
    // authentication and creates misleading diagnostics.
    if (destination_cid.empty())
    {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    constexpr std::array<std::uint8_t, 20> v1_salt{
        0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 0x4d, 0x17,
        0x9a, 0xe6, 0xa4, 0xc8, 0x0c, 0xad, 0xcc, 0xbb, 0x7f, 0x0a};
    constexpr std::array<std::uint8_t, 20> v2_salt{
        0x0d, 0xed, 0xe3, 0xde, 0xf7, 0x00, 0xa6, 0xdb, 0x81, 0x93,
        0x81, 0xbe, 0x6e, 0x26, 0x9d, 0xcb, 0xf9, 0xbd, 0x2e, 0xd9};
    const auto* salt = version == quic_version::v1 ? v1_salt.data()
        : version == quic_version::v2              ? v2_salt.data()
                                                   : nullptr;
    if (salt == nullptr)
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    const auto initial_label = version == quic_version::v2 ? "quicv2" : "quic";
    std::array<std::uint8_t, 32> initial_secret{};
    std::size_t initial_secret_length = initial_secret.size();
    if (HKDF_extract(initial_secret.data(), &initial_secret_length, EVP_sha256(),
            reinterpret_cast<const std::uint8_t*>(destination_cid.data()),
            destination_cid.size(), salt, v1_salt.size()) != 1 ||
        initial_secret_length != initial_secret.size())
    {
        return std::unexpected(std::make_error_code(std::errc::io_error));
    }

    const auto client_secret = quic_tls_session::hkdf_expand_label(
        initial_secret, "client in", {}, initial_secret.size(), EVP_sha256());
    const auto server_secret = quic_tls_session::hkdf_expand_label(
        initial_secret, "server in", {}, initial_secret.size(), EVP_sha256());
    if (client_secret.size() != initial_secret.size() ||
        server_secret.size() != initial_secret.size())
    {
        return std::unexpected(std::make_error_code(std::errc::io_error));
    }

    auto client = make_initial_level_keys(client_secret, initial_label);
    if (!client)
        return std::unexpected(client.error());
    auto server = make_initial_level_keys(server_secret, initial_label);
    if (!server)
        return std::unexpected(server.error());
    return quic_initial_keys{std::move(*client), std::move(*server)};
}

auto make_retry_integrity_tag(quic_version version,
    const connection_id& original_destination_cid,
    std::span<const std::byte> retry_packet_without_tag)
    -> std::expected<std::array<std::byte, 16>, std::error_code>
{
    if (original_destination_cid.empty())
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    constexpr std::array<std::uint8_t, 16> v1_key{
        0xbe, 0x0c, 0x69, 0x0b, 0x9f, 0x66, 0x57, 0x5a,
        0x1d, 0x76, 0x6b, 0x54, 0xe3, 0x68, 0xc8, 0x4e};
    constexpr std::array<std::uint8_t, 12> v1_nonce{
        0x46, 0x15, 0x99, 0xd3, 0x5d, 0x63, 0x2b, 0xf2,
        0x23, 0x98, 0x25, 0xbb};
    constexpr std::array<std::uint8_t, 16> v2_key{
        0x8f, 0xb4, 0xb0, 0x1b, 0x56, 0xac, 0x48, 0xe2,
        0x60, 0xfb, 0xcb, 0xce, 0xad, 0x7c, 0xcc, 0x92};
    constexpr std::array<std::uint8_t, 12> v2_nonce{
        0xd8, 0x69, 0x69, 0xbc, 0x2d, 0x7c, 0x6d, 0x99,
        0x90, 0xef, 0xb0, 0x4a};
    const auto* key = version == quic_version::v1 ? v1_key.data()
        : version == quic_version::v2             ? v2_key.data()
                                                  : nullptr;
    const auto* nonce = version == quic_version::v1 ? v1_nonce.data()
        : version == quic_version::v2               ? v2_nonce.data()
                                                    : nullptr;
    if (key == nullptr || nonce == nullptr)
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));

    std::vector<std::byte> pseudo_packet;
    pseudo_packet.reserve(1 + original_destination_cid.size() + retry_packet_without_tag.size());
    pseudo_packet.push_back(static_cast<std::byte>(original_destination_cid.size()));
    pseudo_packet.insert(pseudo_packet.end(), original_destination_cid.data(),
        original_destination_cid.data() + original_destination_cid.size());
    pseudo_packet.insert(pseudo_packet.end(), retry_packet_without_tag.begin(), retry_packet_without_tag.end());
    EVP_AEAD_CTX context;
    if (EVP_AEAD_CTX_init(&context, EVP_aead_aes_128_gcm(), key, v1_key.size(), 16, nullptr) != 1)
        return std::unexpected(make_ssl_error());
    std::array<std::byte, 16> tag{};
    std::size_t tag_length{};
    const auto ok = EVP_AEAD_CTX_seal(&context,
        reinterpret_cast<std::uint8_t*>(tag.data()), &tag_length, tag.size(), nonce, v1_nonce.size(),
        nullptr, 0, reinterpret_cast<const std::uint8_t*>(pseudo_packet.data()), pseudo_packet.size());
    EVP_AEAD_CTX_cleanup(&context);
    if (ok != 1 || tag_length != tag.size())
        return std::unexpected(make_ssl_error());
    return tag;
}

auto validate_retry_integrity_tag(quic_version version,
    const connection_id& original_destination_cid,
    std::span<const std::byte> retry_packet) -> std::expected<void, std::error_code>
{
    if (retry_packet.size() < 16)
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    auto tag = make_retry_integrity_tag(version, original_destination_cid,
        retry_packet.first(retry_packet.size() - 16));
    if (!tag || CRYPTO_memcmp(tag->data(), retry_packet.data() + retry_packet.size() - 16, tag->size()) != 0)
        return std::unexpected(std::make_error_code(std::errc::permission_denied));
    return {};
}

// =============================================================================
// Secret installation and key derivation (RFC 9001 §5.1)
// =============================================================================

auto quic_tls_session::install_secret(
    encryption_level level,
    std::uint32_t cipher_id,
    std::span<const std::uint8_t> secret,
    bool for_reading) -> bool
{
    const auto idx = level_index(level);
    if (idx >= encryption_level_count)
    {
        return false;
    }

    auto traits = detail::lookup_cipher_traits(cipher_id);
    if (!traits)
    {
        return false; // Unsupported cipher suite
    }

    auto& keys = for_reading ? read_keys_[idx] : write_keys_[idx];
    keys.cipher_id = cipher_id;
    keys.aead = traits->aead;
    keys.digest = traits->digest;
    keys.tag_len = traits->tag_len;
    keys.secret.assign(secret.begin(), secret.end());

    // quic_key / quic_iv / quic_hp per RFC 9001 §5.1
    keys.aead_key = hkdf_expand_label(
        secret, "quic key", {}, traits->key_len, traits->digest);
    keys.aead_iv = hkdf_expand_label(
        secret, "quic iv", {}, 12, traits->digest);
    keys.hp_key = hkdf_expand_label(
        secret, "quic hp", {}, traits->key_len, traits->digest);
    const auto installed = keys.aead_key.size() == traits->key_len &&
        keys.aead_iv.size() == 12 &&
        keys.hp_key.size() == traits->key_len;
    if (!installed)
        return false;

    // TLS installs the first 1-RTT generation.  Pre-derive both independent
    // directions now, so a peer key-phase transition never needs to mutate
    // traffic-secret state while parsing an unauthenticated packet.
    if (level == encryption_level::application)
    {
        auto next = derive_next_keys(keys);
        if (!next)
            return false;
        if (for_reading)
            next_read_application_keys_ = std::move(*next);
        else
            next_write_application_keys_ = std::move(*next);
    }
    return true;
}

auto quic_tls_session::derive_next_keys(const quic_level_keys& current)
    -> std::optional<quic_level_keys>
{
    if (!current.valid() || current.digest == nullptr || current.aead == nullptr)
        return std::nullopt;
    auto secret = hkdf_expand_label(current.secret, "quic ku", {},
        current.secret.size(), current.digest);
    if (secret.size() != current.secret.size())
        return std::nullopt;
    quic_level_keys next{};
    next.cipher_id = current.cipher_id;
    next.aead = current.aead;
    next.digest = current.digest;
    next.tag_len = current.tag_len;
    next.secret = std::move(secret);
    const auto traits = detail::lookup_cipher_traits(next.cipher_id);
    if (!traits)
        return std::nullopt;
    next.aead_key = hkdf_expand_label(next.secret, "quic key", {}, traits->key_len, next.digest);
    next.aead_iv = hkdf_expand_label(next.secret, "quic iv", {}, 12, next.digest);
    // RFC 9001 section 6 updates only packet-protection keys and IVs.  Header
    // protection is intentionally stable across key phases; deriving a new
    // HP key makes the peer's key-phase bit and packet number undecodable.
    next.hp_key = current.hp_key;
    if (next.aead_key.size() != traits->key_len || next.aead_iv.size() != 12 ||
        next.hp_key.size() != traits->key_len)
        return std::nullopt;
    return next;
}

// =============================================================================
// BoringSSL SSL_QUIC_METHOD callbacks
// =============================================================================

auto quic_tls_session::cb_set_read_secret(
    SSL* ssl, enum ssl_encryption_level_t level,
    const SSL_CIPHER* cipher,
    const std::uint8_t* secret, std::size_t secret_len) -> int
{
    auto* self = static_cast<quic_tls_session*>(SSL_get_app_data(ssl));
    if (self == nullptr || cipher == nullptr)
    {
        return 0;
    }

    return self->install_secret(
               from_ssl_level(static_cast<int>(level)),
               SSL_CIPHER_get_protocol_id(cipher),
               std::span<const std::uint8_t>(secret, secret_len),
               /*for_reading=*/true)
        ? 1
        : 0;
}

auto quic_tls_session::cb_set_write_secret(
    SSL* ssl, enum ssl_encryption_level_t level,
    const SSL_CIPHER* cipher,
    const std::uint8_t* secret, std::size_t secret_len) -> int
{
    auto* self = static_cast<quic_tls_session*>(SSL_get_app_data(ssl));
    if (self == nullptr || cipher == nullptr)
    {
        return 0;
    }

    return self->install_secret(
               from_ssl_level(static_cast<int>(level)),
               SSL_CIPHER_get_protocol_id(cipher),
               std::span<const std::uint8_t>(secret, secret_len),
               /*for_reading=*/false)
        ? 1
        : 0;
}

auto quic_tls_session::cb_add_handshake_data(
    SSL* ssl, enum ssl_encryption_level_t level,
    const std::uint8_t* data, std::size_t len) -> int
{
    auto* self = static_cast<quic_tls_session*>(SSL_get_app_data(ssl));
    if (self == nullptr)
    {
        return 0;
    }

    const auto idx = level_index(from_ssl_level(static_cast<int>(level)));
    if (idx >= encryption_level_count)
    {
        return 0;
    }

    // Buffer the TLS handshake bytes; the transport packs them into
    // CRYPTO frames via take_handshake_data().
    auto& buffer = self->handshake_send_[idx];
    buffer.insert(
        buffer.end(),
        reinterpret_cast<const std::byte*>(data),
        reinterpret_cast<const std::byte*>(data) + len);
    return 1;
}

auto quic_tls_session::cb_flush_flight(SSL* ssl) -> int
{
    auto* self = static_cast<quic_tls_session*>(SSL_get_app_data(ssl));
    if (self == nullptr)
    {
        return 0;
    }

    // Notify the transport that a complete flight is ready to send.
    self->flush_pending_ = true;
    return 1;
}

auto quic_tls_session::cb_send_alert(
    SSL* ssl, enum ssl_encryption_level_t level, std::uint8_t alert) -> int
{
    auto* self = static_cast<quic_tls_session*>(SSL_get_app_data(ssl));
    if (self == nullptr)
    {
        return 0;
    }

    // Record the alert; the transport converts it into a
    // CONNECTION_CLOSE frame (error = CRYPTO_ERROR + alert).
    self->pending_alert_ =
        std::pair{from_ssl_level(static_cast<int>(level)), alert};
    return 1;
}

// =============================================================================
// Construction / factories
// =============================================================================

quic_tls_session::quic_tls_session(SSL* ssl)
    : ssl_(ssl)
{
    register_quic_callbacks();
}

quic_tls_session::~quic_tls_session()
{
    if (ssl_ != nullptr)
    {
        SSL_free(ssl_);
    }
}

void quic_tls_session::register_quic_callbacks()
{
    // Associate this session with the SSL object so the C callbacks can
    // recover it via SSL_get_app_data.
    SSL_set_app_data(ssl_, this);

    // The five mandatory QUIC hooks (RFC 9001 / BoringSSL SSL_QUIC_METHOD).
    static const SSL_QUIC_METHOD quic_method = {
        &quic_tls_session::cb_set_read_secret,
        &quic_tls_session::cb_set_write_secret,
        &quic_tls_session::cb_add_handshake_data,
        &quic_tls_session::cb_flush_flight,
        &quic_tls_session::cb_send_alert,
    };

    SSL_set_quic_method(ssl_, &quic_method);
}

auto quic_tls_session::client(ssl_context& ctx, transport_params params)
    -> std::expected<std::unique_ptr<quic_tls_session>, std::error_code>
{
    SSL* ssl = SSL_new(ctx.native());
    if (ssl == nullptr)
    {
        return std::unexpected(make_ssl_error());
    }

    // Set client mode
    SSL_set_connect_state(ssl);

    std::unique_ptr<quic_tls_session> session(new quic_tls_session(ssl));
    session->set_transport_params(std::move(params));

    // Advertise our transport parameters in the ClientHello
    if (!session->install_transport_params())
    {
        return std::unexpected(make_ssl_error());
    }

    // Set SNI server name if available
    if (!session->sent_params_.server_name.empty())
    {
        SSL_set_tlsext_host_name(ssl, session->sent_params_.server_name.c_str());
    }

    return session;
}

auto quic_tls_session::server(ssl_context& ctx, transport_params params)
    -> std::expected<std::unique_ptr<quic_tls_session>, std::error_code>
{
    SSL* ssl = SSL_new(ctx.native());
    if (ssl == nullptr)
    {
        return std::unexpected(make_ssl_error());
    }

    // Set server mode
    SSL_set_accept_state(ssl);

    std::unique_ptr<quic_tls_session> session(new quic_tls_session(ssl));
    session->server_mode_ = true;
    // Do not permit server 0-RTT through the default BoringSSL ticket path.
    // It cannot expose the offered ticket identifier before the acceptance
    // decision, so there is no point at which a replay cache can enforce
    // single use. A ticket callback must own that decision instead.
    SSL_set_early_data_enabled(ssl, 0);
    session->set_transport_params(std::move(params));

    // Advertise our transport parameters in EncryptedExtensions
    if (!session->install_transport_params())
    {
        return std::unexpected(make_ssl_error());
    }

    return session;
}

void quic_tls_session::set_transport_params(transport_params params)
{
    sent_params_ = std::move(params);
}

auto quic_tls_session::configure_retry_transport_parameters(
    connection_id original_destination_cid, connection_id retry_source_cid)
    -> std::expected<void, std::error_code>
{
    if (original_destination_cid.empty() || retry_source_cid.empty())
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    sent_params_.original_destination_connection_id = std::move(original_destination_cid);
    sent_params_.initial_source_connection_id = retry_source_cid;
    sent_params_.retry_source_connection_id = std::move(retry_source_cid);
    if (!install_transport_params())
        return std::unexpected(make_ssl_error());
    return {};
}

auto quic_tls_session::configure_initial_source_connection_id(connection_id cid)
    -> std::expected<void, std::error_code>
{
    if (cid.empty())
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    sent_params_.initial_source_connection_id = std::move(cid);
    if (!install_transport_params())
        return std::unexpected(make_ssl_error());
    return {};
}

auto quic_tls_session::install_transport_params() -> bool
{
    auto encoded = encode_transport_params();
    if (!encoded)
    {
        return false;
    }

    return SSL_set_quic_transport_params(
               ssl_,
               reinterpret_cast<const std::uint8_t*>(encoded->data()),
               encoded->size()) == 1;
}

void quic_tls_session::consume_peer_transport_params()
{
    const std::uint8_t* params = nullptr;
    std::size_t params_len = 0;
    SSL_get_peer_quic_transport_params(ssl_, &params, &params_len);

    if (params != nullptr && params_len > 0)
    {
        // RFC 9000 defaults for parameters omitted by the peer.  In
        // particular, every initial flow-control and stream-count limit is
        // zero, not this endpoint's local configuration default.
        received_params_ = {};
        received_params_.initial_max_data = 0;
        received_params_.initial_max_stream_data_bidi_local = 0;
        received_params_.initial_max_stream_data_bidi_remote = 0;
        received_params_.initial_max_stream_data_uni = 0;
        received_params_.initial_max_streams_bidi = 0;
        received_params_.initial_max_streams_uni = 0;
        received_params_.max_udp_payload_size = 65527;
        received_params_.ack_delay_exponent = 3;
        received_params_.max_ack_delay = std::chrono::milliseconds(25);
        received_params_.active_connection_id_limit = 2;
        auto decoded = decode_transport_params(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(params), params_len));
        if (!decoded)
            transport_params_error_ = decoded.error();
    }
}

// =============================================================================
// Handshake state machine
// =============================================================================

auto quic_tls_session::do_handshake()
    -> std::expected<handshake_result, std::error_code>
{
    ERR_clear_error();
    const int ret = SSL_do_handshake(ssl_);

    if (ret == 1)
    {
        // Handshake complete: parse the peer's transport parameters.
        consume_peer_transport_params();
        if (transport_params_error_)
            return std::unexpected(*transport_params_error_);
        return handshake_result::complete;
    }

    const int err = SSL_get_error(ssl_, ret);
    switch (err)
    {
    case SSL_ERROR_WANT_READ:
        // Need more CRYPTO data from the peer.
        return handshake_result::want_read;

    case SSL_ERROR_WANT_WRITE:
        // Outbound flight needs to be flushed to the transport.
        return handshake_result::want_write;

    case SSL_ERROR_WANT_CERTIFICATE_VERIFY:
    case SSL_ERROR_PENDING_CERTIFICATE:
        // Certificate lazily produced; treat as needing a re-drive.
        return handshake_result::want_read;

    case SSL_ERROR_EARLY_DATA_REJECTED:
        early_data_rejected_ = true;
        return handshake_result::early_data_rejected;

    default:
        return std::unexpected(make_ssl_error(err));
    }
}

auto quic_tls_session::process_post_handshake()
    -> std::expected<void, std::error_code>
{
    ERR_clear_error();
    if (SSL_process_quic_post_handshake(ssl_) == 1)
    {
        return {};
    }
    return std::unexpected(make_ssl_error());
}

auto quic_tls_session::set_resumption_ticket(const session_ticket& ticket)
    -> std::expected<void, std::error_code>
{
    if (ticket.empty())
    {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    SSL_SESSION* session = SSL_SESSION_from_bytes(
        reinterpret_cast<const std::uint8_t*>(ticket.serialized.data()),
        ticket.serialized.size(), SSL_get_SSL_CTX(ssl_));
    if (session == nullptr)
    {
        return std::unexpected(make_ssl_error());
    }

    const int result = SSL_set_session(ssl_, session);
    SSL_SESSION_free(session);
    if (result != 1)
    {
        return std::unexpected(make_ssl_error());
    }
    return {};
}

early_data_replay_cache::early_data_replay_cache(std::size_t capacity)
    : capacity_(capacity)
{
}

auto early_data_replay_cache::key(std::span<const std::byte> ticket_id)
    -> std::string
{
    return {reinterpret_cast<const char*>(ticket_id.data()), ticket_id.size()};
}

auto early_data_replay_cache::consume(std::span<const std::byte> ticket_id,
    std::chrono::steady_clock::time_point expires_at) -> bool
{
    const auto now = std::chrono::steady_clock::now();
    if (ticket_id.empty() || capacity_ == 0 || expires_at <= now)
        return false;

    std::scoped_lock lock(mutex_);
    for (auto it = entries_.begin(); it != entries_.end();)
    {
        if (it->second.expires_at <= now)
            it = entries_.erase(it);
        else
            ++it;
    }

    const auto ticket_key = key(ticket_id);
    if (entries_.contains(ticket_key))
        return false;

    // Evict the entry which expires first. This preserves the single-use
    // invariant for all entries that remain in the bounded cache.
    if (entries_.size() >= capacity_)
    {
        const auto victim = std::min_element(entries_.begin(), entries_.end(),
            [](const auto& left, const auto& right)
            {
                return left.second.expires_at < right.second.expires_at;
            });
        entries_.erase(victim);
    }
    entries_.emplace(ticket_key, entry{expires_at, ++next_generation_});
    return true;
}

void early_data_replay_cache::purge_expired(
    std::chrono::steady_clock::time_point now)
{
    std::scoped_lock lock(mutex_);
    for (auto it = entries_.begin(); it != entries_.end();)
    {
        if (it->second.expires_at <= now)
            it = entries_.erase(it);
        else
            ++it;
    }
}

auto configure_server_early_data_tickets(ssl_context& context,
    const server_early_data_ticket_callbacks& callbacks)
    -> std::expected<void, std::error_code>
{
    if (!callbacks.seal || !callbacks.open || !callbacks.replay_cache)
    {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    ssl_ticket_aead_callbacks tls_callbacks;
    tls_callbacks.max_overhead = callbacks.max_overhead;
    tls_callbacks.seal = callbacks.seal;
    tls_callbacks.open = [open = callbacks.open](std::span<const std::byte> ciphertext)
        -> std::expected<ssl_ticket_open_result, std::error_code>
    {
        auto opened = open(ciphertext);
        if (!opened)
            return std::unexpected(opened.error());
        return ssl_ticket_open_result{
            .plaintext = std::move(opened->plaintext),
            .identity = std::move(opened->identity),
            .early_data_expires_at = opened->early_data_expires_at,
        };
    };
    const auto replay_cache = callbacks.replay_cache;
    tls_callbacks.consume_early_data = [replay_cache](
                                           std::span<const std::byte> identity,
                                           std::chrono::steady_clock::time_point expires_at)
    {
        return replay_cache->consume(identity, expires_at);
    };
    return context.configure_ticket_aead(std::move(tls_callbacks));
}

auto early_data_replay_cache::size() const -> std::size_t
{
    std::scoped_lock lock(mutex_);
    return entries_.size();
}

auto quic_tls_session::take_resumption_ticket()
    -> std::expected<session_ticket, std::error_code>
{
    SSL_SESSION* session = SSL_get1_session(ssl_);
    if (session == nullptr)
    {
        return std::unexpected(make_ssl_error());
    }

    if (SSL_SESSION_has_ticket(session) != 1)
    {
        SSL_SESSION_free(session);
        return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
    }

    std::uint8_t* bytes = nullptr;
    std::size_t length = 0;
    const int result = SSL_SESSION_to_bytes(session, &bytes, &length);
    SSL_SESSION_free(session);
    if (result != 1 || bytes == nullptr || length == 0)
    {
        if (bytes != nullptr)
        {
            OPENSSL_free(bytes);
        }
        return std::unexpected(make_ssl_error());
    }

    session_ticket ticket;
    ticket.serialized.assign(reinterpret_cast<std::byte*>(bytes),
        reinterpret_cast<std::byte*>(bytes) + length);
    OPENSSL_free(bytes);
    return ticket;
}

void quic_tls_session::enable_early_data(bool enabled) noexcept
{
    if (server_mode_)
    {
        SSL_set_early_data_enabled(ssl_, 0);
        early_data_enabled_ = false;
        return;
    }
    SSL_set_early_data_enabled(ssl_, enabled ? 1 : 0);
    early_data_enabled_ = enabled;
}

auto quic_tls_session::enable_server_early_data()
    -> std::expected<void, std::error_code>
{
    if (!server_mode_)
        return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));

    // The connection constructor reaches here only after it has installed a
    // ticket AEAD bridge with a shared cache and a non-empty QUIC context.
    SSL_set_early_data_enabled(ssl_, 1);
    early_data_enabled_ = true;
    server_early_data_authorized_ = true;
    return {};
}

auto quic_tls_session::set_early_data_context(std::span<const std::byte> context)
    -> std::expected<void, std::error_code>
{
    if (context.empty() || SSL_set_quic_early_data_context(ssl_, reinterpret_cast<const std::uint8_t*>(context.data()), context.size()) != 1)
    {
        return std::unexpected(context.empty()
                ? std::make_error_code(std::errc::invalid_argument)
                : make_ssl_error());
    }
    return {};
}

auto quic_tls_session::early_data_accepted() const noexcept -> bool
{
    return SSL_early_data_accepted(ssl_) == 1;
}

auto quic_tls_session::early_data_status() const noexcept -> early_data_state
{
    if (!early_data_enabled_)
        return early_data_state::disabled;
    if (early_data_accepted())
        return early_data_state::accepted;
    if (early_data_rejected_ ||
        SSL_get_early_data_reason(ssl_) != ssl_early_data_unknown)
    {
        return early_data_state::rejected;
    }
    return early_data_state::pending;
}

auto quic_tls_session::early_data_reason() const noexcept -> int
{
    return static_cast<int>(SSL_get_early_data_reason(ssl_));
}

auto quic_tls_session::reset_after_early_data_rejection()
    -> std::expected<void, std::error_code>
{
    if (!early_data_rejected_)
    {
        return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
    }
    SSL_reset_early_data_reject(ssl_);
    early_data_rejected_ = false;
    return {};
}

auto quic_tls_session::provide_quic_data(
    encryption_level level,
    std::span<const std::byte> data)
    -> std::expected<void, std::error_code>
{
    const auto ssl_level =
        static_cast<enum ssl_encryption_level_t>(to_ssl_level(level));

    // Enforce the per-level flight limit to bound buffering (RFC 9000 §7.5)
    const std::size_t max_len =
        SSL_quic_max_handshake_flight_len(ssl_, ssl_level);
    if (data.size() > max_len)
    {
        return std::unexpected(
            std::make_error_code(std::errc::message_size));
    }

    ERR_clear_error();
    if (SSL_provide_quic_data(
            ssl_, ssl_level,
            reinterpret_cast<const std::uint8_t*>(data.data()),
            data.size()) != 1)
    {
        return std::unexpected(make_ssl_error());
    }

    return {};
}

auto quic_tls_session::extract_plaintext(
    encryption_level level,
    mutable_buffer buf)
    -> std::expected<std::size_t, std::error_code>
{
    // In QUIC mode SSL_read/SSL_write are prohibited; application data
    // never traverses the TLS record layer. Nothing to extract.
    std::ignore = level;
    std::ignore = buf;
    return std::size_t{0};
}

auto quic_tls_session::is_handshake_complete() const noexcept -> bool
{
    return SSL_is_init_finished(ssl_);
}

auto quic_tls_session::current_encryption_level() const noexcept
    -> encryption_level
{
    return from_ssl_level(static_cast<int>(SSL_quic_read_level(ssl_)));
}

// =============================================================================
// Outbound handshake data / alerts
// =============================================================================

auto quic_tls_session::take_handshake_data(encryption_level level)
    -> std::vector<std::byte>
{
    const auto idx = level_index(level);
    if (idx >= encryption_level_count)
    {
        return {};
    }

    auto data = std::move(handshake_send_[idx]);
    handshake_send_[idx].clear();
    return data;
}

auto quic_tls_session::has_pending_handshake_data() const noexcept -> bool
{
    for (const auto& buffer : handshake_send_)
    {
        if (!buffer.empty())
        {
            return true;
        }
    }
    return false;
}

auto quic_tls_session::handshake_flush_pending() noexcept -> bool
{
    const bool pending = flush_pending_;
    flush_pending_ = false;
    return pending;
}

auto quic_tls_session::pending_alert() const noexcept
    -> std::optional<std::pair<encryption_level, std::uint8_t>>
{
    return pending_alert_;
}

void quic_tls_session::consume_alert() noexcept
{
    pending_alert_.reset();
}

// =============================================================================
// Secret / key accessors
// =============================================================================

auto quic_tls_session::read_secret(encryption_level level)
    -> std::optional<std::vector<std::byte>>
{
    const auto& keys = read_keys_[level_index(level)];
    if (!keys.valid())
    {
        return std::nullopt;
    }

    std::vector<std::byte> out(keys.secret.size());
    std::memcpy(out.data(), keys.secret.data(), keys.secret.size());
    return out;
}

auto quic_tls_session::write_secret(encryption_level level)
    -> std::optional<std::vector<std::byte>>
{
    const auto& keys = write_keys_[level_index(level)];
    if (!keys.valid())
    {
        return std::nullopt;
    }

    std::vector<std::byte> out(keys.secret.size());
    std::memcpy(out.data(), keys.secret.data(), keys.secret.size());
    return out;
}

auto quic_tls_session::read_keys(encryption_level level) const noexcept
    -> const quic_level_keys*
{
    const auto& keys = read_keys_[level_index(level)];
    return keys.valid() ? &keys : nullptr;
}

auto quic_tls_session::write_keys(encryption_level level) const noexcept
    -> const quic_level_keys*
{
    const auto& keys = write_keys_[level_index(level)];
    return keys.valid() ? &keys : nullptr;
}

void quic_tls_session::discard_keys(encryption_level level) noexcept
{
    if (level == encryption_level::application)
        return;
    const auto index = level_index(level);
    read_keys_[index] = {};
    write_keys_[index] = {};
    handshake_send_[index].clear();
}

auto quic_tls_session::initiate_key_update()
    -> std::expected<void, std::error_code>
{
    const auto index = level_index(encryption_level::application);
    if (!write_keys_[index].valid() || !next_write_application_keys_)
        return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));

    write_keys_[index] = std::move(*next_write_application_keys_);
    auto next = derive_next_keys(write_keys_[index]);
    if (!next)
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
    next_write_application_keys_ = std::move(*next);
    write_application_key_phase_ = !write_application_key_phase_;
    return {};
}

auto quic_tls_session::application_write_key_phase() const noexcept -> bool
{
    return write_application_key_phase_;
}

auto quic_tls_session::application_read_key_candidates() const
    -> std::vector<application_read_key_candidate>
{
    std::vector<application_read_key_candidate> candidates;
    const auto index = level_index(encryption_level::application);
    if (read_keys_[index].valid())
        candidates.push_back({&read_keys_[index], read_application_key_phase_,
            application_read_key_kind::current});
    if (next_read_application_keys_)
        candidates.push_back({std::addressof(*next_read_application_keys_), !read_application_key_phase_,
            application_read_key_kind::next});
    if (previous_read_application_keys_)
        candidates.push_back({std::addressof(*previous_read_application_keys_), !read_application_key_phase_,
            application_read_key_kind::previous});
    return candidates;
}

void quic_tls_session::confirm_application_read_key(application_read_key_kind kind)
{
    if (kind != application_read_key_kind::next || !next_read_application_keys_)
        return;
    const auto index = level_index(encryption_level::application);
    previous_read_application_keys_ = std::move(read_keys_[index]);
    previous_read_key_since_ = std::chrono::steady_clock::now();
    read_keys_[index] = std::move(*next_read_application_keys_);
    next_read_application_keys_ = derive_next_keys(read_keys_[index]);
    read_application_key_phase_ = !read_application_key_phase_;
}

void quic_tls_session::discard_expired_application_read_keys(
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::duration retention) noexcept
{
    if (previous_read_key_since_ && now - *previous_read_key_since_ >= retention)
    {
        previous_read_application_keys_.reset();
        previous_read_key_since_.reset();
    }
}

auto quic_tls_session::application_read_key_phase() const noexcept -> bool
{
    return read_application_key_phase_;
}

auto quic_tls_session::received_transport_params() const noexcept
    -> const transport_params&
{
    return received_params_;
}

auto quic_tls_session::get_server_name() const noexcept -> std::string_view
{
    return sent_params_.server_name;
}

auto quic_tls_session::get_alpn_selected() const noexcept -> std::string_view
{
    const unsigned char* data = nullptr;
    unsigned int len = 0;
    SSL_get0_alpn_selected(ssl_, &data, &len);
    return {reinterpret_cast<const char*>(data), static_cast<std::size_t>(len)};
}

// =============================================================================
// Header protection (RFC 9001 §5.4)
// =============================================================================

namespace detail {

    /// Compute the 5-byte header protection mask from the sample
    /// (AES-ECB for AES suites, ChaCha20 for ChaCha20-Poly1305).
    [[nodiscard]] inline auto compute_hp_mask(
        const quic_level_keys& keys,
        std::span<const std::byte> sample)
        -> std::expected<std::array<std::uint8_t, 5>, std::error_code>
    {
        std::array<std::uint8_t, 5> mask{};

        if (sample.size() < 16 || keys.hp_key.empty())
        {
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        }

        const auto* sample_u8 = reinterpret_cast<const std::uint8_t*>(sample.data());

        if (keys.cipher_id == SSL_CIPHER_CHACHA20_POLY1305_SHA256)
        {
            // counter = sample[0..3], nonce = sample[4..15]
            const auto counter = static_cast<std::uint32_t>(sample_u8[0]) |
                (static_cast<std::uint32_t>(sample_u8[1]) << 8U) |
                (static_cast<std::uint32_t>(sample_u8[2]) << 16U) |
                (static_cast<std::uint32_t>(sample_u8[3]) << 24U);

            std::array<std::uint8_t, 5> zeros{};
            std::array<std::uint8_t, 12> nonce{};
            std::copy_n(sample_u8 + 4, 12, nonce.begin());

            CRYPTO_chacha_20(
                mask.data(), zeros.data(), zeros.size(),
                keys.hp_key.data(), nonce.data(), counter);
        }
        else
        {
            // AES header protection uses AES-ECB over the 16-byte sample
            const auto key_bits = keys.hp_key.size() * CHAR_BIT;
            if (key_bits != 128 && key_bits != 256)
            {
                return std::unexpected(
                    std::make_error_code(std::errc::not_supported));
            }

            AES_KEY aes_key;
            if (AES_set_encrypt_key(keys.hp_key.data(), static_cast<int>(key_bits), &aes_key) != 0)
            {
                return std::unexpected(make_ssl_error());
            }

            std::array<std::uint8_t, 16> block{};
            AES_encrypt(sample_u8, block.data(), &aes_key);
            std::copy_n(block.begin(), 5, mask.begin());
        }

        return mask;
    }

} // namespace detail

auto protect_header(const quic_level_keys& keys, std::span<std::byte> packet,
    std::size_t packet_number_offset, bool long_header)
    -> std::expected<void, std::error_code>
{
    // The sample starts four bytes after the packet-number field, even when
    // the encoded packet number itself is shorter (RFC 9001 §5.4.2).
    constexpr std::size_t sample_offset = 4;
    if (packet_number_offset >= packet.size() ||
        packet_number_offset + sample_offset + 16 > packet.size())
    {
        return std::unexpected(std::make_error_code(std::errc::message_size));
    }
    const auto pn_length = (std::to_integer<std::uint8_t>(packet.front()) & 0x03U) + 1U;
    if (packet_number_offset + pn_length > packet.size())
        return std::unexpected(std::make_error_code(std::errc::bad_message));
    auto mask = detail::compute_hp_mask(keys,
        std::span<const std::byte>{packet}.subspan(packet_number_offset + sample_offset, 16));
    if (!mask)
        return std::unexpected(mask.error());
    packet.front() ^= static_cast<std::byte>(mask->at(0) & (long_header ? 0x0f : 0x1f));
    for (std::size_t i = 0; i < pn_length; ++i)
        packet[packet_number_offset + i] ^= static_cast<std::byte>(mask->at(i + 1));
    return {};
}

auto unprotect_header(const quic_level_keys& keys, std::span<std::byte> packet,
    std::size_t packet_number_offset, bool long_header)
    -> std::expected<std::size_t, std::error_code>
{
    constexpr std::size_t sample_offset = 4;
    if (packet_number_offset >= packet.size() ||
        packet_number_offset + sample_offset + 16 > packet.size())
    {
        return std::unexpected(std::make_error_code(std::errc::message_size));
    }
    auto mask = detail::compute_hp_mask(keys,
        std::span<const std::byte>{packet}.subspan(packet_number_offset + sample_offset, 16));
    if (!mask)
        return std::unexpected(mask.error());
    packet.front() ^= static_cast<std::byte>(mask->at(0) & (long_header ? 0x0f : 0x1f));
    const auto pn_length = (std::to_integer<std::uint8_t>(packet.front()) & 0x03U) + 1U;
    if (packet_number_offset + pn_length > packet.size())
        return std::unexpected(std::make_error_code(std::errc::bad_message));
    for (std::size_t i = 0; i < pn_length; ++i)
        packet[packet_number_offset + i] ^= static_cast<std::byte>(mask->at(i + 1));
    return pn_length;
}

/// Pick the best installed key set for header protection.
/// For sending use write keys, for receiving use read keys; prefer the
/// highest encryption level with installed keys.
static auto pick_hp_keys(
    const std::array<quic_level_keys, encryption_level_count>& sets) noexcept
    -> const quic_level_keys*
{
    for (std::size_t i = encryption_level_count; i-- > 0;)
    {
        if (sets[i].valid())
        {
            return &sets[i];
        }
    }
    return nullptr;
}

auto quic_tls_session::apply_header_protection(
    std::span<const std::byte> sample,
    std::span<std::byte> protected_value)
    -> std::expected<void, std::error_code>
{
    const auto* keys = pick_hp_keys(write_keys_);
    if (keys == nullptr)
    {
        return std::unexpected(
            std::make_error_code(std::errc::operation_not_permitted));
    }

    auto mask = detail::compute_hp_mask(*keys, sample);
    if (!mask)
    {
        return std::unexpected(mask.error());
    }

    if (protected_value.empty())
    {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    // First byte: protect only the low bits (long header: 4, short: 5).
    // Without header-form context we conservatively mask 5 low bits; the
    // caller passes only the packet-number region plus first byte.
    protected_value[0] ^= static_cast<std::byte>(mask->at(0) & 0x1f);
    for (std::size_t i = 1; i < protected_value.size() && i < 5; ++i)
    {
        protected_value[i] ^= static_cast<std::byte>(mask->at(i));
    }

    return {};
}

auto quic_tls_session::remove_header_protection(
    std::span<const std::byte> sample,
    std::span<const std::byte> header,
    std::span<std::byte> unprotected)
    -> std::expected<void, std::error_code>
{
    const auto* keys = pick_hp_keys(read_keys_);
    if (keys == nullptr)
    {
        return std::unexpected(
            std::make_error_code(std::errc::operation_not_permitted));
    }

    auto mask = detail::compute_hp_mask(*keys, sample);
    if (!mask)
    {
        return std::unexpected(mask.error());
    }

    if (unprotected.empty())
    {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    // Determine the first-byte mask width from the long-header bit.
    // `header` carries at least the first header byte of the packet.
    const bool long_header =
        !header.empty() &&
        (std::to_integer<int>(header[0]) & 0x80) != 0;
    const std::uint8_t first_mask = long_header ? 0x0f : 0x1f;

    unprotected[0] ^= static_cast<std::byte>(mask->at(0) & first_mask);
    for (std::size_t i = 1; i < unprotected.size() && i < 5; ++i)
    {
        unprotected[i] ^= static_cast<std::byte>(mask->at(i));
    }

    return {};
}

// =============================================================================
// Packet payload AEAD (RFC 9001 §5.3)
// =============================================================================

namespace detail {

    /// Build the per-packet nonce: iv XOR packet number (RFC 9001 §5.3)
    [[nodiscard]] inline auto build_nonce(
        std::span<const std::uint8_t> iv, std::uint64_t packet_number)
        -> std::array<std::uint8_t, 12>
    {
        std::array<std::uint8_t, 12> nonce{};
        if (iv.size() == nonce.size())
        {
            std::copy(iv.begin(), iv.end(), nonce.begin());
        }

        for (int i = 0; i < 8; ++i)
        {
            nonce[11 - i] ^= static_cast<std::uint8_t>(
                (packet_number >> (8 * i)) & 0xff);
        }
        return nonce;
    }

} // namespace detail

auto seal_payload(const quic_level_keys& keys,
    std::span<const std::byte> payload, std::span<const std::byte> header,
    std::uint64_t packet_number)
    -> std::expected<std::vector<std::byte>, std::error_code>
{
    if (!keys.valid() || keys.aead == nullptr || keys.aead_key.empty() ||
        keys.aead_iv.size() != 12)
    {
        return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
    }
    const auto nonce = detail::build_nonce(keys.aead_iv, packet_number);
    EVP_AEAD_CTX context;
    if (EVP_AEAD_CTX_init(&context, static_cast<const EVP_AEAD*>(keys.aead),
            keys.aead_key.data(), keys.aead_key.size(), keys.tag_len, nullptr) != 1)
    {
        return std::unexpected(make_ssl_error());
    }
    std::vector<std::byte> output(payload.size() + keys.tag_len);
    std::size_t output_length{};
    const auto ok = EVP_AEAD_CTX_seal(&context,
        reinterpret_cast<std::uint8_t*>(output.data()), &output_length, output.size(),
        nonce.data(), nonce.size(), reinterpret_cast<const std::uint8_t*>(payload.data()),
        payload.size(), reinterpret_cast<const std::uint8_t*>(header.data()), header.size());
    EVP_AEAD_CTX_cleanup(&context);
    if (ok != 1)
        return std::unexpected(make_ssl_error());
    output.resize(output_length);
    return output;
}

auto open_payload(const quic_level_keys& keys,
    std::span<const std::byte> protected_payload,
    std::span<const std::byte> header, std::uint64_t packet_number)
    -> std::expected<std::vector<std::byte>, std::error_code>
{
    if (!keys.valid() || keys.aead == nullptr || keys.aead_key.empty() ||
        keys.aead_iv.size() != 12 || protected_payload.size() < keys.tag_len)
    {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    const auto nonce = detail::build_nonce(keys.aead_iv, packet_number);
    EVP_AEAD_CTX context;
    if (EVP_AEAD_CTX_init(&context, static_cast<const EVP_AEAD*>(keys.aead),
            keys.aead_key.data(), keys.aead_key.size(), keys.tag_len, nullptr) != 1)
    {
        return std::unexpected(make_ssl_error());
    }
    std::vector<std::byte> output(protected_payload.size() - keys.tag_len);
    std::size_t output_length{};
    const auto ok = EVP_AEAD_CTX_open(&context,
        reinterpret_cast<std::uint8_t*>(output.data()), &output_length, output.size(),
        nonce.data(), nonce.size(),
        reinterpret_cast<const std::uint8_t*>(protected_payload.data()), protected_payload.size(),
        reinterpret_cast<const std::uint8_t*>(header.data()), header.size());
    EVP_AEAD_CTX_cleanup(&context);
    if (ok != 1)
        return std::unexpected(make_ssl_error());
    output.resize(output_length);
    return output;
}

auto quic_tls_session::encrypt_packet(
    std::span<const std::byte> payload,
    std::span<const std::byte> header,
    encryption_level level,
    std::uint64_t packet_number)
    -> std::expected<std::pair<std::vector<std::byte>, std::array<std::byte, 16>>, std::error_code>
{
    const auto& keys = write_keys_[level_index(level)];
    if (!keys.valid())
    {
        return std::unexpected(
            std::make_error_code(std::errc::operation_not_permitted));
    }

    const auto* aead = static_cast<const EVP_AEAD*>(keys.aead);
    const auto nonce = detail::build_nonce(keys.aead_iv, packet_number);

    EVP_AEAD_CTX ctx;
    if (EVP_AEAD_CTX_init(
            &ctx, aead, keys.aead_key.data(), keys.aead_key.size(),
            keys.tag_len, nullptr) != 1)
    {
        return std::unexpected(make_ssl_error());
    }

    std::vector<std::byte> out(payload.size() + keys.tag_len);
    std::size_t out_len = 0;

    const int ok = EVP_AEAD_CTX_seal(
        &ctx,
        reinterpret_cast<std::uint8_t*>(out.data()), &out_len, out.size(),
        nonce.data(), nonce.size(),
        reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(),
        reinterpret_cast<const std::uint8_t*>(header.data()), header.size());

    EVP_AEAD_CTX_cleanup(&ctx);

    if (ok != 1)
    {
        return std::unexpected(make_ssl_error());
    }

    out.resize(out_len);

    // The AEAD tag is appended to the ciphertext; also surface it
    // separately for callers that track it explicitly.
    std::array<std::byte, 16> tag{};
    const std::size_t tag_copy = std::min<std::size_t>(keys.tag_len, 16);
    std::memcpy(tag.data(), out.data() + out_len - keys.tag_len, tag_copy);

    return std::pair{std::move(out), tag};
}

auto quic_tls_session::encrypt_packet(
    std::span<const std::byte> payload,
    encryption_level level)
    -> std::expected<std::pair<std::vector<std::byte>, std::array<std::byte, 16>>, std::error_code>
{
    const auto idx = level_index(level);
    const auto pn = send_pn_[idx]++;
    return encrypt_packet(payload, {}, level, pn);
}

auto quic_tls_session::decrypt_packet(
    std::span<const std::byte> protected_payload,
    std::span<const std::byte> header,
    encryption_level level,
    std::uint64_t packet_number)
    -> std::expected<std::vector<std::byte>, std::error_code>
{
    const auto& keys = read_keys_[level_index(level)];
    if (!keys.valid())
    {
        return std::unexpected(
            std::make_error_code(std::errc::operation_not_permitted));
    }

    if (protected_payload.size() < keys.tag_len)
    {
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));
    }

    const auto* aead = static_cast<const EVP_AEAD*>(keys.aead);
    const auto nonce = detail::build_nonce(keys.aead_iv, packet_number);

    EVP_AEAD_CTX ctx;
    if (EVP_AEAD_CTX_init(
            &ctx, aead, keys.aead_key.data(), keys.aead_key.size(),
            keys.tag_len, nullptr) != 1)
    {
        return std::unexpected(make_ssl_error());
    }

    std::vector<std::byte> out(protected_payload.size());
    std::size_t out_len = 0;

    const int ok = EVP_AEAD_CTX_open(
        &ctx,
        reinterpret_cast<std::uint8_t*>(out.data()), &out_len, out.size(),
        nonce.data(), nonce.size(),
        reinterpret_cast<const std::uint8_t*>(protected_payload.data()),
        protected_payload.size(),
        reinterpret_cast<const std::uint8_t*>(header.data()), header.size());

    EVP_AEAD_CTX_cleanup(&ctx);

    if (ok != 1)
    {
        // Authentication failed: treat as a protocol/crypto error.
        ERR_clear_error();
        return std::unexpected(
            std::make_error_code(std::errc::bad_message));
    }

    out.resize(out_len);
    return out;
}

auto quic_tls_session::decrypt_packet(
    std::span<const std::byte> protected_payload,
    encryption_level level)
    -> std::expected<std::vector<std::byte>, std::error_code>
{
    // Counter-based overload: peek at the next expected packet number but
    // only advance it on successful authentication.
    const auto idx = level_index(level);
    const auto pn = recv_pn_[idx];

    auto result = decrypt_packet(protected_payload, {}, level, pn);
    if (result)
    {
        ++recv_pn_[idx];
    }
    return result;
}

// =============================================================================
// Transport Parameters Encoding/Decoding (RFC 9000 §7.4)
// =============================================================================

namespace detail {

    /// Encode a transport parameter as (varint type, varint length, value)
    [[nodiscard]] inline auto encode_transport_parameter(
        std::uint64_t param_type,
        std::span<const std::byte> value)
        -> std::vector<std::byte>
    {
        std::vector<std::byte> result = encode_varint_vec(param_type);

        auto len_bytes = encode_varint_vec(value.size());
        result.insert(result.end(), len_bytes.begin(), len_bytes.end());
        result.insert(result.end(), value.begin(), value.end());

        return result;
    }

    /// Decode a single transport parameter; returns bytes consumed
    [[nodiscard]] inline auto decode_transport_parameter(
        std::span<const std::byte> data,
        std::uint64_t& param_type,
        std::span<const std::byte>& param_value)
        -> std::expected<std::size_t, std::error_code>
    {
        std::size_t offset = 0;

        // Decode parameter type
        auto type_result = decode_varint_view(data.subspan(offset));
        if (!type_result)
        {
            return std::unexpected(type_result.error());
        }
        param_type = type_result->first;
        offset += type_result->second;

        // Decode value length
        auto len_result = decode_varint_view(data.subspan(offset));
        if (!len_result)
        {
            return std::unexpected(len_result.error());
        }

        const auto value_len = static_cast<std::size_t>(len_result->first);
        offset += len_result->second;

        if (offset + value_len > data.size())
        {
            return std::unexpected(
                std::make_error_code(std::errc::protocol_error));
        }
        param_value = data.subspan(offset, value_len);
        offset += value_len;

        return offset;
    }

} // namespace detail

auto quic_tls_session::encode_transport_params()
    -> std::expected<std::vector<std::byte>, std::error_code>
{
    std::vector<std::byte> result;

    const auto append_varint = [&result](std::uint64_t type, std::uint64_t value)
    {
        const auto bytes = detail::encode_varint_vec(value);
        auto encoded = detail::encode_transport_parameter(
            type, std::span<const std::byte>(bytes));
        result.insert(result.end(), encoded.begin(), encoded.end());
    };

    // RFC 9000 §18: every integer transport-parameter value is a QUIC
    // variable-length integer, never a fixed-width network integer.
    append_varint(0x01, static_cast<std::uint64_t>(sent_params_.idle_timeout.count()));
    append_varint(0x03, sent_params_.max_udp_payload_size);
    append_varint(0x04, sent_params_.initial_max_data);
    append_varint(0x05, sent_params_.initial_max_stream_data_bidi_local);
    append_varint(0x06, sent_params_.initial_max_stream_data_bidi_remote);
    append_varint(0x07, sent_params_.initial_max_stream_data_uni);
    append_varint(0x08, sent_params_.initial_max_streams_bidi);
    append_varint(0x09, sent_params_.initial_max_streams_uni);
    append_varint(0x0a, sent_params_.ack_delay_exponent);
    append_varint(0x0b, static_cast<std::uint64_t>(sent_params_.max_ack_delay.count()));
    append_varint(0x0e, sent_params_.active_connection_id_limit);

    const auto append_cid = [&result](std::uint64_t type, const connection_id& cid)
    {
        auto encoded = detail::encode_transport_parameter(type,
            std::span<const std::byte>{cid.data(), cid.size()});
        result.insert(result.end(), encoded.begin(), encoded.end());
    };
    if (sent_params_.original_destination_connection_id)
        append_cid(0x00, *sent_params_.original_destination_connection_id);
    if (sent_params_.initial_source_connection_id)
        append_cid(0x0f, *sent_params_.initial_source_connection_id);
    if (sent_params_.retry_source_connection_id)
        append_cid(0x10, *sent_params_.retry_source_connection_id);

    // Disable Active Migration
    if (sent_params_.disable_active_migration)
    {
        auto encoded = detail::encode_transport_parameter(0x0c, {});
        result.insert(result.end(), encoded.begin(), encoded.end());
    }

    // ALPN and SNI are TLS extensions, not QUIC transport parameters.

    return result;
}

auto quic_tls_session::decode_transport_params(
    std::span<const std::byte> encoded)
    -> std::expected<void, std::error_code>
{
    std::size_t offset = 0;
    std::set<std::uint64_t> seen;

    while (offset < encoded.size())
    {
        std::uint64_t param_type = 0;
        std::span<const std::byte> param_value;

        auto result = detail::decode_transport_parameter(
            encoded.subspan(offset), param_type, param_value);
        if (!result)
        {
            return std::unexpected(result.error());
        }
        offset += *result;
        if (!seen.insert(param_type).second)
            return std::unexpected(make_error_code(quic_errc::transport_parameter_error));

        const auto read_varint = [&param_value]() -> std::expected<std::uint64_t, std::error_code>
        {
            const auto decoded = detail::decode_varint_view(param_value);
            if (!decoded || decoded->second != param_value.size())
                return std::unexpected(make_error_code(quic_errc::transport_parameter_error));
            return decoded->first;
        };

        const auto assign_varint = [&read_varint](std::uint64_t& destination)
            -> std::expected<void, std::error_code>
        {
            auto value = read_varint();
            if (!value)
                return std::unexpected(value.error());
            destination = *value;
            return {};
        };

        switch (param_type)
        {
        case 0x01: // max_idle_timeout
        {
            auto timeout = read_varint();
            if (!timeout)
                return std::unexpected(timeout.error());
            received_params_.idle_timeout = std::chrono::milliseconds(*timeout);
            break;
        }

        case 0x03: // max_udp_payload_size
            if (auto value = assign_varint(received_params_.max_udp_payload_size); !value)
                return std::unexpected(value.error());
            break;

        case 0x04: // initial_max_data
            if (auto value = assign_varint(received_params_.initial_max_data); !value)
                return std::unexpected(value.error());
            break;

        case 0x05: // initial_max_stream_data_bidi_local
            if (auto value = assign_varint(received_params_.initial_max_stream_data_bidi_local); !value)
                return std::unexpected(value.error());
            break;

        case 0x06: // initial_max_stream_data_bidi_remote
            if (auto value = assign_varint(received_params_.initial_max_stream_data_bidi_remote); !value)
                return std::unexpected(value.error());
            break;

        case 0x07: // initial_max_stream_data_uni
            if (auto value = assign_varint(received_params_.initial_max_stream_data_uni); !value)
                return std::unexpected(value.error());
            break;

        case 0x08: // initial_max_streams_bidi
            if (auto value = assign_varint(received_params_.initial_max_streams_bidi); !value)
                return std::unexpected(value.error());
            break;

        case 0x09: // initial_max_streams_uni
            if (auto value = assign_varint(received_params_.initial_max_streams_uni); !value)
                return std::unexpected(value.error());
            break;

        case 0x0a: // ack_delay_exponent
            if (auto value = assign_varint(received_params_.ack_delay_exponent); !value)
                return std::unexpected(value.error());
            break;

        case 0x0b: // max_ack_delay
        {
            auto delay = read_varint();
            if (!delay)
                return std::unexpected(delay.error());
            received_params_.max_ack_delay = std::chrono::milliseconds(*delay);
            break;
        }

        case 0x0e: // active_connection_id_limit
            if (auto value = assign_varint(received_params_.active_connection_id_limit); !value)
                return std::unexpected(value.error());
            break;

        case 0x0c: // Disable Active Migration
            if (!param_value.empty())
                return std::unexpected(make_error_code(quic_errc::transport_parameter_error));
            received_params_.disable_active_migration = true;
            break;

        case 0x00: // original_destination_connection_id
        case 0x0f: // initial_source_connection_id
        case 0x10: // retry_source_connection_id
        {
            if (param_value.empty() || param_value.size() > max_cid_length)
                return std::unexpected(make_error_code(quic_errc::transport_parameter_error));
            const auto cid = connection_id{param_value};
            if (param_type == 0x00)
                received_params_.original_destination_connection_id = cid;
            else if (param_type == 0x0f)
                received_params_.initial_source_connection_id = cid;
            else
                received_params_.retry_source_connection_id = cid;
            break;
        }

        default:
            // Unknown parameter, skip
            break;
        }
    }

    if (received_params_.max_udp_payload_size < 1200 ||
        received_params_.ack_delay_exponent > 20 ||
        received_params_.active_connection_id_limit < 2)
        return std::unexpected(make_error_code(quic_errc::transport_parameter_error));

    return {};
}

} // namespace cnetmod::quic

    #endif // CNETMOD_ENABLE_QUIC
#endif     // CNETMOD_HAS_SSL
