module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.quic:frame;

import std;
import :types;
import :varint;

namespace cnetmod::quic {

// =============================================================================
// Frame Type (RFC 9000 §19)
// =============================================================================

export enum class frame_type : std::uint64_t
{
    padding = 0x00,
    ping = 0x01,
    ack = 0x02,
    ack_ecn = 0x03,
    reset_stream = 0x04,
    stop_sending = 0x05,
    crypto = 0x06,
    new_token = 0x07,
    stream = 0x08,
    max_data = 0x10,
    max_stream_data = 0x11,
    max_streams_bidi = 0x12,
    max_streams_uni = 0x13,
    data_blocked = 0x14,
    stream_data_blocked = 0x15,
    streams_blocked_bidi = 0x16,
    streams_blocked_uni = 0x17,
    new_connection_id = 0x18,
    retire_connection_id = 0x19,
    path_challenge = 0x1A,
    path_response = 0x1B,
    connection_close = 0x1C,
    connection_close_app = 0x1D,
    handshake_done = 0x1E,
};

// =============================================================================
// Frame Structs
// =============================================================================

export struct padding_frame {};

export struct ping_frame {};

export struct ack_range
{
    std::uint64_t gap{};
    std::uint64_t ack_range_length{};
};

export struct ack_frame
{
    std::uint64_t largest_acked{};
    std::uint64_t ack_delay{};
    std::uint64_t ack_range_count{};
    std::uint64_t first_ack_range{};
    std::vector<ack_range> ack_ranges{};
    // ECN counts (only for ack_ecn frame type)
    bool has_ecn{false};
    std::uint64_t ect_0_count{};
    std::uint64_t ect_1_count{};
    std::uint64_t ecn_ce_count{};
};

export struct reset_stream_frame
{
    std::uint64_t stream_id{};
    std::uint64_t application_error_code{};
    std::uint64_t final_size{};
};

export struct stop_sending_frame
{
    std::uint64_t stream_id{};
    std::uint64_t application_error_code{};
};

export struct crypto_frame
{
    std::uint64_t offset{};
    std::span<const std::byte> data{};
};

export struct new_token_frame
{
    std::span<const std::byte> token{};
};

export struct stream_frame
{
    std::uint64_t stream_id{};
    std::uint64_t offset{};
    std::span<const std::byte> data{};
    bool fin{false};
};

export struct max_data_frame
{
    std::uint64_t maximum{};
};

export struct max_stream_data_frame
{
    std::uint64_t stream_id{};
    std::uint64_t maximum{};
};

export struct max_streams_frame
{
    std::uint64_t maximum{};
    bool bidirectional{};
};

export struct data_blocked_frame
{
    std::uint64_t maximum_data{};
};

export struct stream_data_blocked_frame
{
    std::uint64_t stream_id{};
    std::uint64_t maximum_stream_data{};
};

export struct streams_blocked_frame
{
    std::uint64_t maximum{};
    bool bidirectional{};
};

export struct new_connection_id_frame
{
    std::uint64_t sequence_number{};
    std::uint64_t retire_prior_to{};
    connection_id cid{};
    std::array<std::byte, 16> stateless_reset_token{};
};

export struct retire_connection_id_frame
{
    std::uint64_t sequence_number{};
};

export struct path_challenge_frame
{
    std::array<std::byte, 8> data{};
};

export struct path_response_frame
{
    std::array<std::byte, 8> data{};
};

export struct connection_close_frame
{
    std::uint64_t error_code{};
    std::uint64_t frame_type_value{};
    std::string reason{};
    bool is_application_error{false};
};

export struct handshake_done_frame {};

// =============================================================================
// Frame Variant
// =============================================================================

export using quic_frame_variant = std::variant<
    padding_frame,
    ping_frame,
    ack_frame,
    reset_stream_frame,
    stop_sending_frame,
    crypto_frame,
    new_token_frame,
    stream_frame,
    max_data_frame,
    max_stream_data_frame,
    max_streams_frame,
    data_blocked_frame,
    stream_data_blocked_frame,
    streams_blocked_frame,
    new_connection_id_frame,
    retire_connection_id_frame,
    path_challenge_frame,
    path_response_frame,
    connection_close_frame,
    handshake_done_frame>;

// =============================================================================
// Frame Decoding / Encoding
// =============================================================================

/// Decode a single frame from raw bytes.
/// Returns decoded frame variant and bytes consumed.
export [[nodiscard]] auto decode_frame(std::span<const std::byte> data)
    -> std::expected<std::pair<quic_frame_variant, std::size_t>,
        std::error_code>;

/// Encode a frame to bytes.
export auto encode_frame(const padding_frame& f) -> std::vector<std::byte>;
export auto encode_frame(const ping_frame& f) -> std::vector<std::byte>;
export auto encode_frame(const ack_frame& f) -> std::vector<std::byte>;
export auto encode_frame(const reset_stream_frame& f) -> std::vector<std::byte>;
export auto encode_frame(const stop_sending_frame& f) -> std::vector<std::byte>;
export auto encode_frame(const crypto_frame& f) -> std::vector<std::byte>;
export auto encode_frame(const new_token_frame& f) -> std::vector<std::byte>;
export auto encode_frame(const stream_frame& f) -> std::vector<std::byte>;
export auto encode_frame(const max_data_frame& f) -> std::vector<std::byte>;
export auto encode_frame(const max_stream_data_frame& f) -> std::vector<std::byte>;
export auto encode_frame(const max_streams_frame& f) -> std::vector<std::byte>;
export auto encode_frame(const data_blocked_frame& f) -> std::vector<std::byte>;
export auto encode_frame(const stream_data_blocked_frame& f)
    -> std::vector<std::byte>;
export auto encode_frame(const streams_blocked_frame& f)
    -> std::vector<std::byte>;
export auto encode_frame(const new_connection_id_frame& f)
    -> std::vector<std::byte>;
export auto encode_frame(const retire_connection_id_frame& f)
    -> std::vector<std::byte>;
export auto encode_frame(const path_challenge_frame& f)
    -> std::vector<std::byte>;
export auto encode_frame(const path_response_frame& f)
    -> std::vector<std::byte>;
export auto encode_frame(const connection_close_frame& f)
    -> std::vector<std::byte>;
export auto encode_frame(const handshake_done_frame& f)
    -> std::vector<std::byte>;

// =============================================================================
// Frame Helpers
// =============================================================================

/// Check if a frame is ack-eliciting (RFC 9000 §13.2.1)
export auto is_ack_eliciting(const quic_frame_variant& f) -> bool;

/// Check if a frame is a probing frame (RFC 9000 §9.1)
export auto is_probing(const quic_frame_variant& f) -> bool;

} // namespace cnetmod::quic
