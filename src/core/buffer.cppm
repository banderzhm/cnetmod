module;

#include <cnetmod/config.hpp>
#include <cstring>

export module cnetmod.core.buffer;

import std;

namespace cnetmod {

// =============================================================================
// Basic buffer views
// =============================================================================

/// Read-only buffer view (does not own data)
export struct const_buffer {
    const void* data = nullptr;
    std::size_t size = 0;

    constexpr const_buffer() noexcept = default;
    constexpr const_buffer(const void* p, std::size_t n) noexcept
        : data(p), size(n) {}

    /// Construct from span
    constexpr const_buffer(std::span<const std::byte> s) noexcept
        : data(s.data()), size(s.size()) {}
};

/// Writable buffer view (does not own data)
export struct mutable_buffer {
    void* data = nullptr;
    std::size_t size = 0;

    constexpr mutable_buffer() noexcept = default;
    constexpr mutable_buffer(void* p, std::size_t n) noexcept
        : data(p), size(n) {}

    /// Construct from span
    constexpr mutable_buffer(std::span<std::byte> s) noexcept
        : data(s.data()), size(s.size()) {}

    /// Implicit conversion to const_buffer
    constexpr operator const_buffer() const noexcept {
        return {data, size};
    }
};

// =============================================================================
// Factory functions
// =============================================================================

/// Create const_buffer from raw pointer and size
export constexpr auto buffer(const void* data, std::size_t size) noexcept
    -> const_buffer
{
    return {data, size};
}

/// Create mutable_buffer from raw pointer and size
export constexpr auto buffer(void* data, std::size_t size) noexcept
    -> mutable_buffer
{
    return {data, size};
}

/// Create const_buffer from string_view
export constexpr auto buffer(std::string_view sv) noexcept
    -> const_buffer
{
    return {sv.data(), sv.size()};
}

/// Create mutable_buffer from vector<byte>
export inline auto buffer(std::vector<std::byte>& v) noexcept
    -> mutable_buffer
{
    return {v.data(), v.size()};
}

/// Create const_buffer from vector<byte>
export inline auto buffer(const std::vector<std::byte>& v) noexcept
    -> const_buffer
{
    return {v.data(), v.size()};
}

/// Create mutable_buffer from array<byte, N>
export template <std::size_t N>
constexpr auto buffer(std::array<std::byte, N>& a) noexcept
    -> mutable_buffer
{
    return {a.data(), N};
}

// =============================================================================
// Dynamic buffer
// =============================================================================

/// Growable dynamic buffer for receiving variable-length data
export class dynamic_buffer {
public:
    explicit dynamic_buffer(std::size_t initial_capacity = 4096)
        : data_(initial_capacity) {}

    /// Get writable region
    [[nodiscard]] auto prepare(std::size_t n) -> mutable_buffer {
        if (write_pos_ + n > data_.size()) {
            data_.resize(write_pos_ + n);
        }
        return {data_.data() + write_pos_, n};
    }

    /// Confirm n bytes written
    void commit(std::size_t n) noexcept {
        write_pos_ += n;
    }

    /// Get readable data
    [[nodiscard]] auto data() const noexcept -> const_buffer {
        return {data_.data() + read_pos_, write_pos_ - read_pos_};
    }

    /// Consume n bytes read
    void consume(std::size_t n) noexcept {
        read_pos_ += n;
        if (read_pos_ == write_pos_) {
            read_pos_ = 0;
            write_pos_ = 0;
        }
    }

    /// Number of readable bytes
    [[nodiscard]] auto readable_bytes() const noexcept -> std::size_t {
        return write_pos_ - read_pos_;
    }

private:
    std::vector<std::byte> data_;
    std::size_t read_pos_ = 0;
    std::size_t write_pos_ = 0;
};

// =============================================================================
// Byte order conversion (Endianness)
// =============================================================================

/// Byte order enum
export enum class byte_order {
    little_endian,
    big_endian,
    native =
#if defined(_MSC_VER) || defined(__LITTLE_ENDIAN__) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
        little_endian
#else
        big_endian
#endif
};

namespace detail {

constexpr auto bswap16(std::uint16_t v) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>((v >> 8) | (v << 8));
}

constexpr auto bswap32(std::uint32_t v) noexcept -> std::uint32_t {
    return ((v >> 24) & 0x000000FF)
         | ((v >>  8) & 0x0000FF00)
         | ((v <<  8) & 0x00FF0000)
         | ((v << 24) & 0xFF000000);
}

constexpr auto bswap64(std::uint64_t v) noexcept -> std::uint64_t {
    return ((v >> 56) & 0x00000000000000FF)
         | ((v >> 40) & 0x000000000000FF00)
         | ((v >> 24) & 0x0000000000FF0000)
         | ((v >>  8) & 0x00000000FF000000)
         | ((v <<  8) & 0x000000FF00000000)
         | ((v << 24) & 0x0000FF0000000000)
         | ((v << 40) & 0x00FF000000000000)
         | ((v << 56) & 0xFF00000000000000);
}

} // namespace detail

// --- host <-> network (big-endian) ---

export constexpr auto hton(std::uint16_t v) noexcept -> std::uint16_t {
    if constexpr (byte_order::native == byte_order::big_endian) return v;
    else return detail::bswap16(v);
}

export constexpr auto hton(std::uint32_t v) noexcept -> std::uint32_t {
    if constexpr (byte_order::native == byte_order::big_endian) return v;
    else return detail::bswap32(v);
}

export constexpr auto hton(std::uint64_t v) noexcept -> std::uint64_t {
    if constexpr (byte_order::native == byte_order::big_endian) return v;
    else return detail::bswap64(v);
}

export constexpr auto ntoh(std::uint16_t v) noexcept -> std::uint16_t { return hton(v); }
export constexpr auto ntoh(std::uint32_t v) noexcept -> std::uint32_t { return hton(v); }
export constexpr auto ntoh(std::uint64_t v) noexcept -> std::uint64_t { return hton(v); }

// --- host <-> little-endian ---

export constexpr auto htole(std::uint16_t v) noexcept -> std::uint16_t {
    if constexpr (byte_order::native == byte_order::little_endian) return v;
    else return detail::bswap16(v);
}

export constexpr auto htole(std::uint32_t v) noexcept -> std::uint32_t {
    if constexpr (byte_order::native == byte_order::little_endian) return v;
    else return detail::bswap32(v);
}

export constexpr auto htole(std::uint64_t v) noexcept -> std::uint64_t {
    if constexpr (byte_order::native == byte_order::little_endian) return v;
    else return detail::bswap64(v);
}

export constexpr auto letoh(std::uint16_t v) noexcept -> std::uint16_t { return htole(v); }
export constexpr auto letoh(std::uint32_t v) noexcept -> std::uint32_t { return htole(v); }
export constexpr auto letoh(std::uint64_t v) noexcept -> std::uint64_t { return htole(v); }

// --- Generic byte_swap ---

export constexpr auto byte_swap(std::uint16_t v) noexcept -> std::uint16_t { return detail::bswap16(v); }
export constexpr auto byte_swap(std::uint32_t v) noexcept -> std::uint32_t { return detail::bswap32(v); }
export constexpr auto byte_swap(std::uint64_t v) noexcept -> std::uint64_t { return detail::bswap64(v); }

// =============================================================================
// buffer_reader — Read integers from buffer in specified byte order
// =============================================================================

export class buffer_reader {
public:
    explicit buffer_reader(const_buffer buf) noexcept
        : data_(static_cast<const std::byte*>(buf.data))
        , size_(buf.size) {}

    explicit buffer_reader(std::span<const std::byte> s) noexcept
        : data_(s.data()), size_(s.size()) {}

    /// Remaining readable bytes
    [[nodiscard]] auto remaining() const noexcept -> std::size_t {
        return size_ - pos_;
    }

    /// Current offset
    [[nodiscard]] auto position() const noexcept -> std::size_t {
        return pos_;
    }

    /// Skip n bytes
    auto skip(std::size_t n) noexcept -> bool {
        if (remaining() < n) return false;
        pos_ += n;
        return true;
    }

    /// Read raw bytes
    auto read_bytes(void* dst, std::size_t n) noexcept -> bool {
        if (remaining() < n) return false;
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
        return true;
    }

    // --- Big-endian (network byte order) ---

    auto read_u8() noexcept -> std::optional<std::uint8_t> {
        if (remaining() < 1) return std::nullopt;
        auto v = static_cast<std::uint8_t>(data_[pos_++]);
        return v;
    }

    auto read_u16_be() noexcept -> std::optional<std::uint16_t> {
        if (remaining() < 2) return std::nullopt;
        std::uint16_t v;
        std::memcpy(&v, data_ + pos_, 2);
        pos_ += 2;
        return ntoh(v);
    }

    auto read_u32_be() noexcept -> std::optional<std::uint32_t> {
        if (remaining() < 4) return std::nullopt;
        std::uint32_t v;
        std::memcpy(&v, data_ + pos_, 4);
        pos_ += 4;
        return ntoh(v);
    }

    auto read_u64_be() noexcept -> std::optional<std::uint64_t> {
        if (remaining() < 8) return std::nullopt;
        std::uint64_t v;
        std::memcpy(&v, data_ + pos_, 8);
        pos_ += 8;
        return ntoh(v);
    }

    // --- Little-endian ---

    auto read_u16_le() noexcept -> std::optional<std::uint16_t> {
        if (remaining() < 2) return std::nullopt;
        std::uint16_t v;
        std::memcpy(&v, data_ + pos_, 2);
        pos_ += 2;
        return letoh(v);
    }

    auto read_u32_le() noexcept -> std::optional<std::uint32_t> {
        if (remaining() < 4) return std::nullopt;
        std::uint32_t v;
        std::memcpy(&v, data_ + pos_, 4);
        pos_ += 4;
        return letoh(v);
    }

    auto read_u64_le() noexcept -> std::optional<std::uint64_t> {
        if (remaining() < 8) return std::nullopt;
        std::uint64_t v;
        std::memcpy(&v, data_ + pos_, 8);
        pos_ += 8;
        return letoh(v);
    }

private:
    const std::byte* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

// =============================================================================
// buffer_writer — Write integers to buffer in specified byte order
// =============================================================================

export class buffer_writer {
public:
    explicit buffer_writer(mutable_buffer buf) noexcept
        : data_(static_cast<std::byte*>(buf.data))
        , capacity_(buf.size) {}

    explicit buffer_writer(std::span<std::byte> s) noexcept
        : data_(s.data()), capacity_(s.size()) {}

    /// Remaining writable bytes
    [[nodiscard]] auto remaining() const noexcept -> std::size_t {
        return capacity_ - pos_;
    }

    /// Number of bytes written
    [[nodiscard]] auto written() const noexcept -> std::size_t {
        return pos_;
    }

    /// Write raw bytes
    auto write_bytes(const void* src, std::size_t n) noexcept -> bool {
        if (remaining() < n) return false;
        std::memcpy(data_ + pos_, src, n);
        pos_ += n;
        return true;
    }

    // --- Big-endian (network byte order) ---

    auto write_u8(std::uint8_t v) noexcept -> bool {
        if (remaining() < 1) return false;
        data_[pos_++] = static_cast<std::byte>(v);
        return true;
    }

    auto write_u16_be(std::uint16_t v) noexcept -> bool {
        if (remaining() < 2) return false;
        auto net = hton(v);
        std::memcpy(data_ + pos_, &net, 2);
        pos_ += 2;
        return true;
    }

    auto write_u32_be(std::uint32_t v) noexcept -> bool {
        if (remaining() < 4) return false;
        auto net = hton(v);
        std::memcpy(data_ + pos_, &net, 4);
        pos_ += 4;
        return true;
    }

    auto write_u64_be(std::uint64_t v) noexcept -> bool {
        if (remaining() < 8) return false;
        auto net = hton(v);
        std::memcpy(data_ + pos_, &net, 8);
        pos_ += 8;
        return true;
    }

    // --- Little-endian ---

    auto write_u16_le(std::uint16_t v) noexcept -> bool {
        if (remaining() < 2) return false;
        auto le = htole(v);
        std::memcpy(data_ + pos_, &le, 2);
        pos_ += 2;
        return true;
    }

    auto write_u32_le(std::uint32_t v) noexcept -> bool {
        if (remaining() < 4) return false;
        auto le = htole(v);
        std::memcpy(data_ + pos_, &le, 4);
        pos_ += 4;
        return true;
    }

    auto write_u64_le(std::uint64_t v) noexcept -> bool {
        if (remaining() < 8) return false;
        auto le = htole(v);
        std::memcpy(data_ + pos_, &le, 8);
        pos_ += 8;
        return true;
    }

private:
    std::byte* data_;
    std::size_t capacity_;
    std::size_t pos_ = 0;
};

} // namespace cnetmod
