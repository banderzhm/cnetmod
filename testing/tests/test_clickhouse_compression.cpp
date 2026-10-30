#include "test_framework.hpp"
import std;
import cnetmod.protocol.clickhouse;
namespace ch = cnetmod::clickhouse;

static auto unhex(std::string_view input) -> std::vector<std::byte>
{
    std::vector<std::byte> result;
    for (std::size_t i{}; i < input.size(); i += 2)
    {
        unsigned value{};
        std::from_chars(input.data() + i, input.data() + i + 2, value, 16);
        result.push_back(std::byte(value));
    }
    return result;
}

TEST(native_compression_official_cityhash_fixtures)
{
    // Generated independently with official ClickHouse 26.11.1.899 compressor.
    constexpr std::string_view payload = "cnetmod Native compression\n";
    const auto raw = std::as_bytes(std::span{payload.data(), payload.size()});
    const std::array fixtures{
        unhex("098959f5e9a110239859855288c0222582260000001b000000f00c636e65746d6f64204e617469766520636f6d7072657373696f6e0a"),
        unhex("671e57fdd0c879577128596dd685494f02240000001b000000636e65746d6f64204e617469766520636f6d7072657373696f6e0a"),
        unhex("a7deb52a8d8f79d6633ee93619c7ea46902d0000001b00000028b52ffd201bd90000636e65746d6f64204e617469766520636f6d7072657373696f6e0a")};
    const std::array modes{ch::compression_mode::lz4, ch::compression_mode::checksum, ch::compression_mode::zstd};
    for (std::size_t index{}; index < fixtures.size(); ++index)
    {
        auto decoded = ch::wire::decompress_frame(fixtures[index]);
        ASSERT_TRUE(decoded.has_value());
        ASSERT_TRUE(std::ranges::equal(decoded->data, raw));
        ASSERT_EQ(decoded->consumed, fixtures[index].size());
        auto encoded = ch::wire::compress_frame(raw, modes[index]);
        ASSERT_TRUE(encoded.has_value());
        ASSERT_TRUE(*encoded == fixtures[index]);
        for (std::size_t prefix{}; prefix < fixtures[index].size(); ++prefix)
        {
            auto truncated = ch::wire::decompress_frame(std::span{fixtures[index]}.first(prefix));
            ASSERT_FALSE(truncated.has_value());
        }
        auto changed = fixtures[index];
        changed.back() ^= std::byte{1};
        auto corrupted = ch::wire::decompress_frame(changed);
        ASSERT_FALSE(corrupted.has_value());
        ASSERT_TRUE(corrupted.error().message.find("checksum") != std::string::npos);
        ch::limits small;
        small.max_packet_bytes = 8;
        ASSERT_FALSE(ch::wire::decompress_frame(fixtures[index], small).has_value());
    }
}

TEST(native_compression_large_frames_and_dimension_limits)
{
    std::vector<std::byte> input(200000);
    for (std::size_t i{}; i < input.size(); ++i)
        input[i] = std::byte((i * 17 + i / 13) % 256);
    for (auto mode : {ch::compression_mode::checksum, ch::compression_mode::lz4, ch::compression_mode::zstd})
    {
        auto encoded = ch::wire::compress_frame(input, mode);
        ASSERT_TRUE(encoded.has_value());
        auto decoded = ch::wire::decompress_frame(*encoded);
        ASSERT_TRUE(decoded.has_value());
        ASSERT_TRUE(decoded->data == input);
        auto bomb = *encoded;
        for (std::size_t i = 21; i < 25; ++i)
            bomb[i] = std::byte{255};
        ASSERT_FALSE(ch::wire::decompress_frame(bomb).has_value());
        auto short_size = *encoded;
        short_size[17] = std::byte{8};
        for (std::size_t i = 18; i < 21; ++i)
            short_size[i] = std::byte{0};
        ASSERT_FALSE(ch::wire::decompress_frame(short_size).has_value());
    }
}
RUN_TESTS()
