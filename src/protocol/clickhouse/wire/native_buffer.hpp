#pragma once

// Private implementation shared by codec and asynchronous connection units.
// Included after importing the primary module; no textual standard headers.
namespace cnetmod::clickhouse::detail {
struct failure
{
    error value;
};

[[noreturn]] inline void fail(std::string message, error_kind kind = error_kind::protocol)
{
    throw failure{error{.kind = kind, .message = std::move(message)}};
}

[[noreturn]] inline void incomplete()
{
    throw failure{error{.cause = std::make_error_code(std::errc::result_out_of_range),
        .message = "incomplete Native packet"}};
}

struct reader
{
    std::span<const std::byte> input;
    limits bounds;
    std::size_t position{}, values{}, decoded_bytes{};
    std::uint64_t revision{54401};

    inline auto take(std::size_t count) -> std::span<const std::byte>
    {
        if (count > bounds.max_packet_bytes || position > bounds.max_packet_bytes - count)
            fail("Native packet exceeds byte limit");
        if (count > input.size() - position)
            incomplete();
        auto result = input.subspan(position, count);
        position += count;
        return result;
    }

    inline auto fixed(std::size_t count) -> std::uint64_t
    {
        auto bytes = take(count);
        std::uint64_t result{};
        for (std::size_t i{}; i < count; ++i)
            result |= std::uint64_t(std::to_integer<unsigned char>(bytes[i])) << (i * 8);
        return result;
    }

    inline auto varint() -> std::uint64_t
    {
        std::uint64_t result{};
        for (unsigned i{}; i < 10; ++i)
        {
            const auto byte = fixed(1);
            if (i == 9 && byte > 1)
                fail("overflowing Native VarUInt");
            result |= (byte & 127) << (i * 7);
            if (!(byte & 128))
                return result;
        }
        fail("unterminated Native VarUInt");
    }

    inline auto string() -> std::string
    {
        auto length = varint();
        if (length > bounds.max_string_bytes)
            fail("Native string exceeds limit");
        auto bytes = take(static_cast<std::size_t>(length));
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    inline void account(std::size_t count)
    {
        if (count > bounds.max_values || values > bounds.max_values - count)
            fail("Native values exceed limit");
        values += count;
    }
};

struct writer
{
    std::vector<std::byte> output;
    limits bounds;
    std::uint64_t revision{54401};

    inline void bytes(std::string_view text)
    {
        if (text.size() > bounds.max_packet_bytes || output.size() > bounds.max_packet_bytes - text.size())
            fail("Native packet exceeds byte limit", error_kind::invalid_argument);
        auto input = std::as_bytes(std::span{text.data(), text.size()});
        output.insert(output.end(), input.begin(), input.end());
    }

    inline void fixed(std::uint64_t value, std::size_t count)
    {
        std::array<char, 8> data{};
        for (std::size_t i{}; i < count; ++i)
            data[i] = static_cast<char>(value >> (i * 8));
        bytes({data.data(), count});
    }

    inline void varint(std::uint64_t value)
    {
        do
        {
            auto byte = static_cast<unsigned char>(value & 127);
            value >>= 7;
            fixed(byte | (value ? 128 : 0), 1);
        } while (value);
    }

    inline void string(std::string_view value)
    {
        if (value.size() > bounds.max_string_bytes)
            fail("Native string exceeds limit", error_kind::invalid_argument);
        varint(value.size());
        bytes(value);
    }
};
} // namespace cnetmod::clickhouse::detail
