module;
#include <cstdint>
#include <array>
#include <span>
#include <bit>
#include <concepts>
#include <vector>
#include <string>
#include <string_view>

export module cnetmod.utils:converter;

export namespace utils::conv {

// =============================================================================
// Endian Type
// =============================================================================

/// Byte order enum
enum class endian {
    little,
    big,
    native =
#if defined(_MSC_VER) || defined(__LITTLE_ENDIAN__) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
        little
#else
        big
#endif
};

// =============================================================================
// Endian Conversion Functions
// =============================================================================

/// Byte swap operations
uint16_t bswap16(uint16_t v) noexcept;
uint32_t bswap32(uint32_t v) noexcept;
uint64_t bswap64(uint64_t v) noexcept;

/// Host to network (big-endian)
uint16_t hton(uint16_t v) noexcept;
uint32_t hton(uint32_t v) noexcept;
uint64_t hton(uint64_t v) noexcept;

/// Network to host (big-endian)
uint16_t ntoh(uint16_t v) noexcept;
uint32_t ntoh(uint32_t v) noexcept;
uint64_t ntoh(uint64_t v) noexcept;

/// Host to little-endian
uint16_t htole(uint16_t v) noexcept;
uint32_t htole(uint32_t v) noexcept;
uint64_t htole(uint64_t v) noexcept;

/// Little-endian to host
uint16_t letoh(uint16_t v) noexcept;
uint32_t letoh(uint32_t v) noexcept;
uint64_t letoh(uint64_t v) noexcept;

// =============================================================================
// Buffer Read/Write Operations
// =============================================================================

/// Read 16-bit big-endian value from buffer
uint16_t read_be16(std::span<const uint8_t> data, size_t offset = 0);

/// Read 32-bit big-endian value from buffer
uint32_t read_be32(std::span<const uint8_t> data, size_t offset = 0);

/// Read 64-bit big-endian value from buffer
uint64_t read_be64(std::span<const uint8_t> data, size_t offset = 0);

/// Write 16-bit big-endian value to buffer
void write_be16(std::span<uint8_t> data, uint16_t value, size_t offset = 0);

/// Write 32-bit big-endian value to buffer
void write_be32(std::span<uint8_t> data, uint32_t value, size_t offset = 0);

/// Write 64-bit big-endian value to buffer
void write_be64(std::span<uint8_t> data, uint64_t value, size_t offset = 0);

/// Read 16-bit little-endian value from buffer
uint16_t read_le16(std::span<const uint8_t> data, size_t offset = 0);

/// Read 32-bit little-endian value from buffer
uint32_t read_le32(std::span<const uint8_t> data, size_t offset = 0);

/// Read 64-bit little-endian value from buffer
uint64_t read_le64(std::span<const uint8_t> data, size_t offset = 0);

/// Write 16-bit little-endian value to buffer
void write_le16(std::span<uint8_t> data, uint16_t value, size_t offset = 0);

/// Write 32-bit little-endian value to buffer
void write_le32(std::span<uint8_t> data, uint32_t value, size_t offset = 0);

/// Write 64-bit little-endian value to buffer
void write_le64(std::span<uint8_t> data, uint64_t value, size_t offset = 0);

// =============================================================================
// Register Data Conversion (for Modbus and similar protocols)
// =============================================================================
class RegisterConverter {
public:
    // Convert single register (16-bit) to signed integer
    static int16_t to_int16(uint16_t reg) noexcept;
    
    // Convert single register to unsigned integer
    static uint16_t to_uint16(uint16_t reg) noexcept;
    
    // Convert two registers (32-bit) to signed integer - high word first
    static int32_t to_int32_hilo(uint16_t high, uint16_t low) noexcept;
    
    // Convert two registers (32-bit) to signed integer - low word first
    static int32_t to_int32_lohi(uint16_t low, uint16_t high) noexcept;
    
    // Convert two registers to unsigned integer - high word first
    static uint32_t to_uint32_hilo(uint16_t high, uint16_t low) noexcept;
    
    // Convert two registers to unsigned integer - low word first
    static uint32_t to_uint32_lohi(uint16_t low, uint16_t high) noexcept;
    
    // Convert two registers to float - high word first
    static float to_float_hilo(uint16_t high, uint16_t low) noexcept;
    
    // Convert two registers to float - low word first
    static float to_float_lohi(uint16_t low, uint16_t high) noexcept;
    
    // Convert four registers to double - high word first
    static double to_double_hilo(uint16_t h1, uint16_t h2, uint16_t l1, uint16_t l2) noexcept;
    
    // Convert signed integer to single register
    static uint16_t from_int16(int16_t value) noexcept;
    
    // Convert 32-bit signed integer to two registers - high word first
    static std::array<uint16_t, 2> from_int32_hilo(int32_t value) noexcept;
    
    // Convert 32-bit signed integer to two registers - low word first
    static std::array<uint16_t, 2> from_int32_lohi(int32_t value) noexcept;
    
    // Convert 32-bit unsigned integer to two registers - high word first
    static std::array<uint16_t, 2> from_uint32_hilo(uint32_t value) noexcept;
    
    // Convert 32-bit unsigned integer to two registers - low word first
    static std::array<uint16_t, 2> from_uint32_lohi(uint32_t value) noexcept;
    
    // Convert float to two registers - high word first
    static std::array<uint16_t, 2> from_float_hilo(float value) noexcept;
    
    // Convert float to two registers - low word first
    static std::array<uint16_t, 2> from_float_lohi(float value) noexcept;
    
    // Convert double to four registers - high word first
    static std::array<uint16_t, 4> from_double_hilo(double value) noexcept;
};

// Bit manipulation utilities for coils and discrete inputs
class BitOps {
public:
    // Get bit at specified position
    static bool get_bit(uint16_t value, uint8_t bit_pos) noexcept;
    
    // Set bit at specified position
    static uint16_t set_bit(uint16_t value, uint8_t bit_pos, bool state) noexcept;
    
    // Extract bit array from buffer
    static std::vector<bool> to_bits(std::span<const uint8_t> buffer, size_t bit_count);
    
    // Convert bit array to buffer
    static std::vector<uint8_t> from_bits(std::span<const bool> bits);
    
    // Calculate required buffer size for given bit count
    static size_t calc_buffer_size(size_t bit_count) noexcept;
};

// CRC16 checksum calculation for Modbus RTU
class CRC16 {
public:
    // Calculate Modbus CRC16
    static uint16_t calculate(std::span<const uint8_t> data) noexcept;
    
    // Verify CRC
    static bool verify(std::span<const uint8_t> data, uint16_t expected_crc) noexcept;
    
    // Append CRC to data (low byte first)
    static void append(std::vector<uint8_t>& data);
    
    // Extract and verify CRC from end of data
    static bool verify_and_remove(std::vector<uint8_t>& data);
};

// Hexadecimal conversion utilities
class Hex {
public:
    // Convert buffer to hexadecimal string
    static std::string encode(std::span<const uint8_t> data, bool uppercase = true);
    
    // Convert hexadecimal string to buffer
    static std::vector<uint8_t> decode(std::string_view hex_str);

private:
    static uint8_t char_to_value(char c) noexcept;
};

} // namespace utils::conv
