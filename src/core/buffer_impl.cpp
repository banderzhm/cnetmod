module;

#include <cnetmod/config.hpp>
#include <cstring>

module cnetmod.core.buffer;

import std;

namespace cnetmod
{
    auto buffer(std::vector<std::byte>& value) noexcept -> mutable_buffer
    {
        return {value.data(), value.size()};
    }

    auto buffer(const std::vector<std::byte>& value) noexcept -> const_buffer
    {
        return {value.data(), value.size()};
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

    buffer_reader::buffer_reader(const_buffer buffer) noexcept
        : data_(static_cast<const std::byte*>(buffer.data)), size_(buffer.size)
    {
    }

    buffer_reader::buffer_reader(std::span<const std::byte> buffer) noexcept
        : data_(buffer.data()), size_(buffer.size())
    {
    }

    auto buffer_reader::remaining() const noexcept -> std::size_t { return size_ - pos_; }
    auto buffer_reader::position() const noexcept -> std::size_t { return pos_; }

    auto buffer_reader::skip(std::size_t size) noexcept -> bool
    {
        if (remaining() < size) return false;
        pos_ += size;
        return true;
    }

    auto buffer_reader::read_bytes(void* destination, std::size_t size) noexcept -> bool
    {
        if (remaining() < size) return false;
        std::memcpy(destination, data_ + pos_, size);
        pos_ += size;
        return true;
    }

    auto buffer_reader::read_u8() noexcept -> std::optional<std::uint8_t>
    {
        if (remaining() < 1) return std::nullopt;
        return static_cast<std::uint8_t>(data_[pos_++]);
    }

    auto buffer_reader::read_u16_be() noexcept -> std::optional<std::uint16_t>
    {
        if (remaining() < 2) return std::nullopt;
        std::uint16_t value;
        std::memcpy(&value, data_ + pos_, sizeof(value));
        pos_ += sizeof(value);
        return ntoh(value);
    }

    auto buffer_reader::read_u32_be() noexcept -> std::optional<std::uint32_t>
    {
        if (remaining() < 4) return std::nullopt;
        std::uint32_t value;
        std::memcpy(&value, data_ + pos_, sizeof(value));
        pos_ += sizeof(value);
        return ntoh(value);
    }

    auto buffer_reader::read_u64_be() noexcept -> std::optional<std::uint64_t>
    {
        if (remaining() < 8) return std::nullopt;
        std::uint64_t value;
        std::memcpy(&value, data_ + pos_, sizeof(value));
        pos_ += sizeof(value);
        return ntoh(value);
    }

    auto buffer_reader::read_u16_le() noexcept -> std::optional<std::uint16_t>
    {
        if (remaining() < 2) return std::nullopt;
        std::uint16_t value;
        std::memcpy(&value, data_ + pos_, sizeof(value));
        pos_ += sizeof(value);
        return letoh(value);
    }

    auto buffer_reader::read_u32_le() noexcept -> std::optional<std::uint32_t>
    {
        if (remaining() < 4) return std::nullopt;
        std::uint32_t value;
        std::memcpy(&value, data_ + pos_, sizeof(value));
        pos_ += sizeof(value);
        return letoh(value);
    }

    auto buffer_reader::read_u64_le() noexcept -> std::optional<std::uint64_t>
    {
        if (remaining() < 8) return std::nullopt;
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

    auto buffer_writer::remaining() const noexcept -> std::size_t { return capacity_ - pos_; }
    auto buffer_writer::written() const noexcept -> std::size_t { return pos_; }

    auto buffer_writer::write_bytes(const void* source, std::size_t size) noexcept -> bool
    {
        if (remaining() < size) return false;
        std::memcpy(data_ + pos_, source, size);
        pos_ += size;
        return true;
    }

    auto buffer_writer::write_u8(std::uint8_t value) noexcept -> bool
    {
        if (remaining() < 1) return false;
        data_[pos_++] = static_cast<std::byte>(value);
        return true;
    }

    auto buffer_writer::write_u16_be(std::uint16_t value) noexcept -> bool
    {
        if (remaining() < 2) return false;
        const auto network = hton(value);
        std::memcpy(data_ + pos_, &network, sizeof(network));
        pos_ += sizeof(network);
        return true;
    }

    auto buffer_writer::write_u32_be(std::uint32_t value) noexcept -> bool
    {
        if (remaining() < 4) return false;
        const auto network = hton(value);
        std::memcpy(data_ + pos_, &network, sizeof(network));
        pos_ += sizeof(network);
        return true;
    }

    auto buffer_writer::write_u64_be(std::uint64_t value) noexcept -> bool
    {
        if (remaining() < 8) return false;
        const auto network = hton(value);
        std::memcpy(data_ + pos_, &network, sizeof(network));
        pos_ += sizeof(network);
        return true;
    }

    auto buffer_writer::write_u16_le(std::uint16_t value) noexcept -> bool
    {
        if (remaining() < 2) return false;
        const auto little_endian = htole(value);
        std::memcpy(data_ + pos_, &little_endian, sizeof(little_endian));
        pos_ += sizeof(little_endian);
        return true;
    }

    auto buffer_writer::write_u32_le(std::uint32_t value) noexcept -> bool
    {
        if (remaining() < 4) return false;
        const auto little_endian = htole(value);
        std::memcpy(data_ + pos_, &little_endian, sizeof(little_endian));
        pos_ += sizeof(little_endian);
        return true;
    }

    auto buffer_writer::write_u64_le(std::uint64_t value) noexcept -> bool
    {
        if (remaining() < 8) return false;
        const auto little_endian = htole(value);
        std::memcpy(data_ + pos_, &little_endian, sizeof(little_endian));
        pos_ += sizeof(little_endian);
        return true;
    }
} // namespace cnetmod