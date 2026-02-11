module;

#include <cnetmod/config.hpp>
#include <cstring>

export module cnetmod.core.buffer;

import std;

namespace cnetmod {

// =============================================================================
// 基础缓冲区视图
// =============================================================================

/// 只读缓冲区视图（不拥有数据）
export struct const_buffer {
    const void* data = nullptr;
    std::size_t size = 0;

    constexpr const_buffer() noexcept = default;
    constexpr const_buffer(const void* p, std::size_t n) noexcept
        : data(p), size(n) {}

    /// 从 span 构造
    constexpr const_buffer(std::span<const std::byte> s) noexcept
        : data(s.data()), size(s.size()) {}
};

/// 可写缓冲区视图（不拥有数据）
export struct mutable_buffer {
    void* data = nullptr;
    std::size_t size = 0;

    constexpr mutable_buffer() noexcept = default;
    constexpr mutable_buffer(void* p, std::size_t n) noexcept
        : data(p), size(n) {}

    /// 从 span 构造
    constexpr mutable_buffer(std::span<std::byte> s) noexcept
        : data(s.data()), size(s.size()) {}

    /// 隐式转换为 const_buffer
    constexpr operator const_buffer() const noexcept {
        return {data, size};
    }
};

// =============================================================================
// 工厂函数
// =============================================================================

/// 从原始指针和大小创建 const_buffer
export constexpr auto buffer(const void* data, std::size_t size) noexcept
    -> const_buffer
{
    return {data, size};
}

/// 从原始指针和大小创建 mutable_buffer
export constexpr auto buffer(void* data, std::size_t size) noexcept
    -> mutable_buffer
{
    return {data, size};
}

/// 从 string_view 创建 const_buffer
export constexpr auto buffer(std::string_view sv) noexcept
    -> const_buffer
{
    return {sv.data(), sv.size()};
}

/// 从 vector<byte> 创建 mutable_buffer
export inline auto buffer(std::vector<std::byte>& v) noexcept
    -> mutable_buffer
{
    return {v.data(), v.size()};
}

/// 从 vector<byte> 创建 const_buffer
export inline auto buffer(const std::vector<std::byte>& v) noexcept
    -> const_buffer
{
    return {v.data(), v.size()};
}

/// 从 array<byte, N> 创建 mutable_buffer
export template <std::size_t N>
constexpr auto buffer(std::array<std::byte, N>& a) noexcept
    -> mutable_buffer
{
    return {a.data(), N};
}

// =============================================================================
// 动态缓冲区
// =============================================================================

/// 可增长的动态缓冲区，用于接收不定长数据
export class dynamic_buffer {
public:
    explicit dynamic_buffer(std::size_t initial_capacity = 4096)
        : data_(initial_capacity) {}

    /// 获取可写区域
    [[nodiscard]] auto prepare(std::size_t n) -> mutable_buffer {
        if (write_pos_ + n > data_.size()) {
            data_.resize(write_pos_ + n);
        }
        return {data_.data() + write_pos_, n};
    }

    /// 确认已写入 n 字节
    void commit(std::size_t n) noexcept {
        write_pos_ += n;
    }

    /// 获取可读数据
    [[nodiscard]] auto data() const noexcept -> const_buffer {
        return {data_.data() + read_pos_, write_pos_ - read_pos_};
    }

    /// 消费已读取的 n 字节
    void consume(std::size_t n) noexcept {
        read_pos_ += n;
        if (read_pos_ == write_pos_) {
            read_pos_ = 0;
            write_pos_ = 0;
        }
    }

    /// 可读字节数
    [[nodiscard]] auto readable_bytes() const noexcept -> std::size_t {
        return write_pos_ - read_pos_;
    }

private:
    std::vector<std::byte> data_;
    std::size_t read_pos_ = 0;
    std::size_t write_pos_ = 0;
};

// =============================================================================
// 字节序转换 (Endianness)
// =============================================================================

/// 字节序枚举
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

// --- 泛型 byte_swap ---

export constexpr auto byte_swap(std::uint16_t v) noexcept -> std::uint16_t { return detail::bswap16(v); }
export constexpr auto byte_swap(std::uint32_t v) noexcept -> std::uint32_t { return detail::bswap32(v); }
export constexpr auto byte_swap(std::uint64_t v) noexcept -> std::uint64_t { return detail::bswap64(v); }

// =============================================================================
// buffer_reader — 从缓冲区按指定字节序读取整数
// =============================================================================

export class buffer_reader {
public:
    explicit buffer_reader(const_buffer buf) noexcept
        : data_(static_cast<const std::byte*>(buf.data))
        , size_(buf.size) {}

    explicit buffer_reader(std::span<const std::byte> s) noexcept
        : data_(s.data()), size_(s.size()) {}

    /// 剩余可读字节
    [[nodiscard]] auto remaining() const noexcept -> std::size_t {
        return size_ - pos_;
    }

    /// 当前偏移
    [[nodiscard]] auto position() const noexcept -> std::size_t {
        return pos_;
    }

    /// 跳过 n 字节
    auto skip(std::size_t n) noexcept -> bool {
        if (remaining() < n) return false;
        pos_ += n;
        return true;
    }

    /// 读取原始字节
    auto read_bytes(void* dst, std::size_t n) noexcept -> bool {
        if (remaining() < n) return false;
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
        return true;
    }

    // --- 大端 (network byte order) ---

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

    // --- 小端 ---

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
// buffer_writer — 向缓冲区按指定字节序写入整数
// =============================================================================

export class buffer_writer {
public:
    explicit buffer_writer(mutable_buffer buf) noexcept
        : data_(static_cast<std::byte*>(buf.data))
        , capacity_(buf.size) {}

    explicit buffer_writer(std::span<std::byte> s) noexcept
        : data_(s.data()), capacity_(s.size()) {}

    /// 剩余可写字节
    [[nodiscard]] auto remaining() const noexcept -> std::size_t {
        return capacity_ - pos_;
    }

    /// 已写入字节数
    [[nodiscard]] auto written() const noexcept -> std::size_t {
        return pos_;
    }

    /// 写入原始字节
    auto write_bytes(const void* src, std::size_t n) noexcept -> bool {
        if (remaining() < n) return false;
        std::memcpy(data_ + pos_, src, n);
        pos_ += n;
        return true;
    }

    // --- 大端 (network byte order) ---

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

    // --- 小端 ---

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
