#include "test_framework.hpp"

import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

TEST(sse_encodes_named_multiline_event) {
    auto frame = sse::encode(sse::event{
        .event = "update",
        .data = "line1\nline2",
        .id = "42",
        .retry = std::chrono::milliseconds{1500},
    });

    ASSERT_EQ(frame, std::string(
        "id: 42\n"
        "event: update\n"
        "retry: 1500\n"
        "data: line1\n"
        "data: line2\n"
        "\n"));
}

TEST(sse_heartbeat_is_comment_frame) {
    ASSERT_EQ(sse::heartbeat(), std::string(": keepalive\n\n"));
}

TEST(sse_prepare_sets_streaming_headers) {
    response resp;
    resp.set_body(std::string_view("old"));
    sse::prepare(resp);

    ASSERT_EQ(resp.get_header("Content-Type"), std::string_view("text/event-stream; charset=utf-8"));
    ASSERT_EQ(resp.get_header("Cache-Control"), std::string_view("no-cache, no-transform"));
    ASSERT_EQ(resp.get_header("Connection"), std::string_view("keep-alive"));
    ASSERT_EQ(resp.get_header("X-Accel-Buffering"), std::string_view("no"));
    ASSERT_EQ(resp.get_header("X-Streamed"), std::string_view("1"));
    ASSERT_TRUE(resp.get_header("Content-Length").empty());
}

TEST(sse_make_response_omits_content_length) {
    std::array events{
        sse::event{.event = "message", .data = "hello", .id = "1"},
        sse::event{.data = "world"},
    };

    auto resp = sse::make_response(std::span<const sse::event>{events.data(), events.size()});
    ASSERT_EQ(resp.status_code(), 200);
    ASSERT_TRUE(resp.get_header("Content-Length").empty());
    ASSERT_TRUE(resp.body().find("event: message\n") != std::string_view::npos);
    ASSERT_TRUE(resp.body().find("data: world\n\n") != std::string_view::npos);
}

RUN_TESTS()
