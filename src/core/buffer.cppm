module;

#include <cnetmod/config.hpp>

export module cnetmod.core.buffer;

import std;

namespace cnetmod {

// =============================================================================
// Basic buffer views
// =============================================================================

/// Read-only buffer view (does not own data)
export struct const_buffer
{
    const void* data = nullptr;
    std::size_t size = 0;

    const_buffer() noexcept;
    const_buffer(const void* pointer, std::size_t size) noexcept;

    /// Construct from span
    const_buffer(std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] auto empty() const noexcept -> bool;
    [[nodiscard]] auto bytes() const noexcept -> std::span<const std::byte>;

    [[nodiscard]] auto subspan(std::size_t offset,
        std::size_t count = std::dynamic_extent) const noexcept -> const_buffer;
    [[nodiscard]] auto begin() const noexcept -> const std::byte*;
    [[nodiscard]] auto end() const noexcept -> const std::byte*;
    [[nodiscard]] auto front() const noexcept -> const std::byte&;

    [[nodiscard]] auto operator[](std::size_t index) const noexcept
        -> const std::byte&;
    operator std::span<const std::byte>() const noexcept;
};

/// Writable buffer view (does not own data)
export struct mutable_buffer
{
    void* data = nullptr;
    std::size_t size = 0;

    mutable_buffer() noexcept;
    mutable_buffer(void* pointer, std::size_t size) noexcept;

    /// Construct from span
    mutable_buffer(std::span<std::byte> bytes) noexcept;

    /// Implicit conversion to const_buffer
    operator const_buffer() const noexcept;
    [[nodiscard]] auto bytes() const noexcept -> std::span<std::byte>;
    operator std::span<std::byte>() const noexcept;
};

/// Typed, non-owning read-only byte view for protocol parsers. This is the
/// span-like counterpart to const_buffer and keeps std::byte representation
/// details inside the core buffer module.
export class byte_view
{
public:
    byte_view() noexcept;
    byte_view(const std::byte* data, std::size_t size) noexcept;
    byte_view(std::span<const std::byte> bytes) noexcept;
    byte_view(const_buffer buffer) noexcept;
    [[nodiscard]] auto data() const noexcept -> const std::byte*;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto empty() const noexcept -> bool;
    [[nodiscard]] auto begin() const noexcept -> const std::byte*;
    [[nodiscard]] auto end() const noexcept -> const std::byte*;
    [[nodiscard]] auto front() const noexcept -> const std::byte&;
    [[nodiscard]] auto operator[](std::size_t index) const noexcept
        -> const std::byte&;

    [[nodiscard]] auto subspan(std::size_t offset,
        std::size_t count = std::dynamic_extent) const noexcept -> byte_view;

    [[nodiscard]] auto first(std::size_t count) const noexcept -> byte_view;
    operator const_buffer() const noexcept;
    operator std::span<const std::byte>() const noexcept;

private:
    const std::byte* data_{};
    std::size_t size_{};
};

// =============================================================================
// Owning byte buffer
// =============================================================================

/// Project-owned contiguous byte storage. Protocol APIs use this type instead
/// of exposing std::vector<std::byte>, allowing allocation and pooling policy
/// to evolve without changing every protocol interface.
export class byte_buffer
{
public:
    using value_type = std::byte;
    using size_type = std::size_t;
    using iterator = std::vector<std::byte>::iterator;
    using const_iterator = std::vector<std::byte>::const_iterator;

    byte_buffer();
    explicit byte_buffer(size_type size);
    byte_buffer(size_type size, std::byte value);
    byte_buffer(std::initializer_list<std::byte> values);

    template <std::input_iterator Iterator,
        std::sentinel_for<Iterator> Sentinel>
    byte_buffer(Iterator first, Sentinel last)
        : storage_(first, last) {}

    [[nodiscard]] auto data() noexcept -> std::byte*;
    [[nodiscard]] auto data() const noexcept -> const std::byte*;
    [[nodiscard]] auto size() const noexcept -> size_type;
    [[nodiscard]] auto capacity() const noexcept -> size_type;
    [[nodiscard]] auto empty() const noexcept -> bool;
    [[nodiscard]] auto begin() noexcept -> iterator;
    [[nodiscard]] auto begin() const noexcept -> const_iterator;
    [[nodiscard]] auto end() noexcept -> iterator;
    [[nodiscard]] auto end() const noexcept -> const_iterator;
    [[nodiscard]] auto front() noexcept -> std::byte&;
    [[nodiscard]] auto front() const noexcept -> const std::byte&;
    [[nodiscard]] auto back() noexcept -> std::byte&;
    [[nodiscard]] auto back() const noexcept -> const std::byte&;
    [[nodiscard]] auto operator[](size_type index) noexcept -> std::byte&;
    [[nodiscard]] auto operator[](size_type index) const noexcept -> const std::byte&;

    void reserve(size_type capacity);
    void resize(size_type size);
    void resize(size_type size, std::byte value);
    void clear() noexcept;
    void push_back(std::byte value);
    void pop_back();

    template <class... Arguments>
    auto emplace_back(Arguments&&... arguments) -> std::byte&
    {
        return storage_.emplace_back(std::forward<Arguments>(arguments)...);
    }

    template <std::input_iterator Iterator,
        std::sentinel_for<Iterator> Sentinel>
    auto insert(const_iterator position, Iterator first, Sentinel last)
        -> iterator
    {
        return storage_.insert(position, first, last);
    }

    auto insert(const_iterator position, std::byte value) -> iterator;
    auto erase(const_iterator position) -> iterator;
    auto erase(const_iterator first, const_iterator last) -> iterator;

    template <std::input_iterator Iterator,
        std::sentinel_for<Iterator> Sentinel>
    void assign(Iterator first, Sentinel last)
    {
        storage_.assign(first, last);
    }

    void append(byte_view source);
    void append(const_buffer source);
    void append(std::span<const std::byte> source);
    [[nodiscard]] auto view() const noexcept -> byte_view;
    [[nodiscard]] auto writable_view() noexcept -> mutable_buffer;
    operator byte_view() const noexcept;
    operator const_buffer() const noexcept;
    operator std::span<const std::byte>() const noexcept;
    operator std::span<std::byte>() noexcept;

private:
    std::vector<std::byte> storage_;
};

// =============================================================================
// Factory functions
// =============================================================================

/// Create const_buffer from raw pointer and size
export auto buffer(const void* data, std::size_t size) noexcept -> const_buffer;

/// Create mutable_buffer from raw pointer and size
export auto buffer(void* data, std::size_t size) noexcept -> mutable_buffer;

/// Owning buffer with explicit power-of-two alignment. Useful with
/// open_mode::direct, where platforms require aligned addresses and sizes.
export class aligned_buffer
{
public:
    explicit aligned_buffer(std::size_t size,
        std::size_t alignment = 4096);
    ~aligned_buffer();

    aligned_buffer(const aligned_buffer&) = delete;
    auto operator=(const aligned_buffer&) -> aligned_buffer& = delete;
    aligned_buffer(aligned_buffer&& other) noexcept;
    auto operator=(aligned_buffer&& other) noexcept -> aligned_buffer&;

    [[nodiscard]] auto data() noexcept -> std::byte*;
    [[nodiscard]] auto data() const noexcept -> const std::byte*;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto alignment() const noexcept -> std::size_t;
    [[nodiscard]] auto writable() noexcept -> mutable_buffer;
    [[nodiscard]] auto readable() const noexcept -> const_buffer;

private:
    std::byte* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t alignment_ = 0;
};

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
export class dynamic_buffer
{
public:
    explicit dynamic_buffer(std::size_t initial_capacity = 4096);

    /// Get writable region
    [[nodiscard]] auto prepare(std::size_t n) -> mutable_buffer;

    /// Confirm n bytes written
    void commit(std::size_t n) noexcept;

    /// Get readable data
    [[nodiscard]] auto data() const noexcept -> const_buffer;

    /// Consume n bytes read
    void consume(std::size_t n) noexcept;

    /// Number of readable bytes
    [[nodiscard]] auto readable_bytes() const noexcept -> std::size_t;

    [[nodiscard]] auto readable_view() const noexcept -> byte_view;

    /// Append bytes while retaining geometric storage growth internally.
    void append(byte_view bytes);

    /// Remove all readable bytes while retaining capacity for reuse.
    void clear() noexcept;

    /// Reserve storage without changing the readable byte count.
    void reserve(std::size_t capacity);

private:
    std::vector<std::byte> data_;
    std::size_t read_pos_ = 0;
    std::size_t write_pos_ = 0;
};

// =============================================================================
// Byte order conversion (Endianness)
// =============================================================================

/// Byte order enum
export auto hton(std::uint16_t value) noexcept -> std::uint16_t;
export auto hton(std::uint32_t value) noexcept -> std::uint32_t;
export auto hton(std::uint64_t value) noexcept -> std::uint64_t;
export auto ntoh(std::uint16_t value) noexcept -> std::uint16_t;
export auto ntoh(std::uint32_t value) noexcept -> std::uint32_t;
export auto ntoh(std::uint64_t value) noexcept -> std::uint64_t;
export auto htole(std::uint16_t value) noexcept -> std::uint16_t;
export auto htole(std::uint32_t value) noexcept -> std::uint32_t;
export auto htole(std::uint64_t value) noexcept -> std::uint64_t;
export auto letoh(std::uint16_t value) noexcept -> std::uint16_t;
export auto letoh(std::uint32_t value) noexcept -> std::uint32_t;
export auto letoh(std::uint64_t value) noexcept -> std::uint64_t;
export auto byte_swap(std::uint16_t value) noexcept -> std::uint16_t;
export auto byte_swap(std::uint32_t value) noexcept -> std::uint32_t;
export auto byte_swap(std::uint64_t value) noexcept -> std::uint64_t;

// =============================================================================
// buffer_reader — Read integers from buffer in specified byte order
// =============================================================================

export class buffer_reader
{
public:
    explicit buffer_reader(const_buffer buf) noexcept;

    explicit buffer_reader(std::span<const std::byte> s) noexcept;

    /// Remaining readable bytes
    [[nodiscard]] auto remaining() const noexcept -> std::size_t;

    /// Current offset
    [[nodiscard]] auto position() const noexcept -> std::size_t;

    /// Skip n bytes
    auto skip(std::size_t n) noexcept -> bool;

    /// Read raw bytes
    auto read_bytes(void* dst, std::size_t n) noexcept -> bool;

    // --- Big-endian (network byte order) ---

    auto read_u8() noexcept -> std::optional<std::uint8_t>;

    auto read_u16_be() noexcept -> std::optional<std::uint16_t>;

    auto read_u32_be() noexcept -> std::optional<std::uint32_t>;

    auto read_u64_be() noexcept -> std::optional<std::uint64_t>;

    // --- Little-endian ---

    auto read_u16_le() noexcept -> std::optional<std::uint16_t>;

    auto read_u32_le() noexcept -> std::optional<std::uint32_t>;

    auto read_u64_le() noexcept -> std::optional<std::uint64_t>;

private:
    const std::byte* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

// =============================================================================
// buffer_writer — Write integers to buffer in specified byte order
// =============================================================================

export class buffer_writer
{
public:
    explicit buffer_writer(mutable_buffer buf) noexcept;

    explicit buffer_writer(std::span<std::byte> s) noexcept;

    /// Remaining writable bytes
    [[nodiscard]] auto remaining() const noexcept -> std::size_t;

    /// Number of bytes written
    [[nodiscard]] auto written() const noexcept -> std::size_t;

    /// Write raw bytes
    auto write_bytes(const void* src, std::size_t n) noexcept -> bool;

    // --- Big-endian (network byte order) ---

    auto write_u8(std::uint8_t v) noexcept -> bool;

    auto write_u16_be(std::uint16_t v) noexcept -> bool;

    auto write_u32_be(std::uint32_t v) noexcept -> bool;

    auto write_u64_be(std::uint64_t v) noexcept -> bool;

    // --- Little-endian ---

    auto write_u16_le(std::uint16_t v) noexcept -> bool;

    auto write_u32_le(std::uint32_t v) noexcept -> bool;

    auto write_u64_le(std::uint64_t v) noexcept -> bool;

private:
    std::byte* data_;
    std::size_t capacity_;
    std::size_t pos_ = 0;
};

} // namespace cnetmod
