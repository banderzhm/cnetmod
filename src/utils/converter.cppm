export module cnetmod.utils.converter;

import std;

export namespace utils::conv {

/// Zero-copy character/byte representation views. Ownership remains with the
/// caller; protocol code should use these instead of representation casts.
std::span<const std::byte> to_bytes(std::string_view value) noexcept;
std::string_view to_string_view(std::span<const std::byte> value) noexcept;

// =============================================================================
// Endian Type
// =============================================================================

/// Byte order enum
enum class endian
{
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
std::uint16_t bswap16(std::uint16_t v) noexcept;
std::uint32_t bswap32(std::uint32_t v) noexcept;
std::uint64_t bswap64(std::uint64_t v) noexcept;

/// Host to network (big-endian)
std::uint16_t hton(std::uint16_t v) noexcept;
std::uint32_t hton(std::uint32_t v) noexcept;
std::uint64_t hton(std::uint64_t v) noexcept;

/// Network to host (big-endian)
std::uint16_t ntoh(std::uint16_t v) noexcept;
std::uint32_t ntoh(std::uint32_t v) noexcept;
std::uint64_t ntoh(std::uint64_t v) noexcept;

/// Host to little-endian
std::uint16_t htole(std::uint16_t v) noexcept;
std::uint32_t htole(std::uint32_t v) noexcept;
std::uint64_t htole(std::uint64_t v) noexcept;

/// Little-endian to host
std::uint16_t letoh(std::uint16_t v) noexcept;
std::uint32_t letoh(std::uint32_t v) noexcept;
std::uint64_t letoh(std::uint64_t v) noexcept;

// =============================================================================
// Buffer Read/Write Operations
// =============================================================================

/// Read 16-bit big-endian value from buffer
std::uint16_t read_be16(std::span<const std::uint8_t> data, std::size_t offset = 0);

/// Read 32-bit big-endian value from buffer
std::uint32_t read_be32(std::span<const std::uint8_t> data, std::size_t offset = 0);

/// Read 64-bit big-endian value from buffer
std::uint64_t read_be64(std::span<const std::uint8_t> data, std::size_t offset = 0);

/// Write 16-bit big-endian value to buffer
void write_be16(std::span<std::uint8_t> data, std::uint16_t value, std::size_t offset = 0);

/// Write 32-bit big-endian value to buffer
void write_be32(std::span<std::uint8_t> data, std::uint32_t value, std::size_t offset = 0);

/// Write 64-bit big-endian value to buffer
void write_be64(std::span<std::uint8_t> data, std::uint64_t value, std::size_t offset = 0);

/// Read 16-bit little-endian value from buffer
std::uint16_t read_le16(std::span<const std::uint8_t> data, std::size_t offset = 0);

/// Read 32-bit little-endian value from buffer
std::uint32_t read_le32(std::span<const std::uint8_t> data, std::size_t offset = 0);

/// Read 64-bit little-endian value from buffer
std::uint64_t read_le64(std::span<const std::uint8_t> data, std::size_t offset = 0);

/// Write 16-bit little-endian value to buffer
void write_le16(std::span<std::uint8_t> data, std::uint16_t value, std::size_t offset = 0);

/// Write 32-bit little-endian value to buffer
void write_le32(std::span<std::uint8_t> data, std::uint32_t value, std::size_t offset = 0);

/// Write 64-bit little-endian value to buffer
void write_le64(std::span<std::uint8_t> data, std::uint64_t value, std::size_t offset = 0);

// =============================================================================
// Register Data Conversion (for Modbus and similar protocols)
// =============================================================================
class RegisterConverter
{
public:
    // Convert single register (16-bit) to signed integer
    static std::int16_t to_int16(std::uint16_t reg) noexcept;

    // Convert single register to unsigned integer
    static std::uint16_t to_uint16(std::uint16_t reg) noexcept;

    // Convert two registers (32-bit) to signed integer - high word first
    static std::int32_t to_int32_hilo(std::uint16_t high, std::uint16_t low) noexcept;

    // Convert two registers (32-bit) to signed integer - low word first
    static std::int32_t to_int32_lohi(std::uint16_t low, std::uint16_t high) noexcept;

    // Convert two registers to unsigned integer - high word first
    static std::uint32_t to_uint32_hilo(std::uint16_t high, std::uint16_t low) noexcept;

    // Convert two registers to unsigned integer - low word first
    static std::uint32_t to_uint32_lohi(std::uint16_t low, std::uint16_t high) noexcept;

    // Convert two registers to float - high word first
    static float to_float_hilo(std::uint16_t high, std::uint16_t low) noexcept;

    // Convert two registers to float - low word first
    static float to_float_lohi(std::uint16_t low, std::uint16_t high) noexcept;

    // Convert four registers to double - high word first
    static double to_double_hilo(std::uint16_t h1, std::uint16_t h2, std::uint16_t l1, std::uint16_t l2) noexcept;

    // Convert signed integer to single register
    static std::uint16_t from_int16(std::int16_t value) noexcept;

    // Convert 32-bit signed integer to two registers - high word first
    static std::array<std::uint16_t, 2> from_int32_hilo(std::int32_t value) noexcept;

    // Convert 32-bit signed integer to two registers - low word first
    static std::array<std::uint16_t, 2> from_int32_lohi(std::int32_t value) noexcept;

    // Convert 32-bit unsigned integer to two registers - high word first
    static std::array<std::uint16_t, 2> from_uint32_hilo(std::uint32_t value) noexcept;

    // Convert 32-bit unsigned integer to two registers - low word first
    static std::array<std::uint16_t, 2> from_uint32_lohi(std::uint32_t value) noexcept;

    // Convert float to two registers - high word first
    static std::array<std::uint16_t, 2> from_float_hilo(float value) noexcept;

    // Convert float to two registers - low word first
    static std::array<std::uint16_t, 2> from_float_lohi(float value) noexcept;

    // Convert double to four registers - high word first
    static std::array<std::uint16_t, 4> from_double_hilo(double value) noexcept;
};

// Bit manipulation utilities for coils and discrete inputs
class BitOps
{
public:
    // Get bit at specified position
    static bool get_bit(std::uint16_t value, std::uint8_t bit_pos) noexcept;

    // Set bit at specified position
    static std::uint16_t set_bit(std::uint16_t value, std::uint8_t bit_pos, bool state) noexcept;

    // Extract bit array from buffer
    static std::vector<bool> to_bits(std::span<const std::uint8_t> buffer, std::size_t bit_count);

    // Convert bit array to buffer
    static std::vector<std::uint8_t> from_bits(std::span<const bool> bits);

    // Calculate required buffer size for given bit count
    static std::size_t calc_buffer_size(std::size_t bit_count) noexcept;
};

// CRC16 checksum calculation for Modbus RTU
class CRC16
{
public:
    // Calculate Modbus CRC16
    static std::uint16_t calculate(std::span<const std::uint8_t> data) noexcept;

    // Verify CRC
    static bool verify(std::span<const std::uint8_t> data, std::uint16_t expected_crc) noexcept;

    // Append CRC to data (low byte first)
    static void append(std::vector<std::uint8_t>& data);

    // Extract and verify CRC from end of data
    static bool verify_and_remove(std::vector<std::uint8_t>& data);
};

// Hexadecimal conversion utilities
class Hex
{
public:
    // Convert buffer to hexadecimal string
    static std::string encode(std::span<const std::uint8_t> data, bool uppercase = true);

    // Convert hexadecimal string to buffer
    static std::vector<std::uint8_t> decode(std::string_view hex_str);

private:
    static std::uint8_t char_to_value(char c) noexcept;
};

} // namespace utils::conv
