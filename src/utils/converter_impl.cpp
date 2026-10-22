module cnetmod.utils.converter;

import std;

namespace utils::conv {

auto to_bytes(std::string_view value) noexcept -> std::span<const std::byte>
{
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}

auto to_string_view(std::span<const std::byte> value) noexcept -> std::string_view
{
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

// =============================================================================
// Byte Swap Implementation
// =============================================================================

std::uint16_t bswap16(std::uint16_t v) noexcept
{
    return static_cast<std::uint16_t>((v >> 8) | (v << 8));
}

std::uint32_t bswap32(std::uint32_t v) noexcept
{
    return ((v >> 24) & 0x000000FF) | ((v >> 8) & 0x0000FF00) | ((v << 8) & 0x00FF0000) | ((v << 24) & 0xFF000000);
}

std::uint64_t bswap64(std::uint64_t v) noexcept
{
    return ((v >> 56) & 0x00000000000000FF) | ((v >> 40) & 0x000000000000FF00) | ((v >> 24) & 0x0000000000FF0000) | ((v >> 8) & 0x00000000FF000000) | ((v << 8) & 0x000000FF00000000) | ((v << 24) & 0x0000FF0000000000) | ((v << 40) & 0x00FF000000000000) | ((v << 56) & 0xFF00000000000000);
}

// =============================================================================
// Host <-> Network (Big-Endian)
// =============================================================================

std::uint16_t hton(std::uint16_t v) noexcept
{
    if constexpr (endian::native == endian::big)
        return v;
    else
        return bswap16(v);
}

std::uint32_t hton(std::uint32_t v) noexcept
{
    if constexpr (endian::native == endian::big)
        return v;
    else
        return bswap32(v);
}

std::uint64_t hton(std::uint64_t v) noexcept
{
    if constexpr (endian::native == endian::big)
        return v;
    else
        return bswap64(v);
}

std::uint16_t ntoh(std::uint16_t v) noexcept
{
    return hton(v);
}

std::uint32_t ntoh(std::uint32_t v) noexcept
{
    return hton(v);
}

std::uint64_t ntoh(std::uint64_t v) noexcept
{
    return hton(v);
}

// =============================================================================
// Host <-> Little-Endian
// =============================================================================

std::uint16_t htole(std::uint16_t v) noexcept
{
    if constexpr (endian::native == endian::little)
        return v;
    else
        return bswap16(v);
}

std::uint32_t htole(std::uint32_t v) noexcept
{
    if constexpr (endian::native == endian::little)
        return v;
    else
        return bswap32(v);
}

std::uint64_t htole(std::uint64_t v) noexcept
{
    if constexpr (endian::native == endian::little)
        return v;
    else
        return bswap64(v);
}

std::uint16_t letoh(std::uint16_t v) noexcept
{
    return htole(v);
}

std::uint32_t letoh(std::uint32_t v) noexcept
{
    return htole(v);
}

std::uint64_t letoh(std::uint64_t v) noexcept
{
    return htole(v);
}

// =============================================================================
// Buffer Read Operations - Big-Endian
// =============================================================================

std::uint16_t read_be16(std::span<const std::uint8_t> data, std::size_t offset)
{
    if (offset + 2 > data.size())
        return 0;
    return (static_cast<std::uint16_t>(data[offset]) << 8) |
        static_cast<std::uint16_t>(data[offset + 1]);
}

std::uint32_t read_be32(std::span<const std::uint8_t> data, std::size_t offset)
{
    if (offset + 4 > data.size())
        return 0;
    return (static_cast<std::uint32_t>(data[offset]) << 24) |
        (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
        (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
        static_cast<std::uint32_t>(data[offset + 3]);
}

std::uint64_t read_be64(std::span<const std::uint8_t> data, std::size_t offset)
{
    if (offset + 8 > data.size())
        return 0;
    return (static_cast<std::uint64_t>(data[offset]) << 56) |
        (static_cast<std::uint64_t>(data[offset + 1]) << 48) |
        (static_cast<std::uint64_t>(data[offset + 2]) << 40) |
        (static_cast<std::uint64_t>(data[offset + 3]) << 32) |
        (static_cast<std::uint64_t>(data[offset + 4]) << 24) |
        (static_cast<std::uint64_t>(data[offset + 5]) << 16) |
        (static_cast<std::uint64_t>(data[offset + 6]) << 8) |
        static_cast<std::uint64_t>(data[offset + 7]);
}

// =============================================================================
// Buffer Write Operations - Big-Endian
// =============================================================================

void write_be16(std::span<std::uint8_t> data, std::uint16_t value, std::size_t offset)
{
    if (offset + 2 > data.size())
        return;
    data[offset] = static_cast<std::uint8_t>(value >> 8);
    data[offset + 1] = static_cast<std::uint8_t>(value & 0xFF);
}

void write_be32(std::span<std::uint8_t> data, std::uint32_t value, std::size_t offset)
{
    if (offset + 4 > data.size())
        return;
    data[offset] = static_cast<std::uint8_t>(value >> 24);
    data[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    data[offset + 2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    data[offset + 3] = static_cast<std::uint8_t>(value & 0xFF);
}

void write_be64(std::span<std::uint8_t> data, std::uint64_t value, std::size_t offset)
{
    if (offset + 8 > data.size())
        return;
    data[offset] = static_cast<std::uint8_t>(value >> 56);
    data[offset + 1] = static_cast<std::uint8_t>((value >> 48) & 0xFF);
    data[offset + 2] = static_cast<std::uint8_t>((value >> 40) & 0xFF);
    data[offset + 3] = static_cast<std::uint8_t>((value >> 32) & 0xFF);
    data[offset + 4] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    data[offset + 5] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    data[offset + 6] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    data[offset + 7] = static_cast<std::uint8_t>(value & 0xFF);
}

// =============================================================================
// Buffer Read Operations - Little-Endian
// =============================================================================

std::uint16_t read_le16(std::span<const std::uint8_t> data, std::size_t offset)
{
    if (offset + 2 > data.size())
        return 0;
    return static_cast<std::uint16_t>(data[offset]) |
        (static_cast<std::uint16_t>(data[offset + 1]) << 8);
}

std::uint32_t read_le32(std::span<const std::uint8_t> data, std::size_t offset)
{
    if (offset + 4 > data.size())
        return 0;
    return static_cast<std::uint32_t>(data[offset]) |
        (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::uint64_t read_le64(std::span<const std::uint8_t> data, std::size_t offset)
{
    if (offset + 8 > data.size())
        return 0;
    return static_cast<std::uint64_t>(data[offset]) |
        (static_cast<std::uint64_t>(data[offset + 1]) << 8) |
        (static_cast<std::uint64_t>(data[offset + 2]) << 16) |
        (static_cast<std::uint64_t>(data[offset + 3]) << 24) |
        (static_cast<std::uint64_t>(data[offset + 4]) << 32) |
        (static_cast<std::uint64_t>(data[offset + 5]) << 40) |
        (static_cast<std::uint64_t>(data[offset + 6]) << 48) |
        (static_cast<std::uint64_t>(data[offset + 7]) << 56);
}

// =============================================================================
// Buffer Write Operations - Little-Endian
// =============================================================================

void write_le16(std::span<std::uint8_t> data, std::uint16_t value, std::size_t offset)
{
    if (offset + 2 > data.size())
        return;
    data[offset] = static_cast<std::uint8_t>(value & 0xFF);
    data[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void write_le32(std::span<std::uint8_t> data, std::uint32_t value, std::size_t offset)
{
    if (offset + 4 > data.size())
        return;
    data[offset] = static_cast<std::uint8_t>(value & 0xFF);
    data[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    data[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    data[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

void write_le64(std::span<std::uint8_t> data, std::uint64_t value, std::size_t offset)
{
    if (offset + 8 > data.size())
        return;
    data[offset] = static_cast<std::uint8_t>(value & 0xFF);
    data[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    data[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    data[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    data[offset + 4] = static_cast<std::uint8_t>((value >> 32) & 0xFF);
    data[offset + 5] = static_cast<std::uint8_t>((value >> 40) & 0xFF);
    data[offset + 6] = static_cast<std::uint8_t>((value >> 48) & 0xFF);
    data[offset + 7] = static_cast<std::uint8_t>(value >> 56);
}

// =============================================================================
// RegisterConverter Implementation
// =============================================================================

std::int16_t RegisterConverter::to_int16(std::uint16_t reg) noexcept
{
    return static_cast<std::int16_t>(reg);
}

std::uint16_t RegisterConverter::to_uint16(std::uint16_t reg) noexcept
{
    return reg;
}

std::int32_t RegisterConverter::to_int32_hilo(std::uint16_t high, std::uint16_t low) noexcept
{
    std::uint32_t value = (static_cast<std::uint32_t>(high) << 16) | low;
    return static_cast<std::int32_t>(value);
}

std::int32_t RegisterConverter::to_int32_lohi(std::uint16_t low, std::uint16_t high) noexcept
{
    return to_int32_hilo(high, low);
}

std::uint32_t RegisterConverter::to_uint32_hilo(std::uint16_t high, std::uint16_t low) noexcept
{
    return (static_cast<std::uint32_t>(high) << 16) | low;
}

std::uint32_t RegisterConverter::to_uint32_lohi(std::uint16_t low, std::uint16_t high) noexcept
{
    return to_uint32_hilo(high, low);
}

float RegisterConverter::to_float_hilo(std::uint16_t high, std::uint16_t low) noexcept
{
    std::uint32_t value = to_uint32_hilo(high, low);
    return std::bit_cast<float>(value);
}

float RegisterConverter::to_float_lohi(std::uint16_t low, std::uint16_t high) noexcept
{
    return to_float_hilo(high, low);
}

double RegisterConverter::to_double_hilo(std::uint16_t h1, std::uint16_t h2, std::uint16_t l1, std::uint16_t l2) noexcept
{
    std::uint64_t value = (static_cast<std::uint64_t>(h1) << 48) |
        (static_cast<std::uint64_t>(h2) << 32) |
        (static_cast<std::uint64_t>(l1) << 16) |
        static_cast<std::uint64_t>(l2);
    return std::bit_cast<double>(value);
}

std::uint16_t RegisterConverter::from_int16(std::int16_t value) noexcept
{
    return static_cast<std::uint16_t>(value);
}

std::array<std::uint16_t, 2> RegisterConverter::from_int32_hilo(std::int32_t value) noexcept
{
    std::uint32_t uval = static_cast<std::uint32_t>(value);
    return {
        static_cast<std::uint16_t>(uval >> 16),
        static_cast<std::uint16_t>(uval & 0xFFFF)};
}

std::array<std::uint16_t, 2> RegisterConverter::from_int32_lohi(std::int32_t value) noexcept
{
    auto result = from_int32_hilo(value);
    return {result[1], result[0]};
}

std::array<std::uint16_t, 2> RegisterConverter::from_uint32_hilo(std::uint32_t value) noexcept
{
    return {
        static_cast<std::uint16_t>(value >> 16),
        static_cast<std::uint16_t>(value & 0xFFFF)};
}

std::array<std::uint16_t, 2> RegisterConverter::from_uint32_lohi(std::uint32_t value) noexcept
{
    auto result = from_uint32_hilo(value);
    return {result[1], result[0]};
}

std::array<std::uint16_t, 2> RegisterConverter::from_float_hilo(float value) noexcept
{
    std::uint32_t uval = std::bit_cast<std::uint32_t>(value);
    return from_uint32_hilo(uval);
}

std::array<std::uint16_t, 2> RegisterConverter::from_float_lohi(float value) noexcept
{
    auto result = from_float_hilo(value);
    return {result[1], result[0]};
}

std::array<std::uint16_t, 4> RegisterConverter::from_double_hilo(double value) noexcept
{
    std::uint64_t uval = std::bit_cast<std::uint64_t>(value);
    return {
        static_cast<std::uint16_t>(uval >> 48),
        static_cast<std::uint16_t>((uval >> 32) & 0xFFFF),
        static_cast<std::uint16_t>((uval >> 16) & 0xFFFF),
        static_cast<std::uint16_t>(uval & 0xFFFF)};
}

// =============================================================================
// BitOps Implementation
// =============================================================================

bool BitOps::get_bit(std::uint16_t value, std::uint8_t bit_pos) noexcept
{
    return (value & (1u << bit_pos)) != 0;
}

std::uint16_t BitOps::set_bit(std::uint16_t value, std::uint8_t bit_pos, bool state) noexcept
{
    if (state)
    {
        return value | (1u << bit_pos);
    }
    else
    {
        return value & ~(1u << bit_pos);
    }
}

std::vector<bool> BitOps::to_bits(std::span<const std::uint8_t> buffer, std::size_t bit_count)
{
    std::vector<bool> bits;
    bits.reserve(bit_count);

    for (std::size_t i = 0; i < bit_count && i / 8 < buffer.size(); ++i)
    {
        std::uint8_t byte = buffer[i / 8];
        bool bit = (byte & (1u << (i % 8))) != 0;
        bits.push_back(bit);
    }

    return bits;
}

std::vector<std::uint8_t> BitOps::from_bits(std::span<const bool> bits)
{
    std::size_t buffer_size = (bits.size() + 7) / 8;
    std::vector<std::uint8_t> buffer(buffer_size, 0);

    for (std::size_t i = 0; i < bits.size(); ++i)
    {
        if (bits[i])
        {
            buffer[i / 8] |= (1u << (i % 8));
        }
    }

    return buffer;
}

std::size_t BitOps::calc_buffer_size(std::size_t bit_count) noexcept
{
    return (bit_count + 7) / 8;
}

// =============================================================================
// CRC16 Implementation
// =============================================================================

std::uint16_t CRC16::calculate(std::span<const std::uint8_t> data) noexcept
{
    std::uint16_t crc = 0xFFFF;

    for (std::uint8_t byte : data)
    {
        crc ^= byte;
        for (int i = 0; i < 8; ++i)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

bool CRC16::verify(std::span<const std::uint8_t> data, std::uint16_t expected_crc) noexcept
{
    return calculate(data) == expected_crc;
}

void CRC16::append(std::vector<std::uint8_t>& data)
{
    std::uint16_t crc = calculate(data);
    data.push_back(static_cast<std::uint8_t>(crc & 0xFF));
    data.push_back(static_cast<std::uint8_t>(crc >> 8));
}

bool CRC16::verify_and_remove(std::vector<std::uint8_t>& data)
{
    if (data.size() < 2)
        return false;

    std::uint16_t received_crc = data[data.size() - 2] |
        (static_cast<std::uint16_t>(data[data.size() - 1]) << 8);

    data.resize(data.size() - 2);
    std::uint16_t calculated_crc = calculate(data);

    return calculated_crc == received_crc;
}

// =============================================================================
// Hex Implementation
// =============================================================================

std::string Hex::encode(std::span<const std::uint8_t> data, bool uppercase)
{
    const char* hex_chars = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string result;
    result.reserve(data.size() * 2);

    for (std::uint8_t byte : data)
    {
        result.push_back(hex_chars[byte >> 4]);
        result.push_back(hex_chars[byte & 0x0F]);
    }

    return result;
}

std::vector<std::uint8_t> Hex::decode(std::string_view hex_str)
{
    std::vector<std::uint8_t> result;
    result.reserve(hex_str.size() / 2);

    for (std::size_t i = 0; i + 1 < hex_str.size(); i += 2)
    {
        std::uint8_t high = char_to_value(hex_str[i]);
        std::uint8_t low = char_to_value(hex_str[i + 1]);
        result.push_back((high << 4) | low);
    }

    return result;
}

std::uint8_t Hex::char_to_value(char c) noexcept
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return 0;
}

} // namespace utils::conv
