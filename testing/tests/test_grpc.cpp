#include "test_framework.hpp"

import std;
import cnetmod.protocol.grpc;

using namespace cnetmod::grpc;

TEST(grpc_frame_roundtrip) {
    std::array<std::byte, 3> payload{std::byte{1}, std::byte{2}, std::byte{3}};
    auto encoded = encode_frame(payload);
    ASSERT_TRUE(encoded.has_value());
    ASSERT_EQ(encoded->size(), std::size_t{8});

    auto decoded = decode_frames(*encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), std::size_t{1});
    ASSERT_FALSE(decoded->front().compressed);
    ASSERT_EQ(decoded->front().payload.size(), std::size_t{3});
}

TEST(grpc_stream_decoder_handles_partial_frames) {
    std::array<std::byte, 2> payload{std::byte{0xaa}, std::byte{0xbb}};
    auto encoded = encode_frame(payload);
    ASSERT_TRUE(encoded.has_value());

    stream_decoder decoder;
    auto first = decoder.feed(std::span<const std::byte>{encoded->data(), 3});
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->size(), std::size_t{0});

    auto second = decoder.feed(std::span<const std::byte>{encoded->data() + 3, encoded->size() - 3});
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(second->size(), std::size_t{1});
    ASSERT_EQ(second->front().payload.size(), std::size_t{2});
}

TEST(grpc_message_stream_decoder_emits_complete_messages_only) {
    std::array<std::byte, 2> payload{std::byte{0x11}, std::byte{0x22}};
    message_stream_encoder encoder;
    auto encoded = encoder.encode(payload);
    ASSERT_TRUE(encoded.has_value());

    message_stream_decoder decoder;
    auto first = decoder.feed(std::span<const std::byte>{encoded->data(), 2});
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(first->empty());
    ASSERT_EQ(decoder.buffered_bytes(), std::size_t{2});

    auto second = decoder.feed(std::span<const std::byte>{encoded->data() + 2, encoded->size() - 2});
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(second->size(), std::size_t{1});
    ASSERT_TRUE(std::ranges::equal(second->front(), payload));
}

TEST(grpc_service_path) {
    ASSERT_EQ(service_path("demo.Echo", "Say"), std::string("/demo.Echo/Say"));
}

TEST(grpc_status_response_uses_trailers) {
    auto resp = make_status_response(status{
        .code = status_code::unavailable,
        .message = "backend down",
        .trailers = {{"retry-info", "later"}},
    });

    ASSERT_EQ(resp.get_header("Content-Type"), std::string_view("application/grpc"));
    ASSERT_TRUE(resp.get_header("grpc-status").empty());
    ASSERT_EQ(resp.trailers().at("grpc-status"), std::string("14"));
    ASSERT_EQ(resp.trailers().at("grpc-message"), std::string("backend down"));
    ASSERT_EQ(resp.trailers().at("retry-info"), std::string("later"));
    ASSERT_EQ(static_cast<int>(status_from_response(resp).code),
              static_cast<int>(status_code::unavailable));
}

TEST(grpc_health_codec_roundtrip) {
    auto req = health::encode_request("demo.Echo");
    auto decoded_req = health::decode_request(req);
    ASSERT_TRUE(decoded_req.has_value());
    ASSERT_EQ(*decoded_req, std::string("demo.Echo"));

    auto resp = health::encode_response(health::serving_status::serving);
    auto decoded_resp = health::decode_response(resp);
    ASSERT_TRUE(decoded_resp.has_value());
    ASSERT_EQ(static_cast<int>(*decoded_resp),
              static_cast<int>(health::serving_status::serving));
}

TEST(grpc_compressed_frames_are_rejected_without_opt_in) {
    std::array<std::byte, 3> payload{std::byte{1}, std::byte{2}, std::byte{3}};
    auto frame = encode_frame(payload, true);
    ASSERT_TRUE(frame.has_value());

    auto decoded = decode_frames(*frame);
    ASSERT_TRUE(decoded.has_value());
    auto messages = frames_to_messages(*decoded, codec_options{
        .compression = compression_algorithm::gzip,
        .accept_compressed = false,
        .max_message_bytes = 1024,
    });
    ASSERT_FALSE(messages.has_value());
    ASSERT_EQ(static_cast<int>(messages.error().code),
              static_cast<int>(status_code::unimplemented));
}

#ifdef CNETMOD_HAS_ZLIB
TEST(grpc_gzip_frame_roundtrip) {
    byte_buffer payload;
    for (int i = 0; i < 256; ++i) {
        payload.push_back(static_cast<std::byte>('a' + (i % 3)));
    }
    std::vector<byte_buffer> messages{payload};

    auto encoded = encode_frames(messages, compression_algorithm::gzip);
    ASSERT_TRUE(encoded.has_value());
    ASSERT_EQ(std::to_integer<int>(encoded->front()), 1);

    auto frames = decode_frames(*encoded);
    ASSERT_TRUE(frames.has_value());
    ASSERT_TRUE(frames->front().compressed);

    auto decoded = frames_to_messages(*frames, codec_options{
        .compression = compression_algorithm::gzip,
        .accept_compressed = true,
        .max_message_bytes = 4096,
    });
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), std::size_t{1});
    ASSERT_EQ(decoded->front().size(), payload.size());
    ASSERT_TRUE(std::ranges::equal(decoded->front(), payload));
}
#endif

TEST(grpc_metadata_size_and_compression_helpers) {
    metadata md;
    md.emplace("authorization", "Bearer token");
    md.emplace("x-trace-id", "abc");
    ASSERT_TRUE(metadata_wire_size(md) >= std::size_t{30});
    ASSERT_EQ(metadata_value(md, "Authorization"), std::string_view("Bearer token"));
    ASSERT_TRUE(accepts_compression("identity, gzip", compression_algorithm::gzip));
    ASSERT_FALSE(accepts_compression("identity", compression_algorithm::gzip));
    ASSERT_EQ(static_cast<int>(compression_from_header("gzip").value()),
              static_cast<int>(compression_algorithm::gzip));
}

TEST(grpc_reflection_lists_services) {
    std::vector<std::string> services{
        "cnetmod.testing.grpc.EchoService",
        "grpc.health.v1.Health",
    };
    auto req = reflection::encode_list_services_request();
    auto decoded_req = reflection::decode_request(req);
    ASSERT_TRUE(decoded_req.has_value());
    ASSERT_EQ(static_cast<int>(decoded_req->kind),
              static_cast<int>(reflection::request_kind::list_services));

    auto resp = reflection::encode_list_services_response(
        req, std::span<const std::string>{services.data(), services.size()});
    auto decoded_resp = reflection::decode_list_services_response(resp);
    ASSERT_TRUE(decoded_resp.has_value());
    ASSERT_EQ(decoded_resp->size(), std::size_t{2});
    ASSERT_TRUE(std::ranges::find(*decoded_resp, "cnetmod.testing.grpc.EchoService") != decoded_resp->end());
    ASSERT_TRUE(std::ranges::find(*decoded_resp, "grpc.health.v1.Health") != decoded_resp->end());
}

TEST(grpc_router_options_are_configurable) {
    service_router router(server_options{
        .max_receive_message_bytes = 1024,
        .max_send_message_bytes = 2048,
        .max_metadata_bytes = 128,
        .accept_gzip = false,
    });
    ASSERT_EQ(router.options().max_receive_message_bytes, std::size_t{1024});
    ASSERT_EQ(router.options().max_send_message_bytes, std::size_t{2048});
    ASSERT_EQ(router.options().max_metadata_bytes, std::size_t{128});
    ASSERT_FALSE(router.options().accept_gzip);
}

RUN_TESTS()
