module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.quic;

import std;
import :types;

namespace cnetmod::quic {

// =============================================================================
// Connection ID
// =============================================================================

connection_id::connection_id(std::span<const std::byte> data)
{
    const auto len = std::min<std::size_t>(data.size(), max_cid_length);
    std::copy_n(data.data(), len, data_.begin());
    length_ = static_cast<std::uint8_t>(len);
}

connection_id::connection_id(const std::byte* data, std::uint8_t length)
{
    const auto len = std::min<std::size_t>(length, max_cid_length);
    std::copy_n(data, len, data_.begin());
    length_ = static_cast<std::uint8_t>(len);
}

auto connection_id::data() const noexcept -> const std::byte*
{
    return data_.data();
}

auto connection_id::size() const noexcept -> std::uint8_t
{
    return length_;
}

auto connection_id::empty() const noexcept -> bool
{
    return length_ == 0;
}

auto connection_id::operator==(const connection_id& other) const noexcept -> bool
{
    if (length_ != other.length_)
        return false;
    return std::equal(data_.begin(), data_.begin() + length_,
        other.data_.begin());
}

auto connection_id::operator<=>(const connection_id& other) const noexcept
    -> std::strong_ordering
{
    if (auto cmp = length_ <=> other.length_; cmp != 0)
        return cmp;
    return std::lexicographical_compare_three_way(
        data_.begin(), data_.begin() + length_,
        other.data_.begin(), other.data_.begin() + other.length_);
}

auto connection_id::to_string() const -> std::string
{
    std::string result;
    result.reserve(static_cast<std::size_t>(length_) * 2);
    static constexpr std::string_view hex_digits = "0123456789abcdef";
    for (std::uint8_t i = 0; i < length_; ++i)
    {
        const auto b = std::to_integer<std::uint8_t>(data_[i]);
        result += hex_digits[(b >> 4) & 0x0f];
        result += hex_digits[b & 0x0f];
    }
    return result;
}

auto format_as(const connection_id& cid) -> std::string
{
    return cid.to_string();
}

// =============================================================================
// Error Category
// =============================================================================

namespace detail {

    class quic_error_category_impl final : public std::error_category
    {
    public:
        auto name() const noexcept -> const char* override
        {
            return "quic";
        }

        auto message(int value) const -> std::string override
        {
            switch (static_cast<quic_errc>(value))
            {
            case quic_errc::no_error:
                return "no error";
            case quic_errc::internal_error:
                return "internal error";
            case quic_errc::connection_refused:
                return "connection refused";
            case quic_errc::flow_control_error:
                return "flow control error";
            case quic_errc::stream_limit_error:
                return "stream limit error";
            case quic_errc::stream_state_error:
                return "stream state error";
            case quic_errc::final_size_error:
                return "final size error";
            case quic_errc::frame_encoding_error:
                return "frame encoding error";
            case quic_errc::transport_parameter_error:
                return "transport parameter error";
            case quic_errc::connection_id_limit_error:
                return "connection ID limit error";
            case quic_errc::protocol_violation:
                return "protocol violation";
            case quic_errc::invalid_token:
                return "invalid token";
            case quic_errc::application_error:
                return "application error";
            case quic_errc::crypto_buffer_exceeded:
                return "crypto buffer exceeded";
            case quic_errc::key_update_error:
                return "key update error";
            case quic_errc::aead_limit_reached:
                return "AEAD limit reached";
            case quic_errc::no_viable_path:
                return "no viable path";
            case quic_errc::crypto_error:
                return "crypto error";
            }
            if (value >= 0x100 && value <= 0x1ff)
                return std::format("crypto alert {}", value - 0x100);
            return "unknown QUIC error";
        }
    };

    auto quic_category_instance() -> const std::error_category&
    {
        static const quic_error_category_impl instance;
        return instance;
    }

} // namespace detail

auto make_error_code(quic_errc value) noexcept -> std::error_code
{
    return {static_cast<int>(value), detail::quic_category_instance()};
}

} // namespace cnetmod::quic
