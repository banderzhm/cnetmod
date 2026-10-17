#include "test_framework.hpp"

import cnetmod.protocol.http.v3.qpack;
import std;

namespace {
using cnetmod::http::v3::header_field;
using cnetmod::http::v3::qpack_decoder;
using cnetmod::http::v3::qpack_encoder;
using cnetmod::http::v3::qpack_static_response_headers;

auto fields(std::initializer_list<header_field> values) -> std::vector<header_field>
{
    return {values};
}

auto as_span(const std::vector<std::byte>& bytes) -> std::span<const std::byte>
{
    return bytes;
}
} // namespace

TEST(qpack_static_table_is_rfc9204_appendix_a)
{
    qpack_decoder decoder;

    const auto method = decoder.lookup_by_name_value(":method", "GET");
    const auto status = decoder.lookup_by_name_value(":status", "200");
    const auto content_type = decoder.lookup_by_name_value("content-type", "application/json");

    ASSERT_TRUE(method.has_value());
    ASSERT_EQ(*method, 17U);
    ASSERT_TRUE(status.has_value());
    ASSERT_EQ(*status, 25U);
    ASSERT_TRUE(content_type.has_value());
    ASSERT_EQ(*content_type, 46U);
}

TEST(qpack_static_and_literal_header_block_round_trip)
{
    qpack_encoder encoder;
    qpack_decoder decoder;
    const auto input = fields({{":method", "GET"}, {"x-cnetmod-test", "plain"}});

    const auto encoded = encoder.encode(input, 4);
    ASSERT_TRUE(encoded.has_value());
    const auto decoded = decoder.decode(*encoded, 4);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), input.size());
    ASSERT_EQ((*decoded)[0].name, input[0].name);
    ASSERT_EQ((*decoded)[0].value, input[0].value);
    ASSERT_EQ((*decoded)[1].name, input[1].name);
    ASSERT_EQ((*decoded)[1].value, input[1].value);
}

TEST(qpack_dynamic_indexed_header_block_ric_base_and_ack)
{
    qpack_encoder encoder{1024};
    qpack_decoder decoder{1024};
    const auto input = fields({{"x-cnetmod-dynamic", "first-value"}, {"x-cnetmod-dynamic", "first-value"}});

    const auto encoded = encoder.encode(input, 8);
    ASSERT_TRUE(encoded.has_value());
    const auto instructions = encoder.take_encoder_instructions();
    ASSERT_FALSE(instructions.empty());
    ASSERT_TRUE(decoder.process_encoder_instructions(instructions).has_value());
    ASSERT_EQ(decoder.get_dynamic_table_size(), 2U);

    const auto decoded = decoder.decode(*encoded, 8);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), input.size());
    for (std::size_t index{}; index < input.size(); ++index)
    {
        ASSERT_EQ((*decoded)[index].name, input[index].name);
        ASSERT_EQ((*decoded)[index].value, input[index].value);
    }

    const auto decoder_instructions = decoder.take_decoder_instructions();
    ASSERT_FALSE(decoder_instructions.empty());
    ASSERT_TRUE(encoder.process_decoder_instructions(decoder_instructions).has_value());
}

TEST(qpack_dynamic_header_blocks_remain_decodable_after_acknowledgement)
{
    qpack_encoder encoder{64U * 1024U};
    qpack_decoder decoder{64U * 1024U};
    encoder.set_max_blocked_streams(100U);
    decoder.set_max_blocked_streams(100U);

    const auto first = fields({
        {":method", "GET"}, {":scheme", "https"},
        {":authority", "127.0.0.1"}, {":path", "/get"}});
    const auto first_block = encoder.encode(first, 0U);
    ASSERT_TRUE(first_block.has_value());
    ASSERT_TRUE(decoder.process_encoder_instructions(
        encoder.take_encoder_instructions()).has_value());
    ASSERT_TRUE(decoder.decode(*first_block, 0U).has_value());
    ASSERT_TRUE(encoder.process_decoder_instructions(
        decoder.take_decoder_instructions()).has_value());

    const auto second = fields({
        {":method", "POST"}, {":scheme", "https"},
        {":authority", "127.0.0.1"}, {":path", "/post"},
        {"content-type", "text/plain"}, {"content-length", "9"}});
    const auto second_block = encoder.encode(second, 4U);
    ASSERT_TRUE(second_block.has_value());
    ASSERT_TRUE(decoder.process_encoder_instructions(
        encoder.take_encoder_instructions()).has_value());
    const auto decoded = decoder.decode(*second_block, 4U);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), second.size());
    ASSERT_TRUE(encoder.process_decoder_instructions(
        decoder.take_decoder_instructions()).has_value());
}

TEST(qpack_decoder_applies_local_maximum_table_capacity)
{
    qpack_encoder encoder{1024};
    qpack_decoder decoder;
    const auto input = fields({{"x-cnetmod-capacity", "value"}});

    ASSERT_TRUE(encoder.encode(input, 18).has_value());
    const auto instructions = encoder.take_encoder_instructions();
    ASSERT_FALSE(decoder.process_encoder_instructions(instructions).has_value());

    decoder.set_max_table_capacity(1024);
    ASSERT_TRUE(decoder.process_encoder_instructions(instructions).has_value());
    ASSERT_EQ(decoder.get_dynamic_table_size(), 1U);
}

TEST(qpack_acknowledged_entries_stay_pinned_until_each_section_ack)
{
    qpack_encoder encoder{128U};
    qpack_decoder decoder{128U};
    const auto original = fields({{"x-original", std::string(40U, 'a')}});
    const auto first = encoder.encode(original, 0U);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(decoder.process_encoder_instructions(
        encoder.take_encoder_instructions()).has_value());
    // Acknowledging insertion is not acknowledgement of either field section.
    ASSERT_TRUE(encoder.process_decoder_instructions(
        decoder.take_decoder_instructions()).has_value());
    const auto delayed = encoder.encode(original, 4U);
    ASSERT_TRUE(delayed.has_value());
    ASSERT_TRUE(decoder.decode(*first, 0U).has_value());
    ASSERT_TRUE(encoder.process_decoder_instructions(
        decoder.take_decoder_instructions()).has_value());
    const auto replacement = encoder.encode(
        fields({{"x-replacement", std::string(40U, 'b')}}), 8U);
    ASSERT_TRUE(replacement.has_value());
    ASSERT_TRUE(decoder.process_encoder_instructions(
        encoder.take_encoder_instructions()).has_value());
    const auto decoded = decoder.decode(*delayed, 4U);
    ASSERT_TRUE(decoded.has_value());
    if (!decoded)
        return;
    ASSERT_EQ(decoded->front().name, original.front().name);
    ASSERT_EQ(decoded->front().value, original.front().value);
}

TEST(qpack_static_section_has_zero_required_insert_count_after_dynamic_use)
{
    qpack_encoder encoder{128U};
    ASSERT_TRUE(encoder.encode(fields({{"x-seed", "one"}}), 0U).has_value());
    const auto block = encoder.encode(fields({{":status", "200"}}), 4U);
    ASSERT_TRUE(block.has_value());
    // RFC 9204 section 2.1.2 requires zero when no dynamic entry is referenced.
    ASSERT_EQ(std::to_integer<unsigned>(block->front()), 0U);
}

TEST(qpack_small_table_does_not_evict_an_insert_in_the_same_section)
{
    qpack_encoder encoder{64U};
    qpack_decoder decoder{64U};
    const auto input = fields({{"x-first", "value-one"}, {"x-second", "value-two"}});
    const auto block = encoder.encode(input, 0U);
    ASSERT_TRUE(block.has_value());
    ASSERT_TRUE(decoder.process_encoder_instructions(
        encoder.take_encoder_instructions()).has_value());
    const auto decoded = decoder.decode(*block, 0U);
    ASSERT_TRUE(decoded.has_value());
    if (!decoded)
        return;
    ASSERT_EQ(decoded->size(), input.size());
    for (std::size_t index{}; index < input.size(); ++index)
    {
        ASSERT_EQ((*decoded)[index].name, input[index].name);
        ASSERT_EQ((*decoded)[index].value, input[index].value);
    }
}

TEST(qpack_section_ack_releases_only_the_oldest_section_on_a_stream)
{
    qpack_encoder encoder{128U};
    qpack_decoder decoder{128U};
    const auto input = fields({{"x-original", std::string(40U, 'a')}});
    const auto headers = encoder.encode(input, 0U);
    ASSERT_TRUE(headers.has_value());
    ASSERT_TRUE(decoder.process_encoder_instructions(
        encoder.take_encoder_instructions()).has_value());
    const auto trailers = encoder.encode(input, 0U);
    ASSERT_TRUE(trailers.has_value());
    ASSERT_TRUE(decoder.decode(*headers, 0U).has_value());
    ASSERT_TRUE(encoder.process_decoder_instructions(
        decoder.take_decoder_instructions()).has_value());
    ASSERT_TRUE(encoder.encode(fields({{"x-new", std::string(40U, 'b')}}), 4U).has_value());
    ASSERT_TRUE(decoder.process_encoder_instructions(
        encoder.take_encoder_instructions()).has_value());
    const auto decoded = decoder.decode(*trailers, 0U);
    ASSERT_TRUE(decoded.has_value());
    if (!decoded)
        return;
    ASSERT_EQ(decoded->front().value, input.front().value);
    ASSERT_TRUE(encoder.process_decoder_instructions(
        decoder.take_decoder_instructions()).has_value());
}

TEST(qpack_huffman_strings_round_trip)
{
    qpack_encoder encoder;
    qpack_decoder decoder;
    const auto input = fields({{"x-repeat", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}});

    const auto encoded = encoder.encode(input, 12);
    ASSERT_TRUE(encoded.has_value());
    ASSERT_TRUE(encoded->size() >= 3U);
    const auto decoded = decoder.decode(*encoded, 12);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), input.size());
    ASSERT_EQ((*decoded)[0].name, input[0].name);
    ASSERT_EQ((*decoded)[0].value, input[0].value);
}

TEST(qpack_encoder_and_decoder_stream_instructions)
{
    qpack_encoder encoder{1024};
    qpack_decoder decoder{1024};
    const auto input = fields({{"content-type", "application/cnetmod"}});

    ASSERT_TRUE(encoder.encode(input, 16).has_value());
    const auto instructions = encoder.take_encoder_instructions();
    ASSERT_TRUE(decoder.process_encoder_instructions(instructions).has_value());
    ASSERT_EQ(decoder.get_dynamic_table_size(), 1U);

    // The Insert Count Increment acknowledges the encoder's first insertion.
    ASSERT_TRUE(encoder.process_decoder_instructions(
                           decoder.take_decoder_instructions())
            .has_value());

    const std::vector<std::byte> duplicate_latest{std::byte{0x00}};
    ASSERT_TRUE(decoder.process_encoder_instructions(as_span(duplicate_latest)).has_value());
    ASSERT_EQ(decoder.get_dynamic_table_size(), 2U);

    // This manually injected Duplicate has no corresponding encoder state,
    // so discard its local increment before validating cancellation handling.
    (void)decoder.take_decoder_instructions();

    decoder.cancel_stream(16);
    const auto decoder_instructions = decoder.take_decoder_instructions();
    ASSERT_FALSE(decoder_instructions.empty());
    ASSERT_TRUE(encoder.process_decoder_instructions(decoder_instructions).has_value());
}

TEST(qpack_static_response_headers_round_trip)
{
    const std::array fields{
        header_field{"content-type", "text/plain"},
        header_field{"vary", "Accept-Encoding"},
        header_field{"content-encoding", "br"}};
    auto cached = qpack_static_response_headers::encode(200, fields);
    ASSERT_TRUE(cached.has_value());
    ASSERT_EQ(cached->status(), 200);

    qpack_decoder decoder;
    auto decoded = decoder.decode(cached->encoded());
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), fields.size() + 1U);
    ASSERT_EQ(decoded->front().name, ":status");
    ASSERT_EQ(decoded->front().value, "200");
    for (std::size_t index = 0; index < fields.size(); ++index)
    {
        ASSERT_EQ(decoded->at(index + 1U).name, fields[index].name);
        ASSERT_EQ(decoded->at(index + 1U).value, fields[index].value);
    }
}

TEST(qpack_rejects_header_block_that_requires_unreceived_dynamic_entries)
{
    qpack_encoder encoder{1024};
    qpack_decoder decoder{1024};
    const auto input = fields({{"x-cnetmod-blocked", "value"}});

    const auto encoded = encoder.encode(input, 20);
    ASSERT_TRUE(encoded.has_value());
    const auto result = decoder.decode(*encoded, 20);
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), std::make_error_code(std::errc::resource_unavailable_try_again));
}

TEST(qpack_unblocks_retained_header_block_and_acknowledges_once)
{
    qpack_encoder encoder{1024};
    qpack_decoder decoder{1024};
    decoder.set_max_blocked_streams(1);
    const auto input = fields({{"x-cnetmod-blocked", "value"}});

    const auto encoded = encoder.encode(input, 24);
    ASSERT_TRUE(encoded.has_value());
    ASSERT_EQ(decoder.decode(*encoded, 24).error(), std::make_error_code(std::errc::resource_unavailable_try_again));

    const auto encoder_instructions = encoder.take_encoder_instructions();
    ASSERT_TRUE(decoder.process_encoder_instructions(encoder_instructions).has_value());
    const auto completed = decoder.take_completed_header_blocks();
    ASSERT_EQ(completed.size(), 1U);
    ASSERT_EQ(completed[0].stream_id, 24U);
    ASSERT_EQ(completed[0].headers.size(), input.size());
    ASSERT_EQ(completed[0].headers[0].name, input[0].name);
    ASSERT_EQ(completed[0].headers[0].value, input[0].value);

    const auto acknowledgements = decoder.take_decoder_instructions();
    ASSERT_TRUE(encoder.process_decoder_instructions(acknowledgements).has_value());
    ASSERT_TRUE(decoder.take_completed_header_blocks().empty());
}

TEST(qpack_enforces_blocked_stream_limit_and_cancellation)
{
    qpack_encoder encoder{1024};
    qpack_decoder decoder{1024};
    decoder.set_max_blocked_streams(1);
    const auto first = encoder.encode(fields({{"x-first", "one"}}), 28);
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(decoder.decode(*first, 28).error(), std::make_error_code(std::errc::resource_unavailable_try_again));

    // Reuse the inserted name so this is an actually blocked dynamic
    // reference. An unrelated literal must carry RIC zero and cannot block.
    const auto second = encoder.encode(fields({{"x-first", "two"}}), 32);
    ASSERT_TRUE(second.has_value());
    ASSERT_FALSE(decoder.decode(*second, 32).has_value());

    decoder.cancel_stream(28);
    const auto cancellation = decoder.take_decoder_instructions();
    ASSERT_TRUE(encoder.process_decoder_instructions(cancellation).has_value());
}

RUN_TESTS();
