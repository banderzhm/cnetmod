module;

#include <cnetmod/config.hpp>
#include <cstring>

module cnetmod.core.buffer;

import std;
import cnetmod.utils.converter;

namespace cnetmod {
const_buffer::const_buffer() noexcept = default;

const_buffer::const_buffer(const void* pointer, std::size_t length) noexcept
    : data(pointer), size(length) {}

const_buffer::const_buffer(std::span<const std::byte> bytes) noexcept
    : data(bytes.data()), size(bytes.size()) {}

auto const_buffer::empty() const noexcept -> bool
{
    return size == 0U;
}

auto const_buffer::bytes() const noexcept -> std::span<const std::byte>
{
    return {static_cast<const std::byte*>(data), size};
}

auto const_buffer::subspan(std::size_t offset, std::size_t count) const noexcept -> const_buffer
{
    if (offset > size)
        return {};
    const auto available = size - offset;
    return {static_cast<const std::byte*>(data) + offset,
        count == std::dynamic_extent ? available : std::min(count, available)};
}

auto const_buffer::begin() const noexcept -> const std::byte*
{
    return static_cast<const std::byte*>(data);
}

auto const_buffer::end() const noexcept -> const std::byte*
{
    return begin() + size;
}

auto const_buffer::front() const noexcept -> const std::byte&
{
    return *begin();
}

auto const_buffer::operator[](std::size_t index) const noexcept -> const std::byte&
{
    return begin()[index];
}

const_buffer::operator std::span<const std::byte>() const noexcept
{
    return bytes();
}

mutable_buffer::mutable_buffer() noexcept = default;

mutable_buffer::mutable_buffer(void* pointer, std::size_t length) noexcept
    : data(pointer), size(length) {}

mutable_buffer::mutable_buffer(std::span<std::byte> bytes) noexcept
    : data(bytes.data()), size(bytes.size()) {}

mutable_buffer::operator const_buffer() const noexcept
{
    return {data, size};
}

auto mutable_buffer::bytes() const noexcept -> std::span<std::byte>
{
    return {static_cast<std::byte*>(data), size};
}

mutable_buffer::operator std::span<std::byte>() const noexcept
{
    return bytes();
}

byte_view::byte_view() noexcept = default;

byte_view::byte_view(const std::byte* data, std::size_t size) noexcept
    : data_(data), size_(size) {}

byte_view::byte_view(std::span<const std::byte> bytes) noexcept
    : data_(bytes.data()), size_(bytes.size()) {}

byte_view::byte_view(const_buffer buffer) noexcept
    : data_(static_cast<const std::byte*>(buffer.data)), size_(buffer.size) {}

auto byte_view::data() const noexcept -> const std::byte*
{
    return data_;
}

auto byte_view::size() const noexcept -> std::size_t
{
    return size_;
}

auto byte_view::empty() const noexcept -> bool
{
    return size_ == 0U;
}

auto byte_view::begin() const noexcept -> const std::byte*
{
    return data_;
}

auto byte_view::end() const noexcept -> const std::byte*
{
    return data_ + size_;
}

auto byte_view::front() const noexcept -> const std::byte&
{
    return *data_;
}

auto byte_view::operator[](std::size_t index) const noexcept -> const std::byte&
{
    return data_[index];
}

auto byte_view::subspan(std::size_t offset, std::size_t count) const noexcept -> byte_view
{
    if (offset > size_)
        return {};
    const auto available = size_ - offset;
    return {data_ + offset,
        count == std::dynamic_extent ? available : std::min(count, available)};
}

auto byte_view::first(std::size_t count) const noexcept -> byte_view
{
    return {data_, std::min(count, size_)};
}

byte_view::operator const_buffer() const noexcept
{
    return {data_, size_};
}

byte_view::operator std::span<const std::byte>() const noexcept
{
    return {data_, size_};
}

auto hton(std::uint16_t value) noexcept -> std::uint16_t
{
    return ::utils::conv::hton(value);
}

auto hton(std::uint32_t value) noexcept -> std::uint32_t
{
    return ::utils::conv::hton(value);
}

auto hton(std::uint64_t value) noexcept -> std::uint64_t
{
    return ::utils::conv::hton(value);
}

auto ntoh(std::uint16_t value) noexcept -> std::uint16_t
{
    return ::utils::conv::ntoh(value);
}

auto ntoh(std::uint32_t value) noexcept -> std::uint32_t
{
    return ::utils::conv::ntoh(value);
}

auto ntoh(std::uint64_t value) noexcept -> std::uint64_t
{
    return ::utils::conv::ntoh(value);
}

auto htole(std::uint16_t value) noexcept -> std::uint16_t
{
    return ::utils::conv::htole(value);
}

auto htole(std::uint32_t value) noexcept -> std::uint32_t
{
    return ::utils::conv::htole(value);
}

auto htole(std::uint64_t value) noexcept -> std::uint64_t
{
    return ::utils::conv::htole(value);
}

auto letoh(std::uint16_t value) noexcept -> std::uint16_t
{
    return ::utils::conv::letoh(value);
}

auto letoh(std::uint32_t value) noexcept -> std::uint32_t
{
    return ::utils::conv::letoh(value);
}

auto letoh(std::uint64_t value) noexcept -> std::uint64_t
{
    return ::utils::conv::letoh(value);
}

auto byte_swap(std::uint16_t value) noexcept -> std::uint16_t
{
    return ::utils::conv::bswap16(value);
}

auto byte_swap(std::uint32_t value) noexcept -> std::uint32_t
{
    return ::utils::conv::bswap32(value);
}

auto byte_swap(std::uint64_t value) noexcept -> std::uint64_t
{
    return ::utils::conv::bswap64(value);
}

byte_buffer::byte_buffer() = default;

byte_buffer::byte_buffer(size_type size) : storage_(size) {}

byte_buffer::byte_buffer(size_type size, std::byte value) : storage_(size, value) {}

byte_buffer::byte_buffer(std::initializer_list<std::byte> values) : storage_(values) {}

auto byte_buffer::data() noexcept -> std::byte*
{
    return storage_.data();
}

auto byte_buffer::data() const noexcept -> const std::byte*
{
    return storage_.data();
}

auto byte_buffer::size() const noexcept -> size_type
{
    return storage_.size();
}

auto byte_buffer::capacity() const noexcept -> size_type
{
    return storage_.capacity();
}

auto byte_buffer::empty() const noexcept -> bool
{
    return storage_.empty();
}

auto byte_buffer::begin() noexcept -> iterator
{
    return storage_.begin();
}

auto byte_buffer::begin() const noexcept -> const_iterator
{
    return storage_.begin();
}

auto byte_buffer::end() noexcept -> iterator
{
    return storage_.end();
}

auto byte_buffer::end() const noexcept -> const_iterator
{
    return storage_.end();
}

auto byte_buffer::front() noexcept -> std::byte&
{
    return storage_.front();
}

auto byte_buffer::front() const noexcept -> const std::byte&
{
    return storage_.front();
}

auto byte_buffer::back() noexcept -> std::byte&
{
    return storage_.back();
}

auto byte_buffer::back() const noexcept -> const std::byte&
{
    return storage_.back();
}

auto byte_buffer::operator[](size_type index) noexcept -> std::byte&
{
    return storage_[index];
}

auto byte_buffer::operator[](size_type index) const noexcept -> const std::byte&
{
    return storage_[index];
}

void byte_buffer::reserve(size_type capacity)
{
    storage_.reserve(capacity);
}

void byte_buffer::resize(size_type size)
{
    storage_.resize(size);
}

void byte_buffer::resize(size_type size, std::byte value)
{
    storage_.resize(size, value);
}

void byte_buffer::clear() noexcept
{
    storage_.clear();
}

void byte_buffer::push_back(std::byte value)
{
    storage_.push_back(value);
}

void byte_buffer::pop_back()
{
    storage_.pop_back();
}

auto byte_buffer::insert(const_iterator position, std::byte value) -> iterator
{
    return storage_.insert(position, value);
}

auto byte_buffer::erase(const_iterator position) -> iterator
{
    return storage_.erase(position);
}

auto byte_buffer::erase(const_iterator first, const_iterator last) -> iterator
{
    return storage_.erase(first, last);
}

void byte_buffer::append(byte_view source)
{
    storage_.insert(storage_.end(), source.begin(), source.end());
}

void byte_buffer::append(const_buffer source)
{
    append(byte_view{source});
}

void byte_buffer::append(std::span<const std::byte> source)
{
    append(byte_view{source});
}

auto byte_buffer::view() const noexcept -> byte_view
{
    return {storage_.data(), storage_.size()};
}

auto byte_buffer::writable_view() noexcept -> mutable_buffer
{
    return {storage_.data(), storage_.size()};
}

byte_buffer::operator byte_view() const noexcept
{
    return view();
}

byte_buffer::operator const_buffer() const noexcept
{
    const auto bytes = view();
    return {bytes.data(), bytes.size()};
}

byte_buffer::operator std::span<const std::byte>() const noexcept
{
    return static_cast<std::span<const std::byte>>(view());
}

byte_buffer::operator std::span<std::byte>() noexcept
{
    return writable_view().bytes();
}

aligned_buffer::aligned_buffer(std::size_t size, std::size_t alignment)
{
    if (alignment == 0 || !std::has_single_bit(alignment))
    {
        throw std::invalid_argument(
            "aligned_buffer alignment must be a power of two");
    }
    alignment_ = std::max(alignment, alignof(std::max_align_t));
    if (size > std::numeric_limits<std::size_t>::max() -
            (alignment_ - 1))
    {
        throw std::length_error("aligned_buffer size is too large");
    }
    size_ = size == 0 ? 0 : ((size + alignment_ - 1) / alignment_) * alignment_;
    if (size_ != 0)
    {
        data_ = static_cast<std::byte*>(
            ::operator new(size_, std::align_val_t{alignment_}));
    }
}

aligned_buffer::~aligned_buffer()
{
    if (data_)
        ::operator delete(data_, std::align_val_t{alignment_});
}

aligned_buffer::aligned_buffer(aligned_buffer&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)),
      size_(std::exchange(other.size_, 0)),
      alignment_(std::exchange(other.alignment_, 0))
{
}

auto aligned_buffer::operator=(aligned_buffer&& other) noexcept
    -> aligned_buffer&
{
    if (this != &other)
    {
        if (data_)
            ::operator delete(data_, std::align_val_t{alignment_});
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
        alignment_ = std::exchange(other.alignment_, 0);
    }
    return *this;
}

auto aligned_buffer::data() noexcept -> std::byte*
{
    return data_;
}

auto aligned_buffer::data() const noexcept -> const std::byte*
{
    return data_;
}

auto aligned_buffer::size() const noexcept -> std::size_t
{
    return size_;
}

auto aligned_buffer::alignment() const noexcept -> std::size_t
{
    return alignment_;
}

auto aligned_buffer::writable() noexcept -> mutable_buffer
{
    return {data_, size_};
}

auto aligned_buffer::readable() const noexcept -> const_buffer
{
    return {data_, size_};
}

auto buffer(const void* data, std::size_t size) noexcept -> const_buffer
{
    return {data, size};
}

auto buffer(void* data, std::size_t size) noexcept -> mutable_buffer
{
    return {data, size};
}

dynamic_buffer::dynamic_buffer(std::size_t initial_capacity)
    : data_(initial_capacity)
{
}

auto dynamic_buffer::prepare(std::size_t size) -> mutable_buffer
{
    if (write_pos_ + size > data_.size() && read_pos_ > 0)
    {
        const auto readable = write_pos_ - read_pos_;
        std::memmove(data_.data(), data_.data() + read_pos_, readable);
        read_pos_ = 0;
        write_pos_ = readable;
    }
    if (write_pos_ + size > data_.size())
    {
        data_.resize(write_pos_ + size);
    }
    return {data_.data() + write_pos_, size};
}

void dynamic_buffer::commit(std::size_t size) noexcept
{
    write_pos_ += size;
}

auto dynamic_buffer::data() const noexcept -> const_buffer
{
    return {data_.data() + read_pos_, write_pos_ - read_pos_};
}

void dynamic_buffer::consume(std::size_t size) noexcept
{
    size = std::min(size, write_pos_ - read_pos_);
    read_pos_ += size;
    if (read_pos_ == write_pos_)
    {
        read_pos_ = 0;
        write_pos_ = 0;
    }
}

auto dynamic_buffer::readable_bytes() const noexcept -> std::size_t
{
    return write_pos_ - read_pos_;
}

auto dynamic_buffer::readable_view() const noexcept -> byte_view
{
    return {data_.data() + read_pos_, write_pos_ - read_pos_};
}

void dynamic_buffer::append(byte_view bytes)
{
    if (bytes.empty())
        return;
    auto destination = prepare(bytes.size());
    std::memcpy(destination.data, bytes.data(), bytes.size());
    commit(bytes.size());
}

void dynamic_buffer::clear() noexcept
{
    read_pos_ = 0;
    write_pos_ = 0;
}

void dynamic_buffer::reserve(std::size_t capacity)
{
    if (capacity <= data_.size())
        return;
    const auto readable = write_pos_ - read_pos_;
    if (read_pos_ != 0U && readable != 0U)
        std::memmove(data_.data(), data_.data() + read_pos_, readable);
    read_pos_ = 0;
    write_pos_ = readable;
    data_.resize(capacity);
}

buffer_reader::buffer_reader(const_buffer buffer) noexcept
    : data_(static_cast<const std::byte*>(buffer.data)), size_(buffer.size)
{
}

buffer_reader::buffer_reader(std::span<const std::byte> buffer) noexcept
    : data_(buffer.data()), size_(buffer.size())
{
}

auto buffer_reader::remaining() const noexcept -> std::size_t
{
    return size_ - pos_;
}

auto buffer_reader::position() const noexcept -> std::size_t
{
    return pos_;
}

auto buffer_reader::skip(std::size_t size) noexcept -> bool
{
    if (remaining() < size)
        return false;
    pos_ += size;
    return true;
}

auto buffer_reader::read_bytes(void* destination, std::size_t size) noexcept -> bool
{
    if (remaining() < size)
        return false;
    std::memcpy(destination, data_ + pos_, size);
    pos_ += size;
    return true;
}

auto buffer_reader::read_u8() noexcept -> std::optional<std::uint8_t>
{
    if (remaining() < 1)
        return std::nullopt;
    return static_cast<std::uint8_t>(data_[pos_++]);
}

auto buffer_reader::read_u16_be() noexcept -> std::optional<std::uint16_t>
{
    if (remaining() < 2)
        return std::nullopt;
    std::uint16_t value;
    std::memcpy(&value, data_ + pos_, sizeof(value));
    pos_ += sizeof(value);
    return ntoh(value);
}

auto buffer_reader::read_u32_be() noexcept -> std::optional<std::uint32_t>
{
    if (remaining() < 4)
        return std::nullopt;
    std::uint32_t value;
    std::memcpy(&value, data_ + pos_, sizeof(value));
    pos_ += sizeof(value);
    return ntoh(value);
}

auto buffer_reader::read_u64_be() noexcept -> std::optional<std::uint64_t>
{
    if (remaining() < 8)
        return std::nullopt;
    std::uint64_t value;
    std::memcpy(&value, data_ + pos_, sizeof(value));
    pos_ += sizeof(value);
    return ntoh(value);
}

auto buffer_reader::read_u16_le() noexcept -> std::optional<std::uint16_t>
{
    if (remaining() < 2)
        return std::nullopt;
    std::uint16_t value;
    std::memcpy(&value, data_ + pos_, sizeof(value));
    pos_ += sizeof(value);
    return letoh(value);
}

auto buffer_reader::read_u32_le() noexcept -> std::optional<std::uint32_t>
{
    if (remaining() < 4)
        return std::nullopt;
    std::uint32_t value;
    std::memcpy(&value, data_ + pos_, sizeof(value));
    pos_ += sizeof(value);
    return letoh(value);
}

auto buffer_reader::read_u64_le() noexcept -> std::optional<std::uint64_t>
{
    if (remaining() < 8)
        return std::nullopt;
    std::uint64_t value;
    std::memcpy(&value, data_ + pos_, sizeof(value));
    pos_ += sizeof(value);
    return letoh(value);
}

buffer_writer::buffer_writer(mutable_buffer buffer) noexcept
    : data_(static_cast<std::byte*>(buffer.data)), capacity_(buffer.size)
{
}

buffer_writer::buffer_writer(std::span<std::byte> buffer) noexcept
    : data_(buffer.data()), capacity_(buffer.size())
{
}

auto buffer_writer::remaining() const noexcept -> std::size_t
{
    return capacity_ - pos_;
}

auto buffer_writer::written() const noexcept -> std::size_t
{
    return pos_;
}

auto buffer_writer::write_bytes(const void* source, std::size_t size) noexcept -> bool
{
    if (remaining() < size)
        return false;
    std::memcpy(data_ + pos_, source, size);
    pos_ += size;
    return true;
}

auto buffer_writer::write_u8(std::uint8_t value) noexcept -> bool
{
    if (remaining() < 1)
        return false;
    data_[pos_++] = static_cast<std::byte>(value);
    return true;
}

auto buffer_writer::write_u16_be(std::uint16_t value) noexcept -> bool
{
    if (remaining() < 2)
        return false;
    const auto network = hton(value);
    std::memcpy(data_ + pos_, &network, sizeof(network));
    pos_ += sizeof(network);
    return true;
}

auto buffer_writer::write_u32_be(std::uint32_t value) noexcept -> bool
{
    if (remaining() < 4)
        return false;
    const auto network = hton(value);
    std::memcpy(data_ + pos_, &network, sizeof(network));
    pos_ += sizeof(network);
    return true;
}

auto buffer_writer::write_u64_be(std::uint64_t value) noexcept -> bool
{
    if (remaining() < 8)
        return false;
    const auto network = hton(value);
    std::memcpy(data_ + pos_, &network, sizeof(network));
    pos_ += sizeof(network);
    return true;
}

auto buffer_writer::write_u16_le(std::uint16_t value) noexcept -> bool
{
    if (remaining() < 2)
        return false;
    const auto little_endian = htole(value);
    std::memcpy(data_ + pos_, &little_endian, sizeof(little_endian));
    pos_ += sizeof(little_endian);
    return true;
}

auto buffer_writer::write_u32_le(std::uint32_t value) noexcept -> bool
{
    if (remaining() < 4)
        return false;
    const auto little_endian = htole(value);
    std::memcpy(data_ + pos_, &little_endian, sizeof(little_endian));
    pos_ += sizeof(little_endian);
    return true;
}

auto buffer_writer::write_u64_le(std::uint64_t value) noexcept -> bool
{
    if (remaining() < 8)
        return false;
    const auto little_endian = htole(value);
    std::memcpy(data_ + pos_, &little_endian, sizeof(little_endian));
    pos_ += sizeof(little_endian);
    return true;
}
} // namespace cnetmod
