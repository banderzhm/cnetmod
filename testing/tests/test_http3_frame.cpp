#include "test_framework.hpp"

import cnetmod.protocol.http.v3.frame;
import cnetmod.protocol.http.v3.session;
import cnetmod.protocol.http.semantics;
import cnetmod.protocol.quic;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.core.buffer;
import std;

TEST(http3_data_frame_roundtrip)
{
    // Test DATA frame encoding and decoding

    cnetmod::http::v3::data_frame frame;
    cnetmod::byte_buffer data = {
        static_cast<std::byte>('H'), static_cast<std::byte>('e'),
        static_cast<std::byte>('l'), static_cast<std::byte>('l'),
        static_cast<std::byte>('o')};
    frame.data = data.view();

    auto encoded = cnetmod::http::v3::encode_http3_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Should have: 1 byte type (0x00) + varint length + data bytes
    ASSERT_TRUE(encoded.size() >= 2 + data.size());
}

TEST(http3_async_server_handler_contract)
{
    cnetmod::http::v3::async_server_request_handler handler =
        [](cnetmod::http::v3::http3_request& request,
            cnetmod::http::v3::http3_response& response,
            cnetmod::cancel_token& token)
        -> cnetmod::task<std::expected<void, std::error_code>>
    {
        if (token.is_cancelled())
            co_return std::unexpected(
                std::make_error_code(std::errc::operation_canceled));
        response.status = cnetmod::http::status::ok;
        response.body = request.path;
        co_return {};
    };

    cnetmod::http::v3::http3_request request;
    request.path = "/dynamic";
    cnetmod::http::v3::http3_response response;
    cnetmod::cancel_token token;
    auto handled = cnetmod::sync_wait(handler(request, response, token));
    ASSERT_TRUE(handled.has_value());
    ASSERT_EQ(response.status, cnetmod::http::status::ok);
    ASSERT_EQ(response.body, "/dynamic");
}

TEST(webtransport_async_server_handler_contract)
{
    cnetmod::http::v3::async_webtransport_handler handler =
        [](cnetmod::http::v3::http3_request& request,
            cnetmod::http::v3::webtransport_session& session,
            cnetmod::cancel_token& token)
        -> cnetmod::task<std::expected<void, std::error_code>>
    {
        if (token.is_cancelled() || !session.is_open() ||
            request.protocol != "webtransport")
            co_return std::unexpected(
                std::make_error_code(std::errc::operation_canceled));
        co_return {};
    };

    ASSERT_TRUE(static_cast<bool>(handler));
}

TEST(http3_headers_frame_with_qpack)
{
    // Test HEADERS frame with QPACK-encoded content

    cnetmod::http::v3::headers_frame frame;

    // Create some literal headers (simplified - not real QPACK encoded)
    cnetmod::byte_buffer headers{
        static_cast<std::byte>(0x30), // Literal without indexing prefix
        static_cast<std::byte>('h'), static_cast<std::byte>('o'),
        static_cast<std::byte>('s'), static_cast<std::byte>('t'),
        static_cast<std::byte>('e'), static_cast<std::byte>('x'),
        static_cast<std::byte>('a'), static_cast<std::byte>('m'),
        static_cast<std::byte>('p'), static_cast<std::byte>('.'),
        static_cast<std::byte>('c'), static_cast<std::byte>('o'),
        static_cast<std::byte>('m')};

    frame.encoded_headers = headers.view();

    auto encoded = cnetmod::http::v3::encode_http3_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Type (1 byte) + length varint + header bytes
    ASSERT_TRUE(encoded.size() >= 2 + frame.encoded_headers.size());

    // Decode back
    auto result = cnetmod::http::v3::decode_http3_frame(encoded.view());

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::http::v3::headers_frame>(result->first));
}

TEST(http3_settings_frame_multi_entries)
{
    // Test SETTINGS frame with multiple settings entries

    cnetmod::http::v3::settings_frame frame;

    // Add multiple settings per RFC 9114
    frame.settings[0x1] = std::uint64_t(1048576);  // MAX_HEADER_LIST_SIZE
    frame.settings[0x2] = std::uint64_t(10485760); // QPACK_MAX_TABLE_CAPACITY
    frame.settings[0x3] = std::uint64_t(100);      // QPACK_BLOCKED_STREAMS

    auto encoded = cnetmod::http::v3::encode_http3_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Type (1 byte) + length varint + key-value pairs * 3
    ASSERT_TRUE(encoded.size() >= 2);

    // Decode back
    auto result = cnetmod::http::v3::decode_http3_frame(encoded.view());

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::http::v3::settings_frame>(result->first));
}

TEST(http3_goaway_frame_decode)
{
    // Test GOAWAY frame

    cnetmod::http::v3::goaway_frame frame;
    frame.stream_id = 42;
    frame.error_code = 0x100; // No error
    frame.reason = "Goodbye";

    auto encoded = cnetmod::http::v3::encode_http3_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Decode back
    auto result = cnetmod::http::v3::decode_http3_frame(encoded.view());

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::http::v3::goaway_frame>(result->first));

    auto& decoded = std::get<cnetmod::http::v3::goaway_frame>(result->first);
    ASSERT_EQ(decoded.stream_id, frame.stream_id);
}

TEST(http3_max_push_id_encode)
{
    // Test MAX_PUSH_ID frame

    cnetmod::http::v3::max_push_id_frame frame;
    frame.max_push_id = 100;

    auto encoded = cnetmod::http::v3::encode_http3_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Should be exactly: 1 byte type (0x0D) + 1 byte varint value
    ASSERT_TRUE(encoded.size() >= 2);

    // First byte should be frame type
    ASSERT_EQ(std::to_integer<std::uint8_t>(encoded[0]), 0x0D);

    // Decode back
    std::span<const std::byte> input{encoded.data(), encoded.size()};
    auto result = cnetmod::http::v3::decode_http3_frame(input);

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::http::v3::max_push_id_frame>(result->first));
}

TEST(http3_frame_varint_length_decoding)
{
    // Test that variable-length integer length decoding works correctly

    cnetmod::http::v3::data_frame frame1;
    cnetmod::byte_buffer data1 = {static_cast<std::byte>(0xFF)}; // 255 bytes
    frame1.data = data1.view();

    auto encoded1 = cnetmod::http::v3::encode_http3_frame(frame1);
    ASSERT_TRUE(encoded1.size() > 0);

    // Length of 255 needs multi-byte varint
    ASSERT_TRUE(encoded1.size() >= 2 + data1.size());

    // Test with larger size (needs 4-byte varint)
    cnetmod::http::v3::data_frame frame2;
    cnetmod::byte_buffer large_data(16384, static_cast<std::byte>(0x00));
    frame2.data = large_data.view();

    auto encoded2 = cnetmod::http::v3::encode_http3_frame(frame2);
    ASSERT_TRUE(encoded2.size() > 0);
    ASSERT_TRUE(encoded2.size() >= 4 + large_data.size()); // Type + 4-byte varint + data
}

TEST(http3_cancel_push_frame)
{
    // Test CANCEL_PUSH frame

    cnetmod::http::v3::cancel_push_frame frame;
    frame.push_id = 10;

    auto encoded = cnetmod::http::v3::encode_http3_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Type (1 byte) + push id varint
    ASSERT_TRUE(encoded.size() >= 2);

    // Type should be 0x03
    ASSERT_EQ(std::to_integer<std::uint8_t>(encoded[0]), 0x03);
}

TEST(http3_push_promise_frame)
{
    // Test PUSH_PROMISE frame

    cnetmod::http::v3::push_promise_frame frame;
    frame.promised_stream_id = 42;

    cnetmod::byte_buffer headers{
        static_cast<std::byte>(0x30), // Literal prefix
        static_cast<std::byte>('t'), static_cast<std::byte>('e'),
        static_cast<std::byte>('s'), static_cast<std::byte>('t')};
    frame.encoded_headers = headers.view();

    auto encoded = cnetmod::http::v3::encode_http3_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);
}

TEST(http3_empty_data_frame)
{
    // Test empty DATA frame

    cnetmod::http::v3::data_frame frame;
    frame.data = {};

    auto encoded = cnetmod::http::v3::encode_http3_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Should still encode successfully with 0 length
    ASSERT_TRUE(encoded.size() >= 2); // Type + length = 0
}

TEST(http3_settings_single_entry)
{
    // Test SETTINGS frame with single entry

    cnetmod::http::v3::settings_frame frame;
    frame.settings[0x1] = std::uint64_t(100); // Single setting

    auto encoded = cnetmod::http::v3::encode_http3_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Decodes correctly?
    auto result = cnetmod::http::v3::decode_http3_frame(encoded.view());

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<cnetmod::http::v3::settings_frame>(result->first));
}

TEST(http3_frame_type_variants)
{
    // Test different frame types via variant

    // DATA frame
    cnetmod::http::v3::data_frame df;
    const cnetmod::byte_buffer data{
        static_cast<std::byte>('A'), static_cast<std::byte>('B')};
    df.data = data.view();

    auto encoded_df = cnetmod::http::v3::encode_http3_frame(df);
    ASSERT_TRUE(encoded_df.size() > 0);
    ASSERT_EQ(std::to_integer<std::uint8_t>(encoded_df[0]), 0x00);

    // HEADERS frame
    cnetmod::http::v3::headers_frame hf;
    hf.encoded_headers = {};

    auto encoded_hf = cnetmod::http::v3::encode_http3_frame(hf);
    ASSERT_TRUE(encoded_hf.size() > 0);
    ASSERT_EQ(std::to_integer<std::uint8_t>(encoded_hf[0]), 0x01);

    // Settings frame
    cnetmod::http::v3::settings_frame sf;
    sf.settings[0x1] = std::uint64_t(100);

    auto encoded_sf = cnetmod::http::v3::encode_http3_frame(sf);
    ASSERT_TRUE(encoded_sf.size() > 0);
    ASSERT_EQ(std::to_integer<std::uint8_t>(encoded_sf[0]), 0x04);
}

TEST(http3_goaway_no_error)
{
    // Test GOAWAY without error code

    cnetmod::http::v3::goaway_frame frame;
    frame.stream_id = 100;
    frame.error_code = std::nullopt;
    frame.reason = "";

    auto encoded = cnetmod::http::v3::encode_http3_frame(frame);
    ASSERT_TRUE(encoded.size() > 0);

    // Decode
    auto result = cnetmod::http::v3::decode_http3_frame(encoded.view());

    ASSERT_TRUE(result.has_value());
}

// RFC 9114 section 7.2.8 prohibits duplicate SETTINGS identifiers.  Keeping
// this at the frame decoder boundary prevents ambiguous peer configuration
// from leaking into the session state machine.
TEST(http3_settings_reject_duplicate_identifier)
{
    const cnetmod::byte_buffer frame = {
        std::byte{0x04}, // SETTINGS
        std::byte{0x04}, // payload length
        std::byte{0x01},
        std::byte{0x10}, // SETTINGS_MAX_FIELD_SECTION_SIZE = 16
        std::byte{0x01},
        std::byte{0x20}, // duplicate identifier
    };

    const auto decoded = cnetmod::http::v3::decode_http3_frame(frame.view());
    ASSERT_FALSE(decoded.has_value());
    ASSERT_EQ(decoded.error(), std::make_error_code(std::errc::protocol_error));
}

TEST(http3_priority_update_frame_roundtrip)
{
    using namespace cnetmod::http::v3;

    priority_update_frame frame{.prioritized_element_id = 12U,
        .priority_field_value = "u=1, i"};
    auto encoded = encode_http3_frame(frame);
    ASSERT_TRUE(!encoded.empty());

    auto decoded = decode_http3_frame(encoded.view());
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(std::holds_alternative<priority_update_frame>(decoded->first));
    const auto& priority = std::get<priority_update_frame>(decoded->first);
    ASSERT_EQ(priority.prioritized_element_id, 12U);
    ASSERT_EQ(priority.priority_field_value, "u=1, i");

    const auto parsed = parse_http_priority(priority.priority_field_value);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->urgency, 1U);
    ASSERT_TRUE(parsed->incremental);
    ASSERT_EQ(format_http_priority(*parsed), "u=1, i");
}

TEST(http3_priority_field_rejects_invalid_urgency)
{
    using namespace cnetmod::http::v3;

    ASSERT_FALSE(parse_http_priority("u=8").has_value());
    ASSERT_FALSE(parse_http_priority("u=1, u=2").has_value());
    ASSERT_FALSE(parse_http_priority("i, i").has_value());
}

TEST(http3_request_priority_contract)
{
    using namespace cnetmod::http::v3;

    http3_request request;
    ASSERT_FALSE(request.priority.has_value());
    ASSERT_FALSE(request.request_stream.has_value());

    request.priority = http_priority{.urgency = 0U, .incremental = true};
    ASSERT_EQ(request.priority->urgency, 0U);
    ASSERT_TRUE(request.priority->incremental);

    using server_priority_api = cnetmod::task<std::expected<void, std::error_code>> (
        http3_server_session::*)(cnetmod::quic::stream_id, http_priority);
    using client_priority_api = cnetmod::task<std::expected<void, std::error_code>> (
        http3_client_session::*)(cnetmod::quic::stream_id, http_priority);
    static_assert(std::is_same_v<decltype(&http3_server_session::update_priority), server_priority_api>);
    static_assert(std::is_same_v<decltype(&http3_client_session::update_priority), client_priority_api>);
}

TEST(http3_settings_h3_datagram_roundtrip)
{
    cnetmod::http::v3::settings_frame frame;
    frame.settings[static_cast<std::uint64_t>(
        cnetmod::http::v3::http3_setting_key::h3_datagram)] = std::uint64_t{1};

    const auto encoded = cnetmod::http::v3::encode_http3_frame(frame);
    const auto decoded = cnetmod::http::v3::decode_http3_frame(encoded.view());
    ASSERT_TRUE(decoded.has_value());
    const auto& settings = std::get<cnetmod::http::v3::settings_frame>(decoded->first);
    const auto found = settings.settings.find(static_cast<std::uint64_t>(
        cnetmod::http::v3::http3_setting_key::h3_datagram));
    ASSERT_TRUE(found != settings.settings.end());
    ASSERT_EQ(std::get<std::uint64_t>(found->second), 1U);
}

TEST(http3_settings_extended_connect_roundtrip)
{
    cnetmod::http::v3::settings_frame frame;
    frame.settings[static_cast<std::uint64_t>(
        cnetmod::http::v3::http3_setting_key::enable_connect_protocol)] = std::uint64_t{1};

    const auto encoded = cnetmod::http::v3::encode_http3_frame(frame);
    const auto decoded = cnetmod::http::v3::decode_http3_frame(encoded.view());
    ASSERT_TRUE(decoded.has_value());
    const auto& settings = std::get<cnetmod::http::v3::settings_frame>(decoded->first);
    const auto found = settings.settings.find(static_cast<std::uint64_t>(
        cnetmod::http::v3::http3_setting_key::enable_connect_protocol));
    ASSERT_TRUE(found != settings.settings.end());
    ASSERT_EQ(std::get<std::uint64_t>(found->second), 1U);
}

TEST(http3_settings_enable_webtransport_roundtrip)
{
    cnetmod::http::v3::settings_frame frame;
    frame.settings[static_cast<std::uint64_t>(
        cnetmod::http::v3::http3_setting_key::enable_webtransport)] = std::uint64_t{1};

    const auto encoded = cnetmod::http::v3::encode_http3_frame(frame);
    const auto decoded = cnetmod::http::v3::decode_http3_frame(encoded.view());
    ASSERT_TRUE(decoded.has_value());
    const auto& settings = std::get<cnetmod::http::v3::settings_frame>(decoded->first);
    const auto found = settings.settings.find(static_cast<std::uint64_t>(
        cnetmod::http::v3::http3_setting_key::enable_webtransport));
    ASSERT_TRUE(found != settings.settings.end());
    ASSERT_EQ(std::get<std::uint64_t>(found->second), 1U);
}

TEST(http3_settings_webtransport_max_sessions_roundtrip)
{
    cnetmod::http::v3::settings_frame frame;
    frame.settings[static_cast<std::uint64_t>(
        cnetmod::http::v3::http3_setting_key::webtransport_max_sessions)] = std::uint64_t{1};

    const auto encoded = cnetmod::http::v3::encode_http3_frame(frame);
    const auto decoded = cnetmod::http::v3::decode_http3_frame(encoded.view());
    ASSERT_TRUE(decoded.has_value());
    const auto& settings = std::get<cnetmod::http::v3::settings_frame>(decoded->first);
    const auto found = settings.settings.find(static_cast<std::uint64_t>(
        cnetmod::http::v3::http3_setting_key::webtransport_max_sessions));
    ASSERT_TRUE(found != settings.settings.end());
    ASSERT_EQ(std::get<std::uint64_t>(found->second), 1U);
}

TEST(http3_datagram_context_id_roundtrip)
{
    const cnetmod::byte_buffer payload{std::byte{0xaa}, std::byte{0xbb}};
    const auto encoded = cnetmod::http::v3::encode_http_datagram(
        {0x1234U, payload.view()});
    const auto decoded = cnetmod::http::v3::decode_http_datagram(encoded.view());
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->context_id, 0x1234U);
    ASSERT_EQ(decoded->payload.size(), 2U);
    ASSERT_EQ(std::to_integer<unsigned>(decoded->payload[1]), 0xbbU);
}

TEST(webtransport_extended_connect_request_shape)
{
    const auto request = cnetmod::http::v3::make_webtransport_connect_request(
        "example.test", "/chat");
    ASSERT_TRUE(request.method == cnetmod::http::http_method::CONNECT);
    ASSERT_EQ(request.host, "example.test");
    ASSERT_EQ(request.path, "/chat");
    ASSERT_EQ(request.protocol, "webtransport");
    ASSERT_EQ(request.headers.at("sec-webtransport-http3-draft02"), "1");
}

TEST(webtransport_stream_prefaces_roundtrip_and_validate_session_id)
{
    constexpr std::uint64_t session_id = 12U;
    const auto bidi = cnetmod::http::v3::encode_webtransport_bidirectional_stream_preface(session_id);
    ASSERT_TRUE(bidi.has_value());
    const auto decoded_bidi = cnetmod::http::v3::decode_webtransport_stream_preface(
        {bidi->data(), bidi->size()}, false);
    ASSERT_TRUE(decoded_bidi.has_value());
    ASSERT_EQ(decoded_bidi->first, session_id);
    ASSERT_EQ(decoded_bidi->second, bidi->size());

    // aioquic emits its deployed draft marker (0x41) before the RFC 9220
    // session ID. Accept it on input without changing cnetmod's RFC encoder.
    const cnetmod::byte_buffer aioquic_bidi = {
        std::byte{0x40}, std::byte{0x41},
        std::byte{static_cast<unsigned char>(session_id)}};
    const auto decoded_aioquic_bidi = cnetmod::http::v3::decode_webtransport_stream_preface(
        aioquic_bidi.view(), false);
    ASSERT_TRUE(decoded_aioquic_bidi.has_value());
    ASSERT_EQ(decoded_aioquic_bidi->first, session_id);
    ASSERT_EQ(decoded_aioquic_bidi->second, aioquic_bidi.size());

    const auto uni = cnetmod::http::v3::encode_webtransport_unidirectional_stream_preface(session_id);
    ASSERT_TRUE(uni.has_value());
    const auto decoded_uni = cnetmod::http::v3::decode_webtransport_stream_preface(
        {uni->data(), uni->size()}, true);
    ASSERT_TRUE(decoded_uni.has_value());
    ASSERT_EQ(decoded_uni->first, session_id);
    ASSERT_EQ(decoded_uni->second, uni->size());

    const auto invalid = cnetmod::http::v3::encode_webtransport_bidirectional_stream_preface(1U);
    ASSERT_FALSE(invalid.has_value());
}

TEST(http3_frame_rejects_truncated_varint_length)
{
    const cnetmod::byte_buffer frame = {
        std::byte{0x00}, // DATA
        std::byte{0x40}, // two-byte varint length, missing its second octet
    };

    const auto decoded = cnetmod::http::v3::decode_http3_frame(frame.view());
    ASSERT_FALSE(decoded.has_value());
    ASSERT_EQ(decoded.error(), std::make_error_code(std::errc::message_size));
}

TEST(http3_goaway_rejects_trailing_payload)
{
    const cnetmod::byte_buffer frame = {
        std::byte{0x07}, // GOAWAY
        std::byte{0x02}, // payload length
        std::byte{0x00},
        std::byte{0x00}, // GOAWAY carries exactly one varint
    };

    const auto decoded = cnetmod::http::v3::decode_http3_frame(frame.view());
    ASSERT_FALSE(decoded.has_value());
    ASSERT_EQ(decoded.error(), std::make_error_code(std::errc::invalid_argument));
}

TEST(http3_unknown_extension_frame_preserves_payload_and_consumed_length)
{
    const cnetmod::byte_buffer frame = {
        std::byte{0x21}, // Unassigned extension frame type.
        std::byte{0x03},
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0xcc},
        std::byte{0x00}, // Next frame: an empty DATA frame.
        std::byte{0x00},
    };

    const auto decoded = cnetmod::http::v3::decode_http3_frame(frame.view());
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->second, 5U);
    ASSERT_TRUE(std::holds_alternative<cnetmod::http::v3::unknown_frame>(decoded->first));
    const auto& unknown = std::get<cnetmod::http::v3::unknown_frame>(decoded->first);
    ASSERT_EQ(unknown.type, 0x21U);
    ASSERT_EQ(unknown.payload.size(), 3U);
    ASSERT_EQ(std::to_integer<unsigned>(unknown.payload[0]), 0xaaU);
}

RUN_TESTS();
