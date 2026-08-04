module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_SSL

    #ifdef CNETMOD_ENABLE_QUIC

        #include <openssl/ssl.h>

export module cnetmod.protocol.quic:crypto;

import std;
import cnetmod.core.ssl;
import cnetmod.core.buffer;
import :types;

namespace cnetmod::quic {

// =============================================================================
// Encryption Level (RFC 9001)
// =============================================================================

export enum class encryption_level
{
    initial,
    early_data,
    handshake,
    application
};

/// Number of TLS 1.3 encryption levels (used for per-level key arrays)
export inline constexpr std::size_t encryption_level_count = 4;

/// Convert an encryption_level to its zero-based array index
export [[nodiscard]] constexpr auto level_index(encryption_level level) noexcept
    -> std::size_t
{
    return static_cast<std::size_t>(level);
}

// =============================================================================
// Handshake result (driven by SSL_do_handshake)
// =============================================================================

export enum class handshake_result
{
    complete,            ///< Handshake finished successfully
    want_read,           ///< Need more data from the peer (CRYPTO frames)
    want_write,          ///< Pending outbound flight should be flushed
    early_data_rejected, ///< 0-RTT data was rejected (client only)
};

// =============================================================================
// Transport Parameters Structure (RFC 9000 §7.4)
// =============================================================================

export struct transport_params
{
    std::uint64_t initial_max_data{1048576};
    std::uint64_t initial_max_stream_data_bidi_local{262144};
    std::uint64_t initial_max_stream_data_bidi_remote{262144};
    std::uint64_t initial_max_stream_data_uni{262144};
    std::uint64_t initial_max_streams_bidi{100};
    std::uint64_t initial_max_streams_uni{100};
    std::uint64_t max_udp_payload_size{65527};
    std::uint64_t ack_delay_exponent{3};
    std::chrono::milliseconds max_ack_delay{25};
    std::uint64_t active_connection_id_limit{2};
    std::chrono::milliseconds idle_timeout{30000};
    bool disable_active_migration{false};
    std::optional<connection_id> original_destination_connection_id;
    std::optional<connection_id> initial_source_connection_id;
    std::optional<connection_id> retry_source_connection_id;
    std::string alpn_selected;
    std::string server_name;
};

} // namespace cnetmod::quic

// =============================================================================
// Hash specialization for transport_params (if needed)
// =============================================================================

namespace std {

template <>
struct hash<cnetmod::quic::transport_params>
{
    auto operator()(const cnetmod::quic::transport_params& params) const noexcept
        -> std::size_t
    {
        std::size_t h = 0;

        // Combine all fields using boost::hash-like technique
        h ^= std::hash<std::uint64_t>{}(params.initial_max_data) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint64_t>{}(params.initial_max_stream_data_bidi_local) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint64_t>{}(params.initial_max_stream_data_bidi_remote) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint64_t>{}(params.initial_max_stream_data_uni) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint64_t>{}(params.initial_max_streams_bidi) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint64_t>{}(params.initial_max_streams_uni) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint64_t>{}(params.max_udp_payload_size) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint64_t>{}(params.ack_delay_exponent) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::int64_t>{}(params.max_ack_delay.count()) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint64_t>{}(params.active_connection_id_limit) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::int64_t>{}(std::chrono::duration_cast<std::chrono::microseconds>(params.idle_timeout).count()) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(params.disable_active_migration) + 0x9e3779b9 + (h << 6) + (h >> 2);

        if (!params.alpn_selected.empty())
        {
            for (char c : params.alpn_selected)
                h ^= static_cast<std::size_t>(static_cast<unsigned char>(c)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        if (!params.server_name.empty())
        {
            for (char c : params.server_name)
                h ^= static_cast<std::size_t>(static_cast<unsigned char>(c)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }

        return h;
    }
};

} // namespace std

namespace cnetmod::quic {

// =============================================================================
// Per-level key material (RFC 9001 §5)
// =============================================================================

/// Key material derived from a TLS 1.3 traffic secret for one direction
/// at one encryption level.
export struct quic_level_keys
{
    std::uint32_t cipher_id{0};         ///< SSL_CIPHER protocol id
    std::vector<std::uint8_t> secret;   ///< Raw traffic secret
    std::vector<std::uint8_t> aead_key; ///< "quic key" (AEAD key)
    std::vector<std::uint8_t> aead_iv;  ///< "quic iv" (12-byte nonce base)
    std::vector<std::uint8_t> hp_key;   ///< "quic hp" (header protection)
    const void* aead{nullptr};          ///< const EVP_AEAD*
    const void* digest{nullptr};        ///< const EVP_MD*
    std::size_t tag_len{16};            ///< AEAD tag length

    /// True once a secret has been installed and keys derived
    [[nodiscard]] auto valid() const noexcept -> bool
    {
        return !secret.empty();
    }
};

/// The role a successfully authenticated 1-RTT packet played in the key
/// lifecycle.  A receiver only advances its key phase after AEAD validation;
/// header protection alone is deliberately not considered proof of a peer
/// update (RFC 9001 §6.1).
export enum class application_read_key_kind
{
    current,
    next,
    previous
};

export struct application_read_key_candidate
{
    const quic_level_keys* keys{};
    bool key_phase{};
    application_read_key_kind kind{application_read_key_kind::current};
};

/// Serialized TLS 1.3 resumption state.  The application owns persistence of
/// this opaque value and must protect it as it contains TLS session material.
export struct session_ticket
{
    std::vector<std::byte> serialized;

    [[nodiscard]] auto empty() const noexcept -> bool
    {
        return serialized.empty();
    }
};

/// A bounded, thread-safe single-use registry for application-owned 0-RTT
/// ticket identifiers.  The identifier must be derived from authenticated
/// ticket state by the server's ticket implementation; never use a client
/// supplied value directly.  This type intentionally does not enable server
/// early data by itself: BoringSSL's default ticket implementation does not
/// expose the offered ticket before it decides whether to accept 0-RTT.
/// Applications which install a ticket callback can use this registry there
/// and reject the ticket/early data when `consume` returns false.
export class early_data_replay_cache
{
public:
    explicit early_data_replay_cache(std::size_t capacity = 65536);

    /// Atomically consume `ticket_id` until `expires_at`. Returns false for a
    /// replay, expired input, empty identifiers, or when capacity is zero.
    [[nodiscard]] auto consume(std::span<const std::byte> ticket_id,
        std::chrono::steady_clock::time_point expires_at) -> bool;

    /// Remove expired entries. Safe to call from a timer or ticket callback.
    void purge_expired(std::chrono::steady_clock::time_point now);

    [[nodiscard]] auto size() const -> std::size_t;

private:
    struct entry
    {
        std::chrono::steady_clock::time_point expires_at;
        std::uint64_t generation{};
    };

    [[nodiscard]] static auto key(std::span<const std::byte> ticket_id)
        -> std::string;

    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, entry> entries_;
    std::uint64_t next_generation_{};
};

/// Install the QUIC server ticket bridge on an application-owned TLS context.
/// The callbacks and replay cache are shared by every worker using that
/// context. This is explicit: without it, server 0-RTT remains disabled.
export [[nodiscard]] auto configure_server_early_data_tickets(ssl_context& context,
    const server_early_data_ticket_callbacks& callbacks)
    -> std::expected<void, std::error_code>;

export enum class early_data_state
{
    disabled,
    pending,
    accepted,
    rejected
};

/// Initial packet keys derived from the destination connection ID.  These are
/// deliberately separate from TLS traffic secrets: RFC 9001 §5.2 requires
/// Initial protection before TLS has produced any secret.
export struct quic_initial_keys
{
    quic_level_keys client;
    quic_level_keys server;
};

/// Derive QUIC v1 Initial keys using the RFC 9001 fixed salt.  `client` is
/// used to protect packets sent by the client and read by the server.
export [[nodiscard]] auto derive_initial_keys(
    quic_version version, const connection_id& destination_cid)
    -> std::expected<quic_initial_keys, std::error_code>;

/// Validate a server Retry packet's RFC 9001/9369 integrity tag against the
/// original client-chosen destination connection ID.
export [[nodiscard]] auto validate_retry_integrity_tag(quic_version version,
    const connection_id& original_destination_cid,
    std::span<const std::byte> retry_packet) -> std::expected<void, std::error_code>;

/// Compute the RFC 9001 / RFC 9369 Retry Integrity Tag for a Retry packet
/// that does not yet contain its trailing 16-byte tag.
export [[nodiscard]] auto make_retry_integrity_tag(quic_version version,
    const connection_id& original_destination_cid,
    std::span<const std::byte> retry_packet_without_tag)
    -> std::expected<std::array<std::byte, 16>, std::error_code>;

/// Seal/open a QUIC payload with explicit key material.  These functions are
/// used for Initial packets (whose keys precede TLS) and are also the common
/// primitive for TLS-installed Handshake and 1-RTT keys.
export [[nodiscard]] auto seal_payload(const quic_level_keys& keys,
    std::span<const std::byte> payload, std::span<const std::byte> header,
    std::uint64_t packet_number)
    -> std::expected<std::vector<std::byte>, std::error_code>;
export [[nodiscard]] auto open_payload(const quic_level_keys& keys,
    std::span<const std::byte> protected_payload,
    std::span<const std::byte> header, std::uint64_t packet_number)
    -> std::expected<std::vector<std::byte>, std::error_code>;

/// Apply/remove RFC 9001 §5.4 header protection in place. `packet_number_offset`
/// identifies the first protected packet-number byte in the complete packet.
export [[nodiscard]] auto protect_header(const quic_level_keys& keys,
    std::span<std::byte> packet, std::size_t packet_number_offset,
    bool long_header) -> std::expected<void, std::error_code>;
export [[nodiscard]] auto unprotect_header(const quic_level_keys& keys,
    std::span<std::byte> packet, std::size_t packet_number_offset,
    bool long_header) -> std::expected<std::size_t, std::error_code>;

// =============================================================================
// QUIC TLS Session - Main interface (thin wrapper around BoringSSL)
// =============================================================================

export class quic_tls_session
{
public:
    ~quic_tls_session();

    /// Non-copyable
    quic_tls_session(const quic_tls_session&) = delete;
    auto operator=(const quic_tls_session&) -> quic_tls_session& = delete;

    /// Factory: Create QUIC client session
    [[nodiscard]] static auto client(ssl_context& ctx, transport_params params = {})
        -> std::expected<std::unique_ptr<quic_tls_session>, std::error_code>;

    /// Factory: Create QUIC server session
    [[nodiscard]] static auto server(ssl_context& ctx, transport_params params = {})
        -> std::expected<std::unique_ptr<quic_tls_session>, std::error_code>;

    // =========================================================================
    // Handshake state machine (RFC 9001 §4)
    // =========================================================================

    /// Drive the TLS handshake. Call after providing new peer data or when
    /// the transport wants to make progress. Outbound handshake data is
    /// buffered and can be retrieved with take_handshake_data().
    [[nodiscard]] auto do_handshake()
        -> std::expected<handshake_result, std::error_code>;

    /// Process post-handshake data previously fed via provide_quic_data()
    /// (e.g. NewSessionTicket messages).
    [[nodiscard]] auto process_post_handshake()
        -> std::expected<void, std::error_code>;

    /// Configure a client resumption ticket before driving the handshake.
    /// Early data remains disabled unless enable_early_data() is also called.
    [[nodiscard]] auto set_resumption_ticket(const session_ticket& ticket)
        -> std::expected<void, std::error_code>;

    /// Return a freshly issued ticket after post-handshake processing.  The
    /// returned opaque bytes may be stored by the application for resumption.
    [[nodiscard]] auto take_resumption_ticket()
        -> std::expected<session_ticket, std::error_code>;

    /// Enable or disable TLS 1.3 early data for this connection. This is
    /// intentionally client-only. Server calls are ignored and keep early
    /// data disabled: a server must install ticket-time anti-replay enforcement
    /// before accepting replayable application bytes.
    void enable_early_data(bool enabled) noexcept;

    /// Enables server 0-RTT only after the caller has installed an
    /// application ticket callback backed by a shared replay cache.
    [[nodiscard]] auto enable_server_early_data() -> std::expected<void, std::error_code>;

    /// Configure server early-data binding context.  It must cover the local
    /// transport parameters and HTTP/3 SETTINGS used to mint tickets.
    [[nodiscard]] auto set_early_data_context(std::span<const std::byte> context)
        -> std::expected<void, std::error_code>;

    [[nodiscard]] auto early_data_accepted() const noexcept -> bool;

    [[nodiscard]] auto early_data_status() const noexcept -> early_data_state;

    /// BoringSSL's machine-readable reason for the final 0-RTT decision.
    /// The numeric value is the corresponding `ssl_early_data_reason_t`.
    [[nodiscard]] auto early_data_reason() const noexcept -> int;

    /// Recover a client connection after BoringSSL rejected offered 0-RTT.
    /// The caller must replay only idempotent application requests.
    [[nodiscard]] auto reset_after_early_data_rejection()
        -> std::expected<void, std::error_code>;

    /// Provide peer handshake data (CRYPTO frame payload) at a level.
    /// Wraps SSL_provide_quic_data.
    [[nodiscard]] auto provide_quic_data(
        encryption_level level,
        std::span<const std::byte> data)
        -> std::expected<void, std::error_code>;

    /// Extract plaintext from internal buffer.
    /// Note: in QUIC mode application data never flows through the TLS
    /// record layer, so this always reports zero bytes available.
    [[nodiscard]] auto extract_plaintext(
        encryption_level level,
        mutable_buffer buf)
        -> std::expected<std::size_t, std::error_code>;

    /// Check if handshake is complete
    [[nodiscard]] auto is_handshake_complete() const noexcept -> bool;

    /// Get current read encryption level
    [[nodiscard]] auto current_encryption_level() const noexcept -> encryption_level;

    // =========================================================================
    // Outbound handshake data (packed into CRYPTO frames by the transport)
    // =========================================================================

    /// Take (move out) buffered handshake data produced by the TLS engine
    /// for the given encryption level. Returns an empty vector if none.
    [[nodiscard]] auto take_handshake_data(encryption_level level)
        -> std::vector<std::byte>;

    /// True if any encryption level has unsent handshake data
    [[nodiscard]] auto has_pending_handshake_data() const noexcept -> bool;

    /// True if BoringSSL requested a flight flush since the last query
    [[nodiscard]] auto handshake_flush_pending() noexcept -> bool;

    // =========================================================================
    // TLS alerts (mapped by the transport to CONNECTION_CLOSE(CRYPTO_ERROR))
    // =========================================================================

    /// Last alert emitted by the TLS engine, if any
    [[nodiscard]] auto pending_alert() const noexcept
        -> std::optional<std::pair<encryption_level, std::uint8_t>>;

    /// Clear the pending alert
    void consume_alert() noexcept;

    // =========================================================================
    // Secrets and key material
    // =========================================================================

    /// Read secret for a given encryption level
    [[nodiscard]] auto read_secret(encryption_level level)
        -> std::optional<std::vector<std::byte>>;

    /// Write secret for a given encryption level
    [[nodiscard]] auto write_secret(encryption_level level)
        -> std::optional<std::vector<std::byte>>;

    /// Derived keys installed for receiving at the given level (nullptr if
    /// the level has not been unlocked yet)
    [[nodiscard]] auto read_keys(encryption_level level) const noexcept
        -> const quic_level_keys*;

    /// Derived keys installed for sending at the given level (nullptr if
    /// the level has not been unlocked yet)
    [[nodiscard]] auto write_keys(encryption_level level) const noexcept
        -> const quic_level_keys*;

    /// Discard a no-longer-permitted encryption level.  Application keys are
    /// intentionally excluded: use the key-update functions below instead.
    void discard_keys(encryption_level level) noexcept;

    /// RFC 9001 §6: begin a local 1-RTT key update.  The next traffic secret
    /// is derived with the "quic ku" label, then the short-header key phase is
    /// toggled for subsequently protected packets.
    [[nodiscard]] auto initiate_key_update() -> std::expected<void, std::error_code>;
    [[nodiscard]] auto application_write_key_phase() const noexcept -> bool;

    /// Candidate keys used to remove short-header protection and authenticate
    /// a received 1-RTT packet.  The previous generation is retained only for
    /// a bounded reordering window after a confirmed peer update.
    [[nodiscard]] auto application_read_key_candidates() const
        -> std::vector<application_read_key_candidate>;
    void confirm_application_read_key(application_read_key_kind kind);
    void discard_expired_application_read_keys(
        std::chrono::steady_clock::time_point now,
        std::chrono::steady_clock::duration retention) noexcept;
    [[nodiscard]] auto application_read_key_phase() const noexcept -> bool;

    /// HKDF-Expand-Label (RFC 8446 §7.1). `digest` is a const EVP_MD*.
    [[nodiscard]] static auto hkdf_expand_label(
        std::span<const std::uint8_t> secret,
        std::string_view label,
        std::span<const std::uint8_t> context,
        std::size_t output_len,
        const void* digest)
        -> std::vector<std::uint8_t>;

    // =========================================================================
    // Packet protection (RFC 9001 §5)
    // =========================================================================

    /// Apply header protection (encrypt first-byte low bits + PN bytes)
    [[nodiscard]] auto apply_header_protection(
        std::span<const std::byte> sample,
        std::span<std::byte> protected_value)
        -> std::expected<void, std::error_code>;

    /// Remove header protection (decrypt first-byte low bits + PN bytes)
    [[nodiscard]] auto remove_header_protection(
        std::span<const std::byte> sample,
        std::span<const std::byte> header,
        std::span<std::byte> unprotected)
        -> std::expected<void, std::error_code>;

    /// Encrypt packet payload using the internal packet number counter.
    /// Returns (ciphertext || tag, tag).
    [[nodiscard]] auto encrypt_packet(
        std::span<const std::byte> payload,
        encryption_level level)
        -> std::expected<std::pair<std::vector<std::byte>, std::array<std::byte, 16>>, std::error_code>;

    /// Encrypt packet payload with an explicit packet number.
    /// `header` is bound as AEAD associated data (may be empty).
    [[nodiscard]] auto encrypt_packet(
        std::span<const std::byte> payload,
        std::span<const std::byte> header,
        encryption_level level,
        std::uint64_t packet_number)
        -> std::expected<std::pair<std::vector<std::byte>, std::array<std::byte, 16>>, std::error_code>;

    /// Decrypt packet payload using the internal packet number counter.
    [[nodiscard]] auto decrypt_packet(
        std::span<const std::byte> protected_payload,
        encryption_level level)
        -> std::expected<std::vector<std::byte>, std::error_code>;

    /// Decrypt packet payload with an explicit packet number.
    /// `header` is bound as AEAD associated data (may be empty).
    [[nodiscard]] auto decrypt_packet(
        std::span<const std::byte> protected_payload,
        std::span<const std::byte> header,
        encryption_level level,
        std::uint64_t packet_number)
        -> std::expected<std::vector<std::byte>, std::error_code>;

    // =========================================================================
    // Transport parameters (RFC 9000 §7.4)
    // =========================================================================

    /// Configure the local transport parameters to advertise
    /// (must be called before do_handshake()).
    void set_transport_params(transport_params params);

    /// Add the server-only CID transport parameters required after Retry.
    /// This must be called before SSL_do_handshake().
    [[nodiscard]] auto configure_retry_transport_parameters(
        connection_id original_destination_cid, connection_id retry_source_cid)
        -> std::expected<void, std::error_code>;

    /// Set the endpoint's mandatory initial_source_connection_id parameter
    /// before the TLS handshake. Retry configuration augments this value.
    [[nodiscard]] auto configure_initial_source_connection_id(connection_id cid)
        -> std::expected<void, std::error_code>;

    /// Encode transport parameters to bytes
    [[nodiscard]] auto encode_transport_params()
        -> std::expected<std::vector<std::byte>, std::error_code>;

    /// Decode transport parameters from bytes
    [[nodiscard]] auto decode_transport_params(
        std::span<const std::byte> encoded)
        -> std::expected<void, std::error_code>;

    /// Transport parameters received from the peer (valid once the
    /// handshake completes)
    [[nodiscard]] auto received_transport_params() const noexcept
        -> const transport_params&;

    /// Get server name (SNI for client)
    [[nodiscard]] auto get_server_name() const noexcept -> std::string_view;

    /// Get selected ALPN protocol
    [[nodiscard]] auto get_alpn_selected() const noexcept -> std::string_view;

    /// Get native SSL pointer
    [[nodiscard]] auto native() const noexcept -> SSL*
    {
        return ssl_;
    }

private:
    explicit quic_tls_session(SSL* ssl);

    /// Register QUIC method callbacks with BoringSSL
    void register_quic_callbacks();

    /// Install encoded local transport parameters into the SSL object
    [[nodiscard]] auto install_transport_params() -> bool;

    /// Parse peer transport parameters after handshake completion
    void consume_peer_transport_params();

    /// Install a traffic secret and derive AEAD/IV/HP keys for one level
    [[nodiscard]] auto install_secret(
        encryption_level level,
        std::uint32_t cipher_id,
        std::span<const std::uint8_t> secret,
        bool for_reading) -> bool;

    [[nodiscard]] static auto derive_next_keys(const quic_level_keys& current)
        -> std::optional<quic_level_keys>;

    // =========================================================================
    // BoringSSL SSL_QUIC_METHOD callbacks (C-linkage compatible)
    // =========================================================================

    static auto cb_set_read_secret(
        SSL* ssl, enum ssl_encryption_level_t level,
        const SSL_CIPHER* cipher,
        const std::uint8_t* secret, std::size_t secret_len) -> int;

    static auto cb_set_write_secret(
        SSL* ssl, enum ssl_encryption_level_t level,
        const SSL_CIPHER* cipher,
        const std::uint8_t* secret, std::size_t secret_len) -> int;

    static auto cb_add_handshake_data(
        SSL* ssl, enum ssl_encryption_level_t level,
        const std::uint8_t* data, std::size_t len) -> int;

    static auto cb_flush_flight(SSL* ssl) -> int;

    static auto cb_send_alert(
        SSL* ssl, enum ssl_encryption_level_t level, std::uint8_t alert) -> int;

    // =========================================================================
    // State
    // =========================================================================

    SSL* ssl_ = nullptr;

    /// Per-level installed key material
    std::array<quic_level_keys, encryption_level_count> read_keys_{};
    std::array<quic_level_keys, encryption_level_count> write_keys_{};

    // 1-RTT key generations are kept separate from the TLS-installed
    // application slots.  This makes bidirectional updates independent and
    // avoids accidentally reusing a discarded Handshake secret.
    std::optional<quic_level_keys> next_read_application_keys_;
    std::optional<quic_level_keys> previous_read_application_keys_;
    std::optional<quic_level_keys> next_write_application_keys_;
    bool read_application_key_phase_{};
    bool write_application_key_phase_{};
    std::optional<std::chrono::steady_clock::time_point> previous_read_key_since_;

    /// Outbound handshake data buffered per level (becomes CRYPTO frames)
    std::array<std::vector<std::byte>, encryption_level_count> handshake_send_{};

    /// Last TLS alert emitted via the send_alert callback
    std::optional<std::pair<encryption_level, std::uint8_t>> pending_alert_;
    bool early_data_rejected_ = false;
    bool early_data_enabled_ = false;
    bool server_early_data_authorized_ = false;
    bool server_mode_ = false;

    /// Set by flush_flight, cleared by handshake_flush_pending()
    bool flush_pending_{false};

    /// Internal packet number counters used by the counter-based
    /// encrypt_packet/decrypt_packet overloads
    std::array<std::uint64_t, encryption_level_count> send_pn_{};
    std::array<std::uint64_t, encryption_level_count> recv_pn_{};

    transport_params received_params_;
    transport_params sent_params_;
    std::optional<std::error_code> transport_params_error_;
};

} // namespace cnetmod::quic

    #endif // CNETMOD_ENABLE_QUIC
#endif     // CNETMOD_HAS_SSL
