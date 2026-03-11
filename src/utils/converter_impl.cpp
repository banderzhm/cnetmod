module;
#include <cstdint>
#include <array>
#include <span>
#include <bit>
#include <vector>
#include <string>
#include <string_view>
#include <cstring>

module cnetmod.utils;

namespace utils::conv {

// =============================================================================
// Byte Swap Implementation
// =============================================================================

uint16_t bswap16(uint16_t v) noexcept {
    return static_cast<uint16_t>((v >> 8) | (v << 8));
}

uint32_t bswap32(uint32_t v) noexcept {
    return ((v >> 24) & 0x000000FF)
         | ((v >>  8) & 0x0000FF00)
         | ((v <<  8) & 0x00FF0000)
         | ((v << 24) & 0xFF000000);
}

uint64_t bswap64(uint64_t v) noexcept {
    return ((v >> 56) & 0x00000000000000FF)
         | ((v >> 40) & 0x000000000000FF00)
         | ((v >> 24) & 0x0000000000FF0000)
         | ((v >>  8) & 0x00000000FF000000)
         | ((v <<  8) & 0x000000FF00000000)
         | ((v << 24) & 0x0000FF0000000000)
         | ((v << 40) & 0x00FF000000000000)
         | ((v << 56) & 0xFF00000000000000);
}

// =============================================================================
// Host <-> Network (Big-Endian)
// =============================================================================

uint16_t hton(uint16_t v) noexcept {
    if constexpr (endian::native == endian::big) return v;
    else return bswap16(v);
}

uint32_t hton(uint32_t v) noexcept {
    if constexpr (endian::native == endian::big) return v;
    else return bswap32(v);
}

uint64_t hton(uint64_t v) noexcept {
    if constexpr (endian::native == endian::big) return v;
    else return bswap64(v);
}

uint16_t ntoh(uint16_t v) noexcept { return hton(v); }
uint32_t ntoh(uint32_t v) noexcept { return hton(v); }
uint64_t ntoh(uint64_t v) noexcept { return hton(v); }

// =============================================================================
// Host <-> Little-Endian
// =============================================================================

uint16_t htole(uint16_t v) noexcept {
    if constexpr (endian::native == endian::little) return v;
    else return bswap16(v);
}

uint32_t htole(uint32_t v) noexcept {
    if constexpr (endian::native == endian::little) return v;
    else return bswap32(v);
}

uint64_t htole(uint64_t v) noexcept {
    if constexpr (endian::native == endian::little) return v;
    else return bswap64(v);
}

uint16_t letoh(uint16_t v) noexcept { return htole(v); }
uint32_t letoh(uint32_t v) noexcept { return htole(v); }
uint64_t letoh(uint64_t v) noexcept { return htole(v); }

// =============================================================================
// Buffer Read Operations - Big-Endian
// =============================================================================

uint16_t read_be16(std::span<const uint8_t> data, size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return (static_cast<uint16_t>(data[offset]) << 8) |
           static_cast<uint16_t>(data[offset + 1]);
}

uint32_t read_be32(std::span<const uint8_t> data, size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return (static_cast<uint32_t>(data[offset]) << 24) |
           (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) |
           static_cast<uint32_t>(data[offset + 3]);
}

uint64_t read_be64(std::span<const uint8_t> data, size_t offset) {
    if (offset + 8 > data.size()) return 0;
    return (static_cast<uint64_t>(data[offset]) << 56) |
           (static_cast<uint64_t>(data[offset + 1]) << 48) |
           (static_cast<uint64_t>(data[offset + 2]) << 40) |
           (static_cast<uint64_t>(data[offset + 3]) << 32) |
           (static_cast<uint64_t>(data[offset + 4]) << 24) |
           (static_cast<uint64_t>(data[offset + 5]) << 16) |
           (static_cast<uint64_t>(data[offset + 6]) << 8) |
           static_cast<uint64_t>(data[offset + 7]);
}

// =============================================================================
// Buffer Write Operations - Big-Endian
// =============================================================================

void write_be16(std::span<uint8_t> data, uint16_t value, size_t offset) {
    if (offset + 2 > data.size()) return;
    data[offset] = static_cast<uint8_t>(value >> 8);
    data[offset + 1] = static_cast<uint8_t>(value & 0xFF);
}

void write_be32(std::span<uint8_t> data, uint32_t value, size_t offset) {
    if (offset + 4 > data.size()) return;
    data[offset] = static_cast<uint8_t>(value >> 24);
    data[offset + 1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[offset + 2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[offset + 3] = static_cast<uint8_t>(value & 0xFF);
}

void write_be64(std::span<uint8_t> data, uint64_t value, size_t offset) {
    if (offset + 8 > data.size()) return;
    data[offset] = static_cast<uint8_t>(value >> 56);
    data[offset + 1] = static_cast<uint8_t>((value >> 48) & 0xFF);
    data[offset + 2] = static_cast<uint8_t>((value >> 40) & 0xFF);
    data[offset + 3] = static_cast<uint8_t>((value >> 32) & 0xFF);
    data[offset + 4] = static_cast<uint8_t>((value >> 24) & 0xFF);
    data[offset + 5] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[offset + 6] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[offset + 7] = static_cast<uint8_t>(value & 0xFF);
}

// =============================================================================
// Buffer Read Operations - Little-Endian
// =============================================================================

uint16_t read_le16(std::span<const uint8_t> data, size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return static_cast<uint16_t>(data[offset]) |
           (static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint32_t read_le32(std::span<const uint8_t> data, size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

uint64_t read_le64(std::span<const uint8_t> data, size_t offset) {
    if (offset + 8 > data.size()) return 0;
    return static_cast<uint64_t>(data[offset]) |
           (static_cast<uint64_t>(data[offset + 1]) << 8) |
           (static_cast<uint64_t>(data[offset + 2]) << 16) |
           (static_cast<uint64_t>(data[offset + 3]) << 24) |
           (static_cast<uint64_t>(data[offset + 4]) << 32) |
           (static_cast<uint64_t>(data[offset + 5]) << 40) |
           (static_cast<uint64_t>(data[offset + 6]) << 48) |
           (static_cast<uint64_t>(data[offset + 7]) << 56);
}

// =============================================================================
// Buffer Write Operations - Little-Endian
// =============================================================================

void write_le16(std::span<uint8_t> data, uint16_t value, size_t offset) {
    if (offset + 2 > data.size()) return;
    data[offset] = static_cast<uint8_t>(value & 0xFF);
    data[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void write_le32(std::span<uint8_t> data, uint32_t value, size_t offset) {
    if (offset + 4 > data.size()) return;
    data[offset] = static_cast<uint8_t>(value & 0xFF);
    data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[offset + 3] = static_cast<uint8_t>(value >> 24);
}

void write_le64(std::span<uint8_t> data, uint64_t value, size_t offset) {
    if (offset + 8 > data.size()) return;
    data[offset] = static_cast<uint8_t>(value & 0xFF);
    data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    data[offset + 4] = static_cast<uint8_t>((value >> 32) & 0xFF);
    data[offset + 5] = static_cast<uint8_t>((value >> 40) & 0xFF);
    data[offset + 6] = static_cast<uint8_t>((value >> 48) & 0xFF);
    data[offset + 7] = static_cast<uint8_t>(value >> 56);
}

// =============================================================================
// RegisterConverter Implementation
// =============================================================================

int16_t RegisterConverter::to_int16(uint16_t reg) noexcept {
    return static_cast<int16_t>(reg);
}

uint16_t RegisterConverter::to_uint16(uint16_t reg) noexcept {
    return reg;
}

int32_t RegisterConverter::to_int32_hilo(uint16_t high, uint16_t low) noexcept {
    uint32_t value = (static_cast<uint32_t>(high) << 16) | low;
    return static_cast<int32_t>(value);
}

int32_t RegisterConverter::to_int32_lohi(uint16_t low, uint16_t high) noexcept {
    return to_int32_hilo(high, low);
}

uint32_t RegisterConverter::to_uint32_hilo(uint16_t high, uint16_t low) noexcept {
    return (static_cast<uint32_t>(high) << 16) | low;
}

uint32_t RegisterConverter::to_uint32_lohi(uint16_t low, uint16_t high) noexcept {
    return to_uint32_hilo(high, low);
}

float RegisterConverter::to_float_hilo(uint16_t high, uint16_t low) noexcept {
    uint32_t value = to_uint32_hilo(high, low);
    return std::bit_cast<float>(value);
}

float RegisterConverter::to_float_lohi(uint16_t low, uint16_t high) noexcept {
    return to_float_hilo(high, low);
}

double RegisterConverter::to_double_hilo(uint16_t h1, uint16_t h2, uint16_t l1, uint16_t l2) noexcept {
    uint64_t value = (static_cast<uint64_t>(h1) << 48) |
                    (static_cast<uint64_t>(h2) << 32) |
                    (static_cast<uint64_t>(l1) << 16) |
                    static_cast<uint64_t>(l2);
    return std::bit_cast<double>(value);
}

uint16_t RegisterConverter::from_int16(int16_t value) noexcept {
    return static_cast<uint16_t>(value);
}

std::array<uint16_t, 2> RegisterConverter::from_int32_hilo(int32_t value) noexcept {
    uint32_t uval = static_cast<uint32_t>(value);
    return {
        static_cast<uint16_t>(uval >> 16),
        static_cast<uint16_t>(uval & 0xFFFF)
    };
}

std::array<uint16_t, 2> RegisterConverter::from_int32_lohi(int32_t value) noexcept {
    auto result = from_int32_hilo(value);
    return {result[1], result[0]};
}

std::array<uint16_t, 2> RegisterConverter::from_uint32_hilo(uint32_t value) noexcept {
    return {
        static_cast<uint16_t>(value >> 16),
        static_cast<uint16_t>(value & 0xFFFF)
    };
}

std::array<uint16_t, 2> RegisterConverter::from_uint32_lohi(uint32_t value) noexcept {
    auto result = from_uint32_hilo(value);
    return {result[1], result[0]};
}

std::array<uint16_t, 2> RegisterConverter::from_float_hilo(float value) noexcept {
    uint32_t uval = std::bit_cast<uint32_t>(value);
    return from_uint32_hilo(uval);
}

std::array<uint16_t, 2> RegisterConverter::from_float_lohi(float value) noexcept {
    auto result = from_float_hilo(value);
    return {result[1], result[0]};
}

std::array<uint16_t, 4> RegisterConverter::from_double_hilo(double value) noexcept {
    uint64_t uval = std::bit_cast<uint64_t>(value);
    return {
        static_cast<uint16_t>(uval >> 48),
        static_cast<uint16_t>((uval >> 32) & 0xFFFF),
        static_cast<uint16_t>((uval >> 16) & 0xFFFF),
        static_cast<uint16_t>(uval & 0xFFFF)
    };
}

// =============================================================================
// BitOps Implementation
// =============================================================================

bool BitOps::get_bit(uint16_t value, uint8_t bit_pos) noexcept {
    return (value & (1u << bit_pos)) != 0;
}

uint16_t BitOps::set_bit(uint16_t value, uint8_t bit_pos, bool state) noexcept {
    if (state) {
        return value | (1u << bit_pos);
    } else {
        return value & ~(1u << bit_pos);
    }
}

std::vector<bool> BitOps::to_bits(std::span<const uint8_t> buffer, size_t bit_count) {
    std::vector<bool> bits;
    bits.reserve(bit_count);
    
    for (size_t i = 0; i < bit_count && i / 8 < buffer.size(); ++i) {
        uint8_t byte = buffer[i / 8];
        bool bit = (byte & (1u << (i % 8))) != 0;
        bits.push_back(bit);
    }
    
    return bits;
}

std::vector<uint8_t> BitOps::from_bits(std::span<const bool> bits) {
    size_t buffer_size = (bits.size() + 7) / 8;
    std::vector<uint8_t> buffer(buffer_size, 0);
    
    for (size_t i = 0; i < bits.size(); ++i) {
        if (bits[i]) {
            buffer[i / 8] |= (1u << (i % 8));
        }
    }
    
    return buffer;
}

size_t BitOps::calc_buffer_size(size_t bit_count) noexcept {
    return (bit_count + 7) / 8;
}

// =============================================================================
// CRC16 Implementation
// =============================================================================

uint16_t CRC16::calculate(std::span<const uint8_t> data) noexcept {
    uint16_t crc = 0xFFFF;
    
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

bool CRC16::verify(std::span<const uint8_t> data, uint16_t expected_crc) noexcept {
    return calculate(data) == expected_crc;
}

void CRC16::append(std::vector<uint8_t>& data) {
    uint16_t crc = calculate(data);
    data.push_back(static_cast<uint8_t>(crc & 0xFF));
    data.push_back(static_cast<uint8_t>(crc >> 8));
}

bool CRC16::verify_and_remove(std::vector<uint8_t>& data) {
    if (data.size() < 2) return false;
    
    uint16_t received_crc = data[data.size() - 2] | 
                           (static_cast<uint16_t>(data[data.size() - 1]) << 8);
    
    data.resize(data.size() - 2);
    uint16_t calculated_crc = calculate(data);
    
    return calculated_crc == received_crc;
}

// =============================================================================
// Hex Implementation
// =============================================================================

std::string Hex::encode(std::span<const uint8_t> data, bool uppercase) {
    const char* hex_chars = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string result;
    result.reserve(data.size() * 2);
    
    for (uint8_t byte : data) {
        result.push_back(hex_chars[byte >> 4]);
        result.push_back(hex_chars[byte & 0x0F]);
    }
    
    return result;
}

std::vector<uint8_t> Hex::decode(std::string_view hex_str) {
    std::vector<uint8_t> result;
    result.reserve(hex_str.size() / 2);
    
    for (size_t i = 0; i + 1 < hex_str.size(); i += 2) {
        uint8_t high = char_to_value(hex_str[i]);
        uint8_t low = char_to_value(hex_str[i + 1]);
        result.push_back((high << 4) | low);
    }
    
    return result;
}

uint8_t Hex::char_to_value(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

} // namespace utils::conv
