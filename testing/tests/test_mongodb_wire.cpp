#include "test_framework.hpp"
#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.protocol.mongodb;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;

using namespace cnetmod::mongodb;

TEST(mongodb_op_msg_decodes_more_to_come_response_flag)
{
    bson_document reply{{"ok", bson_value{1.0}}, {"cursor", bson_value{std::int64_t{0}}}};
    auto wire = encode_command_message(17, reply, 1024U * 1024U);
    ASSERT_TRUE(wire.has_value());
    // OP_MSG flags immediately follow the 16-byte message header. A response
    // with moreToCome has the same body as a one-shot reply, but clients must
    // keep reading it through command_stream rather than reuse the socket.
    ASSERT_TRUE(wire->size() > 20U);
    (*wire)[16] = std::byte{op_message_more_to_come};
    (*wire)[17] = std::byte{0};
    (*wire)[18] = std::byte{0};
    (*wire)[19] = std::byte{0};

    auto decoded = decode_command_message(*wire, 1024U * 1024U);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE((decoded->flags & op_message_more_to_come) != 0U);
    ASSERT_TRUE(decoded->body.contains("ok"));
}

TEST(mongodb_op_msg_rejects_more_to_come_as_a_request_flag)
{
    bson_document command{{"find", bson_value{"users"}}};
    auto encoded = encode_command_message(1, command, 1024U * 1024U,
        op_message_more_to_come);
    ASSERT_FALSE(encoded.has_value());
}

TEST(mongodb_op_msg_allows_exhaust_capability_on_requests)
{
    bson_document command{{"find", bson_value{"users"}}};
    auto encoded = encode_command_message(1, command, 1024U * 1024U,
        op_message_exhaust_allowed);
    ASSERT_TRUE(encoded.has_value());
}

namespace {
void wire_int32(std::vector<std::byte>& wire, std::size_t offset, std::uint32_t value)
{
    for (std::size_t byte{}; byte < 4; ++byte)
        wire[offset + byte] = static_cast<std::byte>((value >> (byte * 8)) & 0xffU);
}
} // namespace

TEST(mongodb_op_msg_ignores_unknown_optional_bits_but_rejects_required_bits)
{
    auto wire = encode_command_message(7, bson_document{{"ping", std::int32_t{1}}}, 1024);
    ASSERT_TRUE(wire.has_value());
    for (unsigned bit = 16; bit < 32; ++bit)
    {
        wire_int32(*wire, 16, std::uint32_t{1} << bit);
        const auto decoded = decode_command_message(*wire, 1024);
        ASSERT_TRUE(decoded.has_value());
        ASSERT_EQ(decoded->flags, std::uint32_t{1} << bit);
        ASSERT_TRUE(decoded->body.contains("ping"));
    }
    for (unsigned bit = 2; bit < 16; ++bit)
    {
        wire_int32(*wire, 16, std::uint32_t{1} << bit);
        const auto decoded = decode_command_message(*wire, 1024);
        ASSERT_FALSE(decoded.has_value());
        ASSERT_TRUE(decoded.error().code == error_code::protocol_error);
    }
}

TEST(mongodb_op_msg_checksum_matches_independent_castagnoli_fixture)
{
    const bson_document ping{{"ping", std::int32_t{1}}};
    auto wire = encode_command_message(7, ping, 1024, op_message_checksum_present);
    ASSERT_TRUE(wire.has_value());
    ASSERT_EQ(wire->size(), 40U);
    // CRC-32C of the complete 36-byte header/body fixture, excluding the CRC.
    const std::array<std::byte, 4> expected{
        std::byte{0x17}, std::byte{0x72}, std::byte{0x7c}, std::byte{0xf4}};
    ASSERT_TRUE(std::equal(expected.begin(), expected.end(), wire->end() - 4));
    ASSERT_TRUE(decode_command_message(*wire, 1024).has_value());
    ASSERT_TRUE(encode_command_message(7, ping, 40, op_message_checksum_present).has_value());
    ASSERT_FALSE(encode_command_message(7, ping, 39, op_message_checksum_present).has_value());
    ASSERT_TRUE(encode_command_message(7, ping, 36).has_value());
    for (const auto offset : {4U, 16U, 31U, 39U})
    {
        auto corrupt = *wire;
        corrupt[offset] ^= std::byte{0x80};
        ASSERT_FALSE(decode_command_message(corrupt, 1024).has_value());
    }
    wire->pop_back();
    wire_int32(*wire, 0, static_cast<std::uint32_t>(wire->size()));
    ASSERT_FALSE(decode_command_message(*wire, 1024).has_value());
}

TEST(mongodb_op_compressed_validates_source_header_and_size_limits)
{
    auto wire = encode_command_message(7, bson_document{{"ping", std::int32_t{1}}}, 1024);
    ASSERT_TRUE(wire.has_value());
    auto compressed = encode_compressed_message(*wire, compressor_noop, 1024);
    ASSERT_TRUE(compressed.has_value());
    auto expanded = decode_compressed_message(*compressed, 1024);
    ASSERT_TRUE(expanded.has_value());
    ASSERT_TRUE(*expanded == *wire);
    auto malformed = *wire;
    wire_int32(malformed, 0, static_cast<std::uint32_t>(wire->size() + 1));
    ASSERT_FALSE(encode_compressed_message(malformed, compressor_noop, 1024).has_value());
    malformed = *compressed;
    wire_int32(malformed, 20, 0x7fffffffU);
    ASSERT_FALSE(decode_compressed_message(malformed, std::numeric_limits<std::size_t>::max()).has_value());
    ASSERT_FALSE(decode_compressed_message(*compressed, wire->size() - 1).has_value());
    ASSERT_FALSE(encode_compressed_message(*compressed, compressor_noop, 1024).has_value());
}

TEST(mongodb_op_compressed_zlib_rejects_trailing_bytes)
{
    auto wire = encode_command_message(7, bson_document{{"ping", std::int32_t{1}}}, 1024,
        op_message_checksum_present);
    ASSERT_TRUE(wire.has_value());
    auto compressed = encode_compressed_message(*wire, compressor_zlib, 1024);
#ifndef CNETMOD_HAS_ZLIB
    ASSERT_FALSE(compressed.has_value());
    ASSERT_TRUE(compressed.error().code == error_code::compression_failed);
    return;
#else
    ASSERT_TRUE(compressed.has_value());
    auto expanded = decode_compressed_message(*compressed, 1024);
    ASSERT_TRUE(expanded.has_value());
    ASSERT_TRUE(*expanded == *wire);
    ASSERT_TRUE(decode_command_message(*expanded, 1024).has_value());
    compressed->push_back(std::byte{0x7f});
    wire_int32(*compressed, 0, static_cast<std::uint32_t>(compressed->size()));
    ASSERT_FALSE(decode_compressed_message(*compressed, 1024).has_value());
#endif
}

TEST(mongodb_document_sequences_round_trip_and_preserve_nested_body)
{
    const bson_document body{{"cursor", bson_document{{"id", std::int64_t{7}}}}};
    const std::array<document_sequence, 1> sequences{{{"cursor.firstBatch", {bson_document{{"value", std::int32_t{1}}}, bson_document{{"value", std::int32_t{2}}}}}}};
    auto wire = encode_command_message(1, body, sequences, 1024, op_message_checksum_present,
        bson_limits{.max_document_bytes = 40});
    ASSERT_TRUE(wire.has_value());
    ASSERT_TRUE(wire->size() > 40);
    auto decoded = decode_command_message(*wire, 1024, bson_limits{.max_document_bytes = 40});
    ASSERT_TRUE(decoded.has_value());
    const auto cursor = decoded->body.find("cursor")->as_document();
    ASSERT_EQ(*cursor->find("id")->get_if<std::int64_t>(), 7);
    ASSERT_EQ(cursor->find("firstBatch")->as_array()->size(), 2U);
    ASSERT_FALSE(body.find("cursor")->as_document()->contains("firstBatch"));
    ASSERT_FALSE(encode_command_message(1, body, sequences, wire->size() - 1,
        op_message_checksum_present)
            .has_value());
}

TEST(mongodb_document_sequences_can_precede_body)
{
    const bson_document body{{"insert", "events"}};
    const std::array<document_sequence, 1> sequences{{{"documents", {bson_document{{"x", 1}}}}}};
    auto wire = encode_command_message(1, body, sequences, 1024);
    ASSERT_TRUE(wire.has_value());
    auto bson = encode_bson_document(body);
    ASSERT_TRUE(bson.has_value());
    std::rotate(wire->begin() + 20, wire->begin() + 21 + bson->size(), wire->end());
    const auto decoded = decode_command_message(*wire, 1024);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->body.find("documents")->as_array()->size(), 1U);
}

TEST(mongodb_document_sequences_reject_collisions_and_malformed_sections)
{
    const bson_document body{{"insert", "events"}};
    for (const auto& path : {"", ".documents", "documents.", "parent..documents", "insert"})
    {
        const std::array<document_sequence, 1> sequences{{{path, {}}}};
        ASSERT_FALSE(encode_command_message(1, body, sequences, 1024).has_value());
    }
    const std::array<document_sequence, 2> duplicate{{{"documents", {}}, {"documents", {}}}};
    ASSERT_FALSE(encode_command_message(1, body, duplicate, 1024).has_value());
    const std::array<document_sequence, 1> sequences{{{"documents", {bson_document{{"x", 1}}}}}};
    auto wire = encode_command_message(1, body, sequences, 1024);
    ASSERT_TRUE(wire.has_value());
    const auto bson = encode_bson_document(body);
    const auto section = 21 + bson->size();
    for (const auto size : {0U, 4U, 0x7fffffffU})
    {
        auto malformed = *wire;
        wire_int32(malformed, section + 1, size);
        ASSERT_FALSE(decode_command_message(malformed, 1024).has_value());
    }
    auto malformed = *wire;
    malformed[section] = std::byte{42};
    ASSERT_FALSE(decode_command_message(malformed, 1024).has_value());
    malformed = *wire;
    malformed.insert(malformed.end(), wire->begin() + 20, wire->begin() + section);
    wire_int32(malformed, 0, static_cast<std::uint32_t>(malformed.size()));
    ASSERT_FALSE(decode_command_message(malformed, 1024).has_value());
    // The body is mandatory even when sequences are structurally valid.
    malformed = *wire;
    malformed.erase(malformed.begin() + 20, malformed.begin() + section);
    wire_int32(malformed, 0, static_cast<std::uint32_t>(malformed.size()));
    ASSERT_FALSE(decode_command_message(malformed, 1024).has_value());
}

TEST(mongodb_zstd_round_trip_and_frame_boundary_validation)
{
    auto wire = encode_command_message(7, bson_document{{"ping", 1}}, 1024);
    ASSERT_TRUE(wire.has_value());
    auto compressed = encode_compressed_message(*wire, compressor_zstd, 1024);
#ifdef CNETMOD_HAS_ZSTD
    ASSERT_TRUE(compressed.has_value());
    auto decoded = decode_compressed_message(*compressed, 1024);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(*decoded == *wire);
    compressed->push_back(std::byte{1});
    wire_int32(*compressed, 0, static_cast<std::uint32_t>(compressed->size()));
    ASSERT_FALSE(decode_compressed_message(*compressed, 1024).has_value());
#else
    ASSERT_FALSE(compressed.has_value());
#endif
}

TEST(mongodb_snappy_uses_raw_block_format_and_rejects_bad_lengths)
{
    std::vector<std::byte> raw(21);
    wire_int32(raw, 0, 21);
    wire_int32(raw, 12, op_message);
    constexpr std::string_view hello{"hello"};
    for (std::size_t i{}; i < hello.size(); ++i)
        raw[16 + i] = static_cast<std::byte>(hello[i]);
    auto encoded = encode_compressed_message(raw, compressor_snappy, 1024);
#ifdef CNETMOD_HAS_SNAPPY
    ASSERT_TRUE(encoded.has_value());
    // Independent raw Snappy fixture: varint 5, literal tag (5-1)<<2, "hello".
    const std::array<std::byte, 7> fixture{std::byte{5}, std::byte{16},
        std::byte{'h'}, std::byte{'e'}, std::byte{'l'}, std::byte{'l'}, std::byte{'o'}};
    ASSERT_EQ(encoded->size(), 32U);
    ASSERT_TRUE(std::equal(fixture.begin(), fixture.end(), encoded->begin() + 25));
    auto decoded = decode_compressed_message(*encoded, 1024);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(*decoded == raw);
    auto incorrect = *encoded;
    wire_int32(incorrect, 20, 4);
    ASSERT_FALSE(decode_compressed_message(incorrect, 1024).has_value());
    encoded->push_back(std::byte{});
    wire_int32(*encoded, 0, static_cast<std::uint32_t>(encoded->size()));
    ASSERT_FALSE(decode_compressed_message(*encoded, 1024).has_value());
#else
    ASSERT_FALSE(encoded.has_value());
#endif
}
RUN_TESTS()
