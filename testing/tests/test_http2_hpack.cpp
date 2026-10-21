/// HPACK RFC 7541 regression tests.

#include "test_framework.hpp"

import std;
import cnetmod.protocol.http.v2.header_compression;
import cnetmod.protocol.http.v2.huffman;

using namespace cnetmod::http::v2;

TEST(hpack_huffman_rfc_example)
{
    // RFC 7541 Appendix C.4.1: "www.example.com" Huffman coding.
    constexpr std::array encoded{
        std::byte{0xf1},
        std::byte{0xe3},
        std::byte{0xc2},
        std::byte{0xe5},
        std::byte{0xf2},
        std::byte{0x3a},
        std::byte{0x6b},
        std::byte{0xa0},
        std::byte{0xab},
        std::byte{0x90},
        std::byte{0xf4},
        std::byte{0xff},
    };
    auto decoded = huffman_decode(encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(*decoded, std::string("www.example.com"));
}

TEST(hpack_huffman_all_byte_symbols_roundtrip)
{
    std::string input;
    input.reserve(256U);
    for (unsigned value{}; value <= 0xffU; ++value)
        input.push_back(static_cast<char>(value));

    const auto encoded = huffman_encode(input);
    const auto decoded = huffman_decode(encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(*decoded, input);
}

TEST(hpack_dynamic_table_and_huffman_roundtrip)
{
    header_compression encoder;
    header_compression decoder;
    const std::array fields{
        header_field{":method", "GET"},
        header_field{":scheme", "https"},
        header_field{":path", "/resource/with/a/long/value"},
        header_field{"host", "www.example.com"},
        header_field{"x-cnetmod-test", "dynamic table survives requests"},
    };
    auto first = encoder.encode(fields);
    ASSERT_TRUE(first.has_value());
    auto first_decoded = decoder.decode(*first);
    ASSERT_TRUE(first_decoded.has_value());
    ASSERT_EQ(first_decoded->size(), fields.size());
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        ASSERT_EQ((*first_decoded)[i].name, fields[i].name);
        ASSERT_EQ((*first_decoded)[i].value, fields[i].value);
    }
    auto second = encoder.encode(fields);
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(second->size() < first->size());
    auto second_decoded = decoder.decode(*second);
    ASSERT_TRUE(second_decoded.has_value());
    ASSERT_EQ(second_decoded->size(), fields.size());
}

TEST(hpack_rejects_invalid_huffman_padding)
{
    constexpr std::array malformed{std::byte{0xff}};
    ASSERT_FALSE(huffman_decode(malformed).has_value());
}

TEST(hpack_repeated_stateless_blocks_own_their_decoded_fields)
{
    header_compression decoder;
    std::vector<std::byte> block{std::byte{0}, std::byte{1}, std::byte{'x'},
        std::byte{3}, std::byte{'o'}, std::byte{'n'}, std::byte{'e'}};
    std::vector<header_field> output;
    ASSERT_TRUE(decoder.decode(block, output).has_value());
    ASSERT_EQ(output.front().value, std::string("one"));
    output.front().name = "changed by caller";
    output.front().value = "changed by caller";
    ASSERT_TRUE(decoder.decode(block, output).has_value());
    ASSERT_EQ(output.front().name, std::string("x"));
    ASSERT_EQ(output.front().value, std::string("one"));
    block[4] = std::byte{'t'};
    block[5] = std::byte{'w'};
    block[6] = std::byte{'o'};
    ASSERT_TRUE(decoder.decode(block, output).has_value());
    ASSERT_EQ(output.front().value, std::string("two"));
    block.push_back(std::byte{0xff}); // Not an equal block: truncated index.
    ASSERT_FALSE(decoder.decode(block, output).has_value());
}

TEST(hpack_repeated_blocks_preserve_dynamic_table_operations)
{
    header_compression decoder;
    const std::array one{std::byte{0x40}, std::byte{1}, std::byte{'x'},
        std::byte{3}, std::byte{'o'}, std::byte{'n'}, std::byte{'e'}};
    const std::array two{std::byte{0x40}, std::byte{1}, std::byte{'x'},
        std::byte{3}, std::byte{'t'}, std::byte{'w'}, std::byte{'o'}};
    const std::array newest{std::byte{0xbe}}; // First dynamic index: 62.
    const std::array third{std::byte{0xc0}};
    ASSERT_TRUE(decoder.decode(one).has_value());
    ASSERT_TRUE(decoder.decode(one).has_value()); // Must insert a second entry.
    ASSERT_EQ(decoder.decode(newest)->front().value, std::string("one"));
    ASSERT_EQ(decoder.decode(newest)->front().value, std::string("one"));
    ASSERT_TRUE(decoder.decode(two).has_value()); // Invalidates index memo.
    ASSERT_EQ(decoder.decode(newest)->front().value, std::string("two"));
    auto older = decoder.decode(third);
    ASSERT_TRUE(older.has_value());
    ASSERT_EQ(older->front().value, std::string("one"));
    decoder.set_dynamic_table_limit(0);
    ASSERT_FALSE(decoder.decode(newest).has_value());
    const std::array resize{std::byte{0x20}, std::byte{0x82}};
    ASSERT_TRUE(decoder.decode(resize).has_value());
    ASSERT_TRUE(decoder.decode(resize).has_value());
    ASSERT_FALSE(decoder.decode(newest).has_value());
}

TEST(hpack_shared_block_does_not_shift_peer_dynamic_indices)
{
    // The static-file fast path replays one cached block on many connections.
    // With no dynamic-table capacity the block must use literal-without-
    // indexing, so replaying it never shifts the peer's table.
    header_compression shared{0};
    const std::array static_fields{
        header_field{":status", "200"},
        header_field{"content-length", "4013"},
        header_field{"content-type", "text/html; charset=utf-8"},
        header_field{"accept-ranges", "bytes"},
        header_field{"vary", "Accept-Encoding"}};
    auto shared_block = shared.encode(static_fields);
    ASSERT_TRUE(shared_block.has_value());
    auto repeated = shared.encode(static_fields);
    ASSERT_TRUE(repeated.has_value());
    ASSERT_TRUE(*repeated == *shared_block);

    header_compression encoder;
    header_compression decoder;
    auto replayed = decoder.decode(*shared_block);
    ASSERT_TRUE(replayed.has_value());
    ASSERT_EQ(replayed->size(), static_fields.size());
    for (std::size_t i = 0; i < static_fields.size(); ++i)
    {
        ASSERT_EQ((*replayed)[i].name, static_fields[i].name);
        ASSERT_EQ((*replayed)[i].value, static_fields[i].value);
    }

    const std::array dynamic_fields{
        header_field{"x-cnetmod-test", "shared block must not insert"}};
    auto first = encoder.encode(dynamic_fields);
    ASSERT_TRUE(first.has_value());
    auto first_decoded = decoder.decode(*first);
    ASSERT_TRUE(first_decoded.has_value());
    ASSERT_EQ(first_decoded->front().value,
        std::string("shared block must not insert"));
    // The second block references its own dynamic entry by index. A spurious
    // insertion from the shared block would shift that index.
    auto second = encoder.encode(dynamic_fields);
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(second->size() < first->size());
    auto second_decoded = decoder.decode(*second);
    ASSERT_TRUE(second_decoded.has_value());
    ASSERT_EQ(second_decoded->front().value,
        std::string("shared block must not insert"));
}

RUN_TESTS()
