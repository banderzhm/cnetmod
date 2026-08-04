#pragma once

#include <quic/varint.hpp>
#include <testing/test_framework.hpp>

#include <array>
#include <random>

TEST(quic_varint_encode_zero)
{
    auto result = cnetmod::quic::encode_varint(0);
    ASSERT_TRUE(result.has_value());

    std::array<std::byte, 8> encoded{};
    std::span<std::byte> buf{encoded.data(), result->second};

    ASSERT_EQ(result->second, 1);
    ASSERT_EQ(std::to_integer<std::uint8_t>(buf[0]), 0x00);
}

TEST(quic_varint_encode_1byte_range)
{
    // Test values at boundaries of 1-byte range (0-63)
    std::vector<std::uint64_t> test_values = {0, 1, 32, 63};

    for (auto val : test_values)
    {
        auto encode_result = cnetmod::quic::encode_varint(val);
        ASSERT_TRUE(encode_result.has_value()) << "Failed to encode value: " << val;

        ASSERT_EQ(encode_result->second, 1) << "Value " << val << " should be 1 byte";

        // Verify round-trip decode
        std::array<std::byte, 8> buffer{};
        std::span<std::byte> span{buffer.data(), encode_result->second};
        for (std::size_t i = 0; i < encode_result->second; ++i)
        {
            buffer[i] = encode_result->value_or(std::array<std::byte, 8>{})[i];
        }

        auto decode_result = cnetmod::quic::decode_varint(span);
        ASSERT_TRUE(decode_result.has_value());
        ASSERT_EQ(decode_result->first, val);
    }
}

TEST(quic_varint_encode_2byte_range)
{
    // Test values in 2-byte range (64-16383)
    std::vector<std::uint64_t> test_values = {64, 127, 16383};

    for (auto val : test_values)
    {
        auto encode_result = cnetmod::quic::encode_varint(val);
        ASSERT_TRUE(encode_result.has_value()) << "Failed to encode value: " << val;

        ASSERT_EQ(encode_result->second, 2) << "Value " << val << " should be 2 bytes";

        // Verify first byte has correct prefix (0x40 | high_bits)
        ASSERT_EQ((std::to_integer<std::uint8_t>(encode_result->value_or({})[0]) & 0xC0), 0x40);

        // Round-trip test
        std::array<std::byte, 8> buffer{};
        for (std::size_t i = 0; i < encode_result->second; ++i)
        {
            buffer[i] = encode_result->value_or({})[i];
        }

        auto decode_result = cnetmod::quic::decode_varint(std::span{buffer.data(), encode_result->second});
        ASSERT_TRUE(decode_result.has_value());
        ASSERT_EQ(decode_result->first, val);
    }
}

TEST(quic_varint_encode_4byte_range)
{
    // Test values in 4-byte range (16384-1073741823)
    std::vector<std::uint64_t> test_values = {
        16384,     // Minimum 4-byte value
        65535,     // Maximum uint16_t
        1000000,   // Common large value
        1073741823 // Maximum 4-byte varint value (2^30 - 1)
    };

    for (auto val : test_values)
    {
        auto encode_result = cnetmod::quic::encode_varint(val);
        ASSERT_TRUE(encode_result.has_value()) << "Failed to encode value: " << val;

        ASSERT_EQ(encode_result->second, 4) << "Value " << val << " should be 4 bytes";

        // Verify prefix
        ASSERT_EQ((std::to_integer<std::uint8_t>(encode_result->value_or({})[0]) & 0xC0), 0x80);

        // Round-trip test
        std::array<std::byte, 8> buffer{};
        for (std::size_t i = 0; i < encode_result->second; ++i)
        {
            buffer[i] = encode_result->value_or({})[i];
        }

        auto decode_result = cnetmod::quic::decode_varint(std::span{buffer.data(), encode_result->second});
        ASSERT_TRUE(decode_result.has_value());
        ASSERT_EQ(decode_result->first, val);
    }
}

TEST(quic_varint_encode_max_valid)
{
    // Test maximum valid varint value (2^62 - 1 = 4398046511103)
    const std::uint64_t max_val = 4398046511103ULL;

    auto encode_result = cnetmod::quic::encode_varint(max_val);
    ASSERT_TRUE(encode_result.has_value());

    ASSERT_EQ(encode_result->second, 8) << "Max value should use all 8 bytes";

    // Verify prefix is 0xC0
    ASSERT_EQ((std::to_integer<std::uint8_t>(encode_result->value_or({})[0]) & 0xC0), 0xC0);

    // Round-trip test
    std::array<std::byte, 8> buffer{};
    for (std::size_t i = 0; i < encode_result->second; ++i)
    {
        buffer[i] = encode_result->value_or({})[i];
    }

    auto decode_result = cnetmod::quic::decode_varint(std::span{buffer.data(), 8});
    ASSERT_TRUE(decode_result.has_value());
    ASSERT_EQ(decode_result->first, max_val);
}

TEST(quic_varint_encode_exceeds_62bit)
{
    // Values >= 2^62 should fail
    const std::uint64_t exceeds_min = 4398046511104ULL; // 2^62
    const std::uint64_t u64_max = UINT64_MAX;

    // Test minimum exceeding value
    auto result1 = cnetmod::quic::encode_varint(exceeds_min);
    ASSERT_FALSE(result1.has_value());

    // Test UINT64_MAX
    auto result2 = cnetmod::quic::encode_varint(u64_max);
    ASSERT_FALSE(result2.has_value());
}

TEST(quic_varint_decode_partial_input)
{
    // Test decoding truncated varints

    // 1-byte varint with only partial data
    std::array<std::byte, 2> partial_2byte{{static_cast<std::byte>(0x40), 0x00}};
    auto result1 = cnetmod::quic::decode_varint(std::span{partial_2byte.data(), 1});
    ASSERT_FALSE(result1.has_value());

    // 4-byte varint with only 2 bytes
    std::array<std::byte, 4> partial_4byte{{static_cast<std::byte>(0x80), 0xFF, 0xFF, 0x00}};
    auto result2 = cnetmod::quic::decode_varint(std::span{partial_4byte.data(), 2});
    ASSERT_FALSE(result2.has_value());

    // Empty input
    std::array<std::byte, 1> empty{};
    auto result3 = cnetmod::quic::decode_varint(std::span<std::byte>{});
    ASSERT_FALSE(result3.has_value());
}

TEST(quic_varint_roundtrip_random)
{
    // Test random values up to 2^62-1
    std::random_device rd;
    std::mt19937_64 gen(rd());

    // Distribution for each range type
    std::uniform_int_distribution<std::uint64_t> dist_1byte(0, 63);
    std::uniform_int_distribution<std::uint64_t> dist_2byte(64, 16383);
    std::uniform_int_distribution<std::uint64_t> dist_4byte(16384, 1073741823ULL);
    std::uniform_int_distribution<std::uint64_t> dist_8byte(1073741824ULL, 4398046511103ULL);

    std::uniform_int_distribution<int> dist_type(0, 3);

    int passed = 0;
    int failed = 0;

    for (int i = 0; i < 100; ++i)
    {
        auto type = dist_type(gen);
        std::uint64_t val;

        switch (type)
        {
        case 0:
            val = dist_1byte(gen);
            break;
        case 1:
            val = dist_2byte(gen);
            break;
        case 2:
            val = dist_4byte(gen);
            break;
        default:
            val = dist_8byte(gen);
            break;
        }

        auto encode_result = cnetmod::quic::encode_varint(val);
        if (!encode_result.has_value())
        {
            ++failed;
            continue;
        }

        std::array<std::byte, 8> buffer{};
        for (std::size_t j = 0; j < encode_result->second; ++j)
        {
            buffer[j] = encode_result->value_or({})[j];
        }

        auto decode_result = cnetmod::quic::decode_varint(std::span{buffer.data(), encode_result->second});

        if (decode_result.has_value() && decode_result->first == val)
        {
            ++passed;
        }
        else
        {
            ++failed;
        }
    }

    std::cout << "Random round-trip tests: " << passed << " passed, " << failed << " failed\n";
    ASSERT_EQ(failed, 0);
}

TEST(quic_varint_all_boundary_values)
{
    // Comprehensive boundary test: 0, 63, 64, 16383, 16384, max 4-byte, min 8-byte, max value
    struct test_case
    {
        std::uint64_t value;
        std::size_t expected_size;
    };

    test_case cases[] = {
        {0, 1},             // 1-byte encoding
        {63, 1},            // max 1-byte
        {64, 2},            // min 2-byte
        {16383, 2},         // max 2-byte
        {16384, 4},         // min 4-byte
        {1073741823, 4},    // max 4-byte (2^30 - 1)
        {1073741824, 8},    // min 8-byte (2^30)
        {4398046511103, 8}, // max value (2^62 - 1)
    };

    for (const auto& tc : cases)
    {
        auto result = cnetmod::quic::encode_varint(tc.value);
        ASSERT_TRUE(result.has_value()) << "Failed to encode value: " << tc.value;
        ASSERT_EQ(result->second, tc.expected_size) << "Wrong size for value: " << tc.value;

        // Round-trip decode
        auto& arr = result->first;
        std::span<const std::byte> encoded_span{arr.data(), result->second};
        auto decode_result = cnetmod::quic::decode_varint(encoded_span);
        ASSERT_TRUE(decode_result.has_value()) << "Failed to decode value: " << tc.value;
        ASSERT_EQ(decode_result->first, tc.value) << "Round-trip mismatch for value: " << tc.value;
        ASSERT_EQ(decode_result->second, tc.expected_size);
    }
}

TEST(quic_varint_decode_truncated_2byte_prefix)
{
    // 2-byte prefix (0x40) but only 1 byte available → should fail
    std::array<std::byte, 1> truncated{{static_cast<std::byte>(0x40)}};
    auto result = cnetmod::quic::decode_varint(std::span{truncated.data(), 1});
    ASSERT_FALSE(result.has_value());
}

TEST(quic_varint_decode_truncated_4byte_prefix)
{
    // 4-byte prefix (0x80) but only 2 bytes available → should fail
    std::array<std::byte, 2> truncated{{static_cast<std::byte>(0x80), static_cast<std::byte>(0xFF)}};
    auto result = cnetmod::quic::decode_varint(std::span{truncated.data(), 2});
    ASSERT_FALSE(result.has_value());
}

TEST(quic_varint_decode_truncated_8byte_prefix)
{
    // 8-byte prefix (0xC0) but only 4 bytes available → should fail
    std::array<std::byte, 4> truncated{{static_cast<std::byte>(0xC0), static_cast<std::byte>(0x00),
        static_cast<std::byte>(0x00), static_cast<std::byte>(0x00)}};
    auto result = cnetmod::quic::decode_varint(std::span{truncated.data(), 4});
    ASSERT_FALSE(result.has_value());
}

TEST(quic_varint_encode_overflow_error)
{
    // 2^62 should fail with value_too_large
    auto result = cnetmod::quic::encode_varint(4398046511104ULL); // 2^62
    ASSERT_FALSE(result.has_value());

    // Verify error code
    ASSERT_TRUE(result.error() == std::errc::value_too_large);
}

TEST(quic_varint_encode_to_buffer_success)
{
    // Test encode_varint_to with sufficient buffer
    std::array<std::byte, 8> buffer{};

    auto result = cnetmod::quic::encode_varint_to(16383, std::span{buffer.data(), buffer.size()});
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, 2); // 16383 uses 2 bytes

    // Verify round-trip
    auto decode_result = cnetmod::quic::decode_varint(std::span{buffer.data(), *result});
    ASSERT_TRUE(decode_result.has_value());
    ASSERT_EQ(decode_result->first, 16383ULL);
}

TEST(quic_varint_encode_to_buffer_too_small)
{
    // Buffer too small for the value
    std::array<std::byte, 1> small_buffer{};

    // 64 requires 2 bytes but buffer is only 1 byte
    auto result = cnetmod::quic::encode_varint_to(64, std::span{small_buffer.data(), 1});
    ASSERT_FALSE(result.has_value());
}

TEST(quic_varint_size_function)
{
    // Test varint_size helper
    ASSERT_EQ(cnetmod::quic::varint_size(0), 1);
    ASSERT_EQ(cnetmod::quic::varint_size(63), 1);
    ASSERT_EQ(cnetmod::quic::varint_size(64), 2);
    ASSERT_EQ(cnetmod::quic::varint_size(16383), 2);
    ASSERT_EQ(cnetmod::quic::varint_size(16384), 4);
    ASSERT_EQ(cnetmod::quic::varint_size(1073741823), 4);
    ASSERT_EQ(cnetmod::quic::varint_size(1073741824), 8);
    ASSERT_EQ(cnetmod::quic::varint_size(4398046511103ULL), 8);
}

RUN_TESTS();
