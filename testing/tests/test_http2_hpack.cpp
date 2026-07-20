/// HPACK RFC 7541 regression tests.

#include "test_framework.hpp"

import std;
import cnetmod.protocol.http.v2.header_compression;
import cnetmod.protocol.http.v2.huffman;

using namespace cnetmod::http::v2;

TEST(hpack_huffman_rfc_example) {
    // RFC 7541 Appendix C.4.1: "www.example.com" Huffman coding.
    constexpr std::array encoded{
        std::byte{0xf1}, std::byte{0xe3}, std::byte{0xc2}, std::byte{0xe5},
        std::byte{0xf2}, std::byte{0x3a}, std::byte{0x6b}, std::byte{0xa0},
        std::byte{0xab}, std::byte{0x90}, std::byte{0xf4}, std::byte{0xff},
    };
    auto decoded = huffman_decode(encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(*decoded, std::string("www.example.com"));
}

TEST(hpack_dynamic_table_and_huffman_roundtrip) {
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
    for (std::size_t i = 0; i < fields.size(); ++i) {
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

TEST(hpack_rejects_invalid_huffman_padding) {
    constexpr std::array malformed{std::byte{0xff}};
    ASSERT_FALSE(huffman_decode(malformed).has_value());
}

RUN_TESTS()
