#include "test_framework.hpp"

import std;
import cnetmod.core.socket;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.instrumentation.metric;
import cnetmod.instrumentation.tracing;
import cnetmod.protocol.grpc;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.tracing;
import cnetmod.observability.grpc;
import cnetmod.observability.grpc_server;

using namespace cnetmod::grpc;

namespace {
auto no_op_grpc_handler(cnetmod::http::request_context&) -> cnetmod::task<void>
{
    co_return;
}
} // namespace

TEST(grpc_frame_roundtrip)
{
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

TEST(grpc_stream_decoder_handles_partial_frames)
{
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

TEST(grpc_stream_decoder_rejects_oversized_incomplete_frame)
{
    stream_decoder decoder(8);
    std::array<std::byte, 5> oversized_header{
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{9}};

    auto decoded = decoder.feed(oversized_header);
    ASSERT_FALSE(decoded.has_value());
    ASSERT_EQ(decoded.error(), std::make_error_code(std::errc::message_size));
    ASSERT_EQ(decoder.buffered_bytes(), std::size_t{0});
}

TEST(grpc_message_stream_decoder_emits_complete_messages_only)
{
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

TEST(grpc_service_path)
{
    ASSERT_EQ(service_path("demo.Echo", "Say"), std::string("/demo.Echo/Say"));
}

TEST(grpc_status_response_uses_trailers)
{
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

TEST(grpc_health_codec_roundtrip)
{
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

TEST(grpc_compressed_frames_are_rejected_without_opt_in)
{
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
TEST(grpc_gzip_frame_roundtrip)
{
    byte_buffer payload;
    for (int i = 0; i < 256; ++i)
    {
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

#ifdef CNETMOD_HAS_BROTLI
TEST(grpc_brotli_frame_roundtrip)
{
    byte_buffer payload;
    for (int i = 0; i < 256; ++i)
        payload.push_back(static_cast<std::byte>('a' + (i % 3)));
    std::vector<byte_buffer> messages{payload};
    auto encoded = encode_frames(messages, compression_algorithm::brotli);
    ASSERT_TRUE(encoded.has_value());
    auto frames = decode_frames(*encoded);
    ASSERT_TRUE(frames.has_value());
    auto decoded = frames_to_messages(*frames, codec_options{
                                                   .compression = compression_algorithm::brotli,
                                                   .accept_compressed = true,
                                                   .max_message_bytes = 4096,
                                               });
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(std::ranges::equal(decoded->front(), payload));
}
#endif

#ifdef CNETMOD_HAS_ZSTD
TEST(grpc_zstd_frame_roundtrip)
{
    byte_buffer payload;
    for (int i = 0; i < 256; ++i)
        payload.push_back(static_cast<std::byte>('a' + (i % 3)));
    std::vector<byte_buffer> messages{payload};
    auto encoded = encode_frames(messages, compression_algorithm::zstd);
    ASSERT_TRUE(encoded.has_value());
    auto frames = decode_frames(*encoded);
    ASSERT_TRUE(frames.has_value());
    auto decoded = frames_to_messages(*frames, codec_options{
                                                   .compression = compression_algorithm::zstd,
                                                   .accept_compressed = true,
                                                   .max_message_bytes = 4096,
                                               });
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(std::ranges::equal(decoded->front(), payload));
}
#endif

TEST(grpc_metadata_size_and_compression_helpers)
{
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

TEST(grpc_trace_context_is_explicit_and_single_valued)
{
    const auto parent = cnetmod::http::tracing::parse_traceparent(
        "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01",
        "vendor=value");
    ASSERT_TRUE(parent.has_value());
    metadata md{{"traceparent", "stale"}, {"tracestate", "stale"}};
    const auto child = inject_trace_context(md, *parent);
    ASSERT_EQ(child.trace_id, parent->trace_id);
    ASSERT_NE(child.span_id, parent->span_id);
    ASSERT_EQ(md.count("traceparent"), std::size_t{1});
    ASSERT_EQ(md.count("tracestate"), std::size_t{1});
    const auto extracted = extract_trace_context(md);
    ASSERT_TRUE(extracted.has_value());
    ASSERT_EQ(extracted->trace_id, child.trace_id);
    ASSERT_EQ(extracted->span_id, child.span_id);
    ASSERT_EQ(extracted->tracestate, "vendor=value");
}

TEST(grpc_reflection_lists_services)
{
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

TEST(grpc_router_options_are_configurable)
{
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

TEST(disabled_grpc_server_observation_returns_the_original_handler)
{
    cnetmod::http::handler_fn raw{no_op_grpc_handler};
    auto decorated = cnetmod::observability::grpc_server_handler(raw, {}, {});
    const auto target = decorated.target<decltype(&no_op_grpc_handler)>();
    ASSERT_TRUE(target != nullptr);
    ASSERT_TRUE(*target == &no_op_grpc_handler);
}

TEST(grpc_server_observation_extracts_remote_context_and_records_rpc_status)
{
    auto io = cnetmod::make_io_context();
    cnetmod::socket socket;
    cnetmod::http::response response;
    cnetmod::http::header_map headers{
        {"traceparent", "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"},
        {"tracestate", "vendor=value"},
    };
    cnetmod::http::request_context context{*io, socket, "POST", "/demo.Echo/Say",
        headers, {}, response, {}};
    ASSERT_EQ(context.get_header("traceparent"),
        "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");
    std::vector<cnetmod::instrumentation::completed_span> spans;
    std::vector<cnetmod::instrumentation::metric_measurement> measurements;
    cnetmod::http::handler_fn raw = [](cnetmod::http::request_context& request)
        -> cnetmod::task<void>
    {
        request.resp() = cnetmod::grpc::make_status_response({
            .code = cnetmod::grpc::status_code::unavailable,
            .message = "backend unavailable",
        });
        co_return;
    };
    const auto observed = cnetmod::observability::grpc_server_handler(raw, [&](const cnetmod::instrumentation::completed_span& span)
        {
            spans.push_back(span);
        },
        [&](cnetmod::instrumentation::metric_measurement measurement)
        {
            measurements.push_back(std::move(measurement));
        });

    cnetmod::sync_wait(observed(context));

    ASSERT_EQ(spans.size(), std::size_t{1});
    ASSERT_EQ(spans.front().context.trace_id, "4bf92f3577b34da6a3ce929d0e0e4736");
    ASSERT_EQ(spans.front().parent_span_id, "00f067aa0ba902b7");
    ASSERT_TRUE(spans.front().kind == cnetmod::instrumentation::span_kind::server);
    ASSERT_EQ(spans.front().name, "grpc.server demo.Echo/Say");
    ASSERT_TRUE(spans.front().result.status == cnetmod::instrumentation::operation_status::error);
    ASSERT_EQ(measurements.size(), std::size_t{1});
    ASSERT_EQ(measurements.front().name, "rpc.server.duration");
    const auto status = std::ranges::find_if(measurements.front().attributes,
        [](const auto& attribute)
        {
            return attribute.first == "rpc.grpc.status_code";
        });
    ASSERT_TRUE(status != measurements.front().attributes.end());
    ASSERT_EQ(status->second, "14");
}

TEST(grpc_server_observation_contains_exporter_failures)
{
    auto io = cnetmod::make_io_context();
    cnetmod::socket socket;
    cnetmod::http::response response;
    cnetmod::http::header_map headers;
    cnetmod::http::request_context context{*io, socket, "POST", "/demo.Echo/Say",
        headers, {}, response, {}};
    cnetmod::http::handler_fn raw = [](cnetmod::http::request_context& request)
        -> cnetmod::task<void>
    {
        request.resp() = cnetmod::grpc::make_status_response({
            .code = cnetmod::grpc::status_code::ok,
        });
        co_return;
    };
    const auto observed = cnetmod::observability::grpc_server_handler(raw, [](const cnetmod::instrumentation::completed_span&)
        {
            throw std::runtime_error("span export failed");
        },
        [](cnetmod::instrumentation::metric_measurement)
        {
            throw std::runtime_error("metric export failed");
        });

    cnetmod::sync_wait(observed(context));

    ASSERT_EQ(context.resp().trailers().at("grpc-status"), "0");
}

TEST(grpc_client_observation_owns_metric_metadata_for_every_call_shape)
{
    auto io = cnetmod::make_io_context();
    cnetmod::grpc::client raw{*io, "invalid://grpc-observation"};
    std::vector<cnetmod::instrumentation::completed_span> spans;
    std::vector<cnetmod::instrumentation::metric_measurement> measurements;
    cnetmod::observability::instrumented_grpc_client observed{raw,
        [&](const cnetmod::instrumentation::completed_span& span)
        {
            spans.push_back(span);
        },
        [&](cnetmod::instrumentation::metric_measurement measurement)
        {
            measurements.push_back(std::move(measurement));
        }};
    const auto parent = cnetmod::instrumentation::new_root_context();
    const std::string service{"example.observation.LongServiceName"};
    const std::string method{"LongMethodNameThatOutlivesTheMovedRequest"};
    const auto unary = [&]
    {
        return cnetmod::grpc::unary_request{.service = service, .method = method};
    };
    const auto streaming = [&]
    {
        return cnetmod::grpc::streaming_request{.service = service, .method = method};
    };

    ASSERT_FALSE(cnetmod::sync_wait(observed.unary(unary(), parent)).has_value());
    ASSERT_FALSE(cnetmod::sync_wait(observed.client_streaming(streaming(), parent)).has_value());
    ASSERT_FALSE(cnetmod::sync_wait(observed.server_streaming(unary(), parent)).has_value());
    ASSERT_FALSE(cnetmod::sync_wait(observed.bidi_streaming(streaming(), parent)).has_value());

    ASSERT_EQ(spans.size(), std::size_t{4});
    ASSERT_EQ(measurements.size(), std::size_t{4});
    for (const auto& span : spans)
    {
        ASSERT_EQ(span.context.trace_id, parent.trace_id);
        ASSERT_EQ(span.parent_span_id, parent.span_id);
        ASSERT_TRUE(span.kind == cnetmod::instrumentation::span_kind::client);
    }
    for (const auto& measurement : measurements)
    {
        ASSERT_EQ(measurement.name, "rpc.client.duration");
        const auto service_attribute = std::ranges::find_if(measurement.attributes,
            [](const auto& attribute)
            {
                return attribute.first == "rpc.service";
            });
        const auto method_attribute = std::ranges::find_if(measurement.attributes,
            [](const auto& attribute)
            {
                return attribute.first == "rpc.method";
            });
        ASSERT_TRUE(service_attribute != measurement.attributes.end());
        ASSERT_TRUE(method_attribute != measurement.attributes.end());
        ASSERT_EQ(service_attribute->second, service);
        ASSERT_EQ(method_attribute->second, method);
    }
}

TEST(grpc_client_observation_preserves_cancelled_terminal_status)
{
    auto io = cnetmod::make_io_context();
    cnetmod::grpc::client raw{*io, "invalid://grpc-observation"};
    std::vector<cnetmod::instrumentation::completed_span> spans;
    std::vector<cnetmod::instrumentation::metric_measurement> measurements;
    cnetmod::observability::instrumented_grpc_client observed{raw,
        [&](const cnetmod::instrumentation::completed_span& span)
        {
            spans.push_back(span);
        },
        [&](cnetmod::instrumentation::metric_measurement measurement)
        {
            measurements.push_back(std::move(measurement));
        }};
    cnetmod::cancel_token cancellation;
    cancellation.cancel();
    const auto result = cnetmod::sync_wait(observed.unary({
                                                              .service = "example.observation.Service",
                                                              .method = "CancelledPath",
                                                          },
        cnetmod::instrumentation::new_root_context(), cancellation));

    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(result.error().code == cnetmod::grpc::status_code::cancelled);
    ASSERT_EQ(spans.size(), std::size_t{1});
    ASSERT_TRUE(spans.front().result.status ==
        cnetmod::instrumentation::operation_status::cancelled);
    ASSERT_EQ(measurements.size(), std::size_t{1});
    const auto status = std::ranges::find_if(measurements.front().attributes,
        [](const auto& attribute)
        {
            return attribute.first == "rpc.grpc.status_code";
        });
    ASSERT_TRUE(status != measurements.front().attributes.end());
    ASSERT_EQ(status->second, "1");
}

RUN_TESTS()
