#include "test_framework.hpp"

import std;
import cnetmod.protocol.coap;

namespace {

auto bytes(std::initializer_list<unsigned char> values) -> std::vector<std::byte> {
    std::vector<std::byte> out;
    out.reserve(values.size());
    for (auto value : values) {
        out.push_back(static_cast<std::byte>(value));
    }
    return out;
}

} // namespace

TEST(coap_block_option_roundtrip) {
    cnetmod::coap::block_option block{
        .number = 17,
        .more = true,
        .size_exponent = 4,
    };

    auto encoded = cnetmod::coap::encode_block_option(block);
    auto decoded = cnetmod::coap::decode_block_option(encoded);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->number, 17u);
    ASSERT_TRUE(decoded->more);
    ASSERT_EQ(decoded->size_exponent, 4u);
    ASSERT_EQ(decoded->block_size(), 256u);
}

TEST(coap_message_roundtrip) {
    auto req = cnetmod::coap::make_request(cnetmod::coap::request_options{
        .method_code = cnetmod::coap::method::post,
        .path = "/sensor/temp",
        .query = "unit=c",
        .content_type = cnetmod::coap::content_format::text_plain,
        .accept = cnetmod::coap::content_format::json,
        .payload = cnetmod::coap::to_bytes("22.5"),
    });
    req.message_id = 0x1234;
    req.token = bytes({0xaa, 0xbb});

    auto raw = cnetmod::coap::serialize_message(req);
    ASSERT_TRUE(raw.has_value());

    auto parsed = cnetmod::coap::parse_message(*raw);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->message_id, 0x1234u);
    ASSERT_EQ(parsed->token.size(), 2u);
    ASSERT_EQ(cnetmod::coap::extract_path(*parsed), std::string("/sensor/temp"));
    ASSERT_EQ(cnetmod::coap::extract_query(*parsed), std::string("unit=c"));
    ASSERT_EQ(cnetmod::coap::payload_text(*parsed), std::string("22.5"));
    ASSERT_TRUE(parsed->first_option(cnetmod::coap::option_number::accept).has_value());
}

TEST(coap_rejects_bad_token_length) {
    auto raw = bytes({0x49, 0x01, 0x00, 0x01});
    auto parsed = cnetmod::coap::parse_message(raw);
    ASSERT_FALSE(parsed.has_value());
}

TEST(coap_rejects_empty_message_with_token) {
    cnetmod::coap::message msg;
    msg.code = 0;
    msg.token = bytes({0x01});
    auto raw = cnetmod::coap::serialize_message(msg);
    ASSERT_FALSE(raw.has_value());
}

TEST(coap_extended_option_roundtrip) {
    cnetmod::coap::message msg;
    msg.set_method(cnetmod::coap::method::get);
    msg.message_id = 0x2211;
    msg.token = bytes({0x10});
    msg.add_string_option(cnetmod::coap::option_number::uri_path, "x");
    auto large = bytes({0xde, 0xad, 0xbe, 0xef});
    msg.add_option(300, large);

    auto raw = cnetmod::coap::serialize_message(msg);
    ASSERT_TRUE(raw.has_value());
    auto parsed = cnetmod::coap::parse_message(*raw);
    ASSERT_TRUE(parsed.has_value());
    auto opt = std::ranges::find_if(parsed->options, [](const auto& item) {
        return item.number == 300;
    });
    ASSERT_TRUE(opt != parsed->options.end());
    ASSERT_EQ(opt->value.size(), 4u);
    ASSERT_EQ(std::to_integer<unsigned>(opt->value[0]), 0xdeu);
}

TEST(coap_rejects_reserved_option_nibble) {
    auto raw = bytes({
        0x40, 0x01, 0x00, 0x01,
        0xf0,
    });
    auto parsed = cnetmod::coap::parse_message(raw);
    ASSERT_FALSE(parsed.has_value());
}

TEST(coap_rejects_block_bert_szx_on_udp) {
    auto decoded = cnetmod::coap::decode_block_option(bytes({0x07}));
    ASSERT_FALSE(decoded.has_value());
}

TEST(coap_conditional_option_roundtrip) {
    auto req = cnetmod::coap::make_request(cnetmod::coap::request_options{
        .method_code = cnetmod::coap::method::get,
        .path = "/sensors/temp",
    });
    auto etag = cnetmod::coap::to_bytes("temp-v1");
    req.add_option(cnetmod::coap::option_number::if_match, etag);
    req.add_option(cnetmod::coap::option_number::if_none_match, {});
    req.message_id = 0x3333;
    req.token = bytes({0x01, 0x02});

    auto raw = cnetmod::coap::serialize_message(req);
    ASSERT_TRUE(raw.has_value());
    auto parsed = cnetmod::coap::parse_message(*raw);
    ASSERT_TRUE(parsed.has_value());
    auto if_match = parsed->first_option(cnetmod::coap::option_number::if_match);
    auto if_none = parsed->first_option(cnetmod::coap::option_number::if_none_match);
    ASSERT_TRUE(if_match.has_value());
    ASSERT_TRUE(if_none.has_value());
    ASSERT_EQ(if_match->as_string(), std::string("temp-v1"));
    ASSERT_TRUE(if_none->value.empty());
}

RUN_TESTS()
