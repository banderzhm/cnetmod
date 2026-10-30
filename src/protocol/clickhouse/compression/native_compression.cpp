module;
#include <city.h>
#include <cnetmod/config.hpp>
#include <lz4.h>
#include <zstd.h>
module cnetmod.protocol.clickhouse;
import std;
#include "../block/native_block.hpp"
#include "native_compression.hpp"

namespace cnetmod::clickhouse::detail {
namespace {
    auto checksum(std::span<const std::byte> bytes) -> std::array<std::uint64_t, 2>
    {
        const auto hash = CityHash_v1_0_2::CityHash128(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return {hash.low64, hash.high64};
    }
} // namespace

auto encode_frame(std::span<const std::byte> input, compression_mode mode, limits bounds) -> std::vector<std::byte>
{
    if (input.size() > bounds.max_packet_bytes || input.size() > static_cast<std::size_t>(LZ4_MAX_INPUT_SIZE))
        fail("Native compression input exceeds limit", error_kind::invalid_argument);
    std::string compressed;
    std::uint8_t method{};
    const auto* source = reinterpret_cast<const char*>(input.data());
    switch (mode)
    {
    case compression_mode::lz4:
    {
        method = 0x82;
        compressed.resize(LZ4_compressBound(static_cast<int>(input.size())));
        const auto size = LZ4_compress_default(source, compressed.data(), static_cast<int>(input.size()), static_cast<int>(compressed.size()));
        if (size <= 0)
            fail("Native LZ4 compression failed");
        compressed.resize(static_cast<std::size_t>(size));
        break;
    }
    case compression_mode::zstd:
    {
        method = 0x90;
        compressed.resize(ZSTD_compressBound(input.size()));
        const auto size = ZSTD_compress(compressed.data(), compressed.size(), source, input.size(), 1);
        if (ZSTD_isError(size))
            fail("Native Zstandard compression failed");
        compressed.resize(size);
        break;
    }
    case compression_mode::checksum:
        method = 0x02;
        compressed.assign(source, input.size());
        break;
    default:
        fail("Native framed compression mode is invalid", error_kind::invalid_argument);
    }
    writer body{{}, bounds};
    body.fixed(method, 1);
    body.fixed(compressed.size() + 9, 4);
    body.fixed(input.size(), 4);
    body.bytes(compressed);
    const auto hash = checksum(body.output);
    writer output{{}, bounds};
    output.fixed(hash[0], 8);
    output.fixed(hash[1], 8);
    output.bytes({reinterpret_cast<const char*>(body.output.data()), body.output.size()});
    return std::move(output.output);
}

auto decode_frame(reader& input) -> std::vector<std::byte>
{
    const auto expected_low = input.fixed(8), expected_high = input.fixed(8);
    const auto start = input.position;
    const auto method = input.fixed(1), compressed_size = input.fixed(4), raw_size = input.fixed(4);
    if (compressed_size < 9 || compressed_size > input.bounds.max_packet_bytes || raw_size > input.bounds.max_packet_bytes || raw_size > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        fail("Native compressed frame dimensions exceed limit");
    if (method != 0x02 && method != 0x82 && method != 0x90)
        fail("unsupported Native compression method");
    const auto compressed = input.take(static_cast<std::size_t>(compressed_size - 9));
    const auto actual = checksum(input.input.subspan(start, static_cast<std::size_t>(compressed_size)));
    if (actual[0] != expected_low || actual[1] != expected_high)
        fail("Native compressed checksum mismatch");
    std::vector<std::byte> result(static_cast<std::size_t>(raw_size));
    if (method == 0x02)
    {
        if (compressed.size() != result.size())
            fail("Native checksum frame size mismatch");
        std::ranges::copy(compressed, result.begin());
    }
    else if (method == 0x82)
    {
        if (compressed.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            fail("Native LZ4 input exceeds limit");
        const auto size = LZ4_decompress_safe(reinterpret_cast<const char*>(compressed.data()), reinterpret_cast<char*>(result.data()), static_cast<int>(compressed.size()), static_cast<int>(result.size()));
        if (size < 0 || static_cast<std::size_t>(size) != result.size())
            fail("invalid Native LZ4 block");
    }
    else
    {
        const auto size = ZSTD_decompress(result.data(), result.size(), compressed.data(), compressed.size());
        if (ZSTD_isError(size) || size != result.size())
            fail("invalid Native Zstandard block");
    }
    return result;
}

void write_data_packet(writer& output, const block& data, compression_mode mode, std::string_view table)
{
    output.varint(2);
    output.string(table);
    if (mode == compression_mode::none)
    {
        write_block(output, data);
        return;
    }
    writer raw{{}, output.bounds};
    raw.revision = output.revision;
    write_block(raw, data);
    // Native blocks can cross multiple independently checksummed frames.
    constexpr std::size_t chunk_size = 64 * 1024;
    for (std::size_t offset{}; offset < raw.output.size(); offset += chunk_size)
    {
        const auto chunk = std::span{raw.output}.subspan(offset, std::min(chunk_size, raw.output.size() - offset));
        const auto encoded = encode_frame(chunk, mode, output.bounds);
        output.bytes({reinterpret_cast<const char*>(encoded.data()), encoded.size()});
    }
}

auto read_data_block(reader& input, bool compressed) -> block
{
    if (!compressed)
        return read_block(input);
    std::vector<std::byte> raw;
    for (;;)
    {
        auto frame = decode_frame(input);
        if (frame.empty() || frame.size() > input.bounds.max_packet_bytes - raw.size())
            fail("Native decompressed block exceeds limit");
        raw.insert(raw.end(), frame.begin(), frame.end());
        try
        {
            reader decoded{raw, input.bounds};
            decoded.revision = input.revision;
            auto block = read_block(decoded);
            if (decoded.position != raw.size())
                fail("trailing bytes in compressed Native block");
            input.values += decoded.values;
            input.decoded_bytes += raw.size();
            return block;
        }
        catch (const failure& error)
        {
            if (error.value.cause != std::errc::result_out_of_range)
                throw;
        }
    }
}
} // namespace cnetmod::clickhouse::detail

namespace cnetmod::clickhouse::wire {
auto compress_frame(std::span<const std::byte> input, compression_mode mode, limits bounds) -> std::expected<std::vector<std::byte>, error>
{
    try
    {
        return detail::encode_frame(input, mode, bounds);
    }
    catch (detail::failure& error)
    {
        return std::unexpected(std::move(error.value));
    }
}

auto decompress_frame(std::span<const std::byte> bytes, limits bounds) -> std::expected<decoded_frame, error>
{
    try
    {
        detail::reader input{bytes, bounds};
        auto data = detail::decode_frame(input);
        return decoded_frame{std::move(data), input.position};
    }
    catch (detail::failure& error)
    {
        return std::unexpected(std::move(error.value));
    }
}
} // namespace cnetmod::clickhouse::wire
