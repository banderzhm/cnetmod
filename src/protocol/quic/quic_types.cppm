module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.quic:types;

import std;

namespace cnetmod::quic {

// =============================================================================
// Constants (RFC 9000)
// =============================================================================

export inline constexpr std::size_t max_cid_length = 20;
/// Packet construction target used by the transport's pacing/frame budget.
/// A peer is allowed to send a larger Initial flight (for example one carrying
/// a complete TLS ClientHello), so this is not a receive-buffer bound.
export inline constexpr std::size_t min_initial_pkt_size = 1200;
export inline constexpr std::size_t max_udp_payload = 1200;
/// Largest UDP payload accepted by IPv4/IPv6 UDP APIs.  Receive paths use this
/// bound to avoid silently truncating valid QUIC Initial and Handshake packets.
export inline constexpr std::size_t max_udp_receive_payload = 65527;
export inline constexpr std::uint32_t quic_version_v1 = 0x00000001;
export inline constexpr std::uint32_t quic_version_v2 = 0x6b3343cf;

// =============================================================================
// QUIC Version
// =============================================================================

export enum class quic_version : std::uint32_t
{
    v1 = 0x00000001,
    v2 = 0x6b3343cf,
};

// =============================================================================
// QUIC Role
// =============================================================================

export enum class quic_role
{
    client,
    server,
};

// =============================================================================
// Connection ID
// =============================================================================

export class connection_id
{
public:
    connection_id() noexcept = default;

    explicit connection_id(std::span<const std::byte> data);

    connection_id(const std::byte* data, std::uint8_t length);

    [[nodiscard]] auto data() const noexcept -> const std::byte*;
    [[nodiscard]] auto size() const noexcept -> std::uint8_t;
    [[nodiscard]] auto empty() const noexcept -> bool;

    auto operator==(const connection_id& other) const noexcept -> bool;
    auto operator<=>(const connection_id& other) const noexcept
        -> std::strong_ordering;

    [[nodiscard]] auto to_string() const -> std::string;

private:
    std::array<std::byte, max_cid_length> data_{};
    std::uint8_t length_{0};
};

export auto format_as(const connection_id& cid) -> std::string;

} // namespace cnetmod::quic

// =============================================================================
// Hash specialization for connection_id
// =============================================================================

template <>
struct std::hash<cnetmod::quic::connection_id>
{
    auto operator()(const cnetmod::quic::connection_id& cid) const noexcept
        -> std::size_t
    {
        std::size_t h = 0;
        for (std::uint8_t i = 0; i < cid.size(); ++i)
        {
            h ^= std::to_integer<std::size_t>(cid.data()[i]) +
                0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

namespace cnetmod::quic {

// =============================================================================
// Stream ID Helpers
// =============================================================================

export using stream_id = std::uint64_t;

// Type aliases for use across modules
using clock_type = std::chrono::steady_clock;
using time_point = clock_type::time_point;
using duration_type = std::chrono::steady_clock::duration;

export enum class stream_type_tag
{
    client_bidirectional,
    server_bidirectional,
    client_unidirectional,
    server_unidirectional,
};

// =============================================================================
// Packet Number Space (RFC 9002 §6)
// =============================================================================

export enum class pn_space
{
    initial,
    handshake,
    application,
};

export constexpr auto is_client_initiated(stream_id id) noexcept -> bool
{
    return (id & 0x01) == 0;
}

export constexpr auto is_server_initiated(stream_id id) noexcept -> bool
{
    return (id & 0x01) == 1;
}

export constexpr auto is_bidirectional(stream_id id) noexcept -> bool
{
    return (id & 0x02) == 0;
}

export constexpr auto is_unidirectional(stream_id id) noexcept -> bool
{
    return (id & 0x02) == 2;
}

export constexpr auto stream_type(stream_id id) noexcept -> stream_type_tag
{
    return static_cast<stream_type_tag>(id & 0x03);
}

// =============================================================================
// QUIC Error Codes (RFC 9000 §20)
// =============================================================================

export enum class quic_errc : std::uint64_t
{
    no_error = 0x0,
    internal_error = 0x1,
    connection_refused = 0x2,
    flow_control_error = 0x3,
    stream_limit_error = 0x4,
    stream_state_error = 0x5,
    final_size_error = 0x6,
    frame_encoding_error = 0x7,
    transport_parameter_error = 0x8,
    connection_id_limit_error = 0x9,
    protocol_violation = 0xA,
    invalid_token = 0xB,
    application_error = 0xC,
    crypto_buffer_exceeded = 0xD,
    key_update_error = 0xE,
    aead_limit_reached = 0xF,
    no_viable_path = 0x10,
    crypto_error = 0x100,
};

namespace detail {
    auto quic_category_instance() -> const std::error_category&;
}

export auto make_error_code(quic_errc e) noexcept -> std::error_code;

/// Implemented in the QUIC crypto partition. A shared instance must cover all
/// server workers which can receive the same ticket.
export class early_data_replay_cache;

/// Application-owned ticket callbacks. The ticket identity must be derived
/// from authenticated ticket plaintext, never from ClientHello input. `seal`
/// and `open` own key selection and rotation; the shared replay cache is
/// atomically consumed only when a client actually offers 0-RTT.
export struct server_early_data_ticket
{
    std::vector<std::byte> plaintext;
    std::vector<std::byte> identity;
    std::chrono::steady_clock::time_point early_data_expires_at;
};

export struct server_early_data_ticket_callbacks
{
    /// Largest ciphertext expansion produced by `seal` for a TLS ticket.
    std::size_t max_overhead{64};
    std::function<std::expected<std::vector<std::byte>, std::error_code>(
        std::span<const std::byte>)>
        seal;
    std::function<std::expected<server_early_data_ticket, std::error_code>(
        std::span<const std::byte>)>
        open;
    std::shared_ptr<early_data_replay_cache> replay_cache;
};

// =============================================================================
// QUIC Configuration
// =============================================================================

export struct quic_config
{
    std::chrono::milliseconds idle_timeout{30000};
    /// Largest UDP datagram this endpoint accepts and advertises to its peer.
    std::uint64_t max_udp_payload_size{max_udp_receive_payload};
    std::uint64_t max_data{1048576};
    std::uint64_t max_stream_data{262144};
    std::uint64_t max_streams_bidi{100};
    std::uint64_t max_streams_uni{100};
    std::uint8_t cid_length{8};
    /// Maximum locally issued CIDs kept active in parallel.  The peer's
    /// active_connection_id_limit remains the authoritative upper bound.
    std::uint64_t active_connection_id_limit{4};
    /// Optional listener-owned token generator.  Servers use this to derive
    /// reset tokens from a rotating secret instead of creating per-connection
    /// opaque state; connections retain each issued value for its CID life.
    std::function<std::expected<std::array<std::byte, 16>, std::error_code>(const connection_id&)>
        stateless_reset_token_generator;
    /// TLS server name used for SNI and certificate hostname validation.
    /// This is intentionally not a QUIC transport parameter.
    std::string server_name;
    /// Server-only 0-RTT binding context. It must be a stable serialization
    /// of the advertised QUIC transport parameters and HTTP/3 SETTINGS. An
    /// empty context keeps 0-RTT disabled, which is the secure default.
    std::vector<std::byte> early_data_context;
    /// Server-only, explicit application ticket implementation. Leaving this
    /// unset preserves the TLS context's existing ticket behavior and forces
    /// server 0-RTT rejection.
    std::shared_ptr<server_early_data_ticket_callbacks> early_data_tickets;
};

} // namespace cnetmod::quic

template <>
struct std::is_error_code_enum<cnetmod::quic::quic_errc> : std::true_type
{
};
