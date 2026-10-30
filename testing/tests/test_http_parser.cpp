/// cnetmod unit tests — HTTP request_parser and response_parser

#include "test_framework.hpp"

import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

TEST(request_parser_transfers_headers_without_losing_parsed_state)
{
    request_parser parser(32);
    const std::string raw = "GET /ws HTTP/1.1\r\nHost: localhost\r\nX-Value: first\r\nX-Value: second\r\n\r\n";
    ASSERT_TRUE(parser.consume(raw.data(), raw.size()).has_value());
    auto owned = parser.take_headers();
    ASSERT_EQ(owned.at("X-Value"), std::string("first, second"));
    ASSERT_EQ(parser.get_header("Host"), std::string_view("localhost"));
    ASSERT_EQ(parser.get_header("X-Value"), std::string_view("first, second"));
    ASSERT_EQ(parser.headers().at("Host"), std::string("localhost"));
    ASSERT_TRUE(parser.ready());
    parser.reset();
    ASSERT_EQ(owned.at("Host"), std::string("localhost"));
}

TEST(request_parser_transfers_compact_upgrade_headers)
{
    request_parser parser(32);
    const std::string raw =
        "GET /ws?room=one HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "X-Value: first\r\n"
        "X-Value: second\r\n\r\n"
        "buffered-frame";
    ASSERT_TRUE(parser.consume(raw.data(), raw.size()).has_value());

    auto snapshot = parser.take_header_snapshot();
    ASSERT_EQ(snapshot.get("host"), std::string_view("localhost"));
    ASSERT_EQ(snapshot.get("X-VALUE"), std::string_view("first, second"));
    ASSERT_EQ(snapshot.headers().at("X-Value"),
        std::string("first, second"));
    ASSERT_EQ(parser.take_buffered_data(), std::string("buffered-frame"));
}

// =============================================================================
// request_parser — Basic GET
// =============================================================================

TEST(request_parser_simple_get)
{
    request_parser p;
    std::string raw = "GET /index.html HTTP/1.1\r\n" "Host: example.com\r\n" "Connection: close\r\n" "\r\n";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.method(), std::string_view("GET"));
    ASSERT_EQ(p.uri(), std::string_view("/index.html"));
    ASSERT_EQ(static_cast<int>(p.version()), static_cast<int>(http_version::http_1_1));
    ASSERT_EQ(p.get_header("Host"), std::string_view("example.com"));
    ASSERT_EQ(p.get_header("Connection"), std::string_view("close"));
    ASSERT_TRUE(p.body().empty());
}

TEST(request_parser_http_1_0)
{
    request_parser p;
    std::string raw = "GET / HTTP/1.0\r\n\r\n";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(static_cast<int>(p.version()), static_cast<int>(http_version::http_1_0));
}

TEST(request_parser_header_views_support_common_and_custom_names)
{
    request_parser parser;
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "hOsT: example.com\r\n"
        "Authorization: Bearer token\r\n"
        "X-Request-Feature: enabled\r\n\r\n";

    ASSERT_TRUE(parser.consume(request.data(), request.size()).has_value());
    ASSERT_TRUE(parser.ready());
    ASSERT_EQ(parser.get_header("HOST"), std::string_view("example.com"));
    ASSERT_EQ(parser.get_header("authorization"),
        std::string_view("Bearer token"));
    ASSERT_EQ(parser.get_header("x-request-feature"),
        std::string_view("enabled"));
    ASSERT_TRUE(parser.get_header("missing").empty());

    const auto& headers = parser.headers();
    ASSERT_EQ(headers.at("host"), std::string("example.com"));
    ASSERT_EQ(headers.at("X-REQUEST-FEATURE"), std::string("enabled"));
}

TEST(request_parser_duplicate_headers_preserve_legacy_joining)
{
    request_parser parser;
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "X-Tag: one\r\n"
        "x-tag: two\r\n\r\n";

    ASSERT_TRUE(parser.consume(request.data(), request.size()).has_value());
    ASSERT_TRUE(parser.ready());
    ASSERT_EQ(parser.get_header("X-TAG"), std::string_view("one, two"));
    ASSERT_EQ(parser.headers().at("x-tag"), std::string("one, two"));
}

TEST(request_parser_all_common_header_slots_are_addressable)
{
    constexpr std::array<std::string_view, 18> names{
        "Host", "Connection", "Content-Length", "Transfer-Encoding",
        "Content-Type", "Authorization", "Accept-Encoding", "Accept", "Range",
        "Origin", "Forwarded", "X-Forwarded-For", "X-Real-IP",
        "Traceparent", "Tracestate", "User-Agent", "Cookie", "Upgrade"};
    std::string request = "GET / HTTP/1.1\r\n";
    for (std::size_t index{}; index < names.size(); ++index)
        request += std::format("{}: value-{}\r\n", names[index], index);
    request += "\r\n";

    request_parser parser;
    ASSERT_TRUE(parser.consume(request.data(), request.size()).has_value());
    ASSERT_TRUE(parser.ready());
    for (std::size_t index{}; index < names.size(); ++index)
    {
        const auto expected = std::format("value-{}", index);
        ASSERT_EQ(parser.get_header(names[index]), std::string_view(expected));
    }
}

TEST(request_parser_fragmented_headers_keep_stable_views)
{
    request_parser parser;
    std::string large_value(6000, 'v');
    std::string request =
        "GET /fragmented HTTP/1.1\r\nHost: fragmented.example\r\n"
        "X-Large: " + large_value + "\r\nX-Tail: retained\r\n\r\n";

    for (const char byte : request)
        ASSERT_TRUE(parser.consume(&byte, 1).has_value());

    ASSERT_TRUE(parser.ready());
    ASSERT_EQ(parser.get_header("host"),
        std::string_view("fragmented.example"));
    ASSERT_EQ(parser.get_header("x-large"), std::string_view(large_value));
    ASSERT_EQ(parser.get_header("X-Tail"), std::string_view("retained"));
}

// =============================================================================
// request_parser — POST with body
// =============================================================================

TEST(request_parser_post_with_body)
{
    request_parser p;
    std::string raw = "POST /api/data HTTP/1.1\r\n" "Host: localhost\r\n" "Content-Length: 13\r\n" "\r\n" "Hello, World!";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.method(), std::string_view("POST"));
    ASSERT_EQ(p.uri(), std::string_view("/api/data"));
    ASSERT_EQ(p.body(), std::string_view("Hello, World!"));
    ASSERT_EQ(p.get_header("Content-Length"), std::string_view("13"));
}

// =============================================================================
// request_parser — Incremental feeding
// =============================================================================

TEST(request_parser_incremental)
{
    request_parser p;

    std::string part1 = "GET /path HTTP/1.1\r\n";
    std::string part2 = "Host: test.com\r\n\r\n";

    auto r1 = p.consume(part1.data(), part1.size());
    ASSERT_TRUE(r1.has_value());
    ASSERT_FALSE(p.ready());

    auto r2 = p.consume(part2.data(), part2.size());
    ASSERT_TRUE(r2.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.uri(), std::string_view("/path"));
}

// =============================================================================
// request_parser — Chunked transfer encoding
// =============================================================================

TEST(request_parser_chunked)
{
    request_parser p;
    std::string raw = "POST /upload HTTP/1.1\r\n" "Host: localhost\r\n" "Transfer-Encoding: chunked\r\n" "\r\n" "5\r\n" "Hello\r\n" "7\r\n" ", World\r\n" "0\r\n" "\r\n";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.body(), std::string_view("Hello, World"));
}

TEST(request_parser_fragmented_chunked_body_survives_cursor_compaction)
{
    request_parser parser;
    std::string payload(12 * 1024, 'x');
    std::string request = "POST /upload HTTP/1.1\r\n" "Host: localhost\r\n" "Transfer-Encoding: chunked\r\n\r\n";
    request += std::format("{:x}\r\n", payload.size());
    request += payload;
    request += "\r\n0\r\n\r\n";

    for (char byte : request)
    {
        auto consumed = parser.consume(&byte, 1);
        ASSERT_TRUE(consumed.has_value());
    }

    ASSERT_TRUE(parser.ready());
    ASSERT_EQ(parser.body(), std::string_view(payload));
}

TEST(request_parser_waits_for_complete_chunked_terminator)
{
    request_parser parser;
    const std::string prefix =
        "POST /upload HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Transfer-Encoding: chunked\r\n\r\n"
        "2\r\n20\r\n0\r\n";

    ASSERT_TRUE(parser.consume(prefix.data(), prefix.size()).has_value());
    ASSERT_FALSE(parser.ready());

    constexpr char carriage_return = '\r';
    ASSERT_TRUE(parser.consume(&carriage_return, 1).has_value());
    ASSERT_FALSE(parser.ready());

    constexpr char line_feed = '\n';
    ASSERT_TRUE(parser.consume(&line_feed, 1).has_value());
    ASSERT_TRUE(parser.ready());
    ASSERT_EQ(parser.body(), std::string_view("20"));
}

TEST(request_parser_preserves_pipelined_requests)
{
    request_parser parser;
    const std::string requests =
        "GET /first HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "POST /second HTTP/1.1\r\nHost: localhost\r\n"
        "Content-Length: 3\r\n\r\ntwo"
        "GET /third HTTP/1.1\r\nHost: localhost\r\n\r\n";

    ASSERT_TRUE(parser.consume(requests.data(), requests.size()).has_value());
    ASSERT_TRUE(parser.ready());
    ASSERT_EQ(parser.uri(), std::string_view("/first"));
    ASSERT_TRUE(parser.has_buffered_data());

    ASSERT_TRUE(parser.reset_for_next_message());
    ASSERT_TRUE(parser.consume_buffered().has_value());
    ASSERT_TRUE(parser.ready());
    ASSERT_EQ(parser.uri(), std::string_view("/second"));
    ASSERT_EQ(parser.body(), std::string_view("two"));

    ASSERT_TRUE(parser.reset_for_next_message());
    ASSERT_TRUE(parser.consume_buffered().has_value());
    ASSERT_TRUE(parser.ready());
    ASSERT_EQ(parser.uri(), std::string_view("/third"));
    ASSERT_FALSE(parser.has_buffered_data());
    ASSERT_FALSE(parser.reset_for_next_message());
}

TEST(request_parser_parses_long_pipelined_batches_in_place)
{
    // Enough requests to cross the in-place compaction threshold, each with a
    // custom header (rebased offsets) and every third one with a body, plus a
    // final request split across two reads.
    request_parser parser;
    std::string batch;
    constexpr int count = 400;
    for (int index = 0; index < count; ++index)
    {
        batch += std::format("{} /item/{} HTTP/1.1\r\nHost: localhost\r\nX-Index: {}\r\n",
            index % 3 == 0 ? "POST" : "GET", index, index);
        if (index % 3 == 0)
            batch += std::format("Content-Length: {}\r\n\r\nbody{}", 4 + std::to_string(index).size(), index);
        else
            batch += "\r\n";
    }
    const std::string tail = "GET /last HTTP/1.1\r\nHost: localhost\r\nX-Index: last\r\n\r\n";
    batch += tail.substr(0, 20);

    ASSERT_TRUE(parser.consume(batch.data(), batch.size()).has_value());
    for (int index = 0; index < count; ++index)
    {
        if (index != 0)
        {
            ASSERT_TRUE(parser.reset_for_next_message());
            ASSERT_TRUE(parser.consume_buffered().has_value());
        }
        ASSERT_TRUE(parser.ready());
        ASSERT_EQ(parser.method(), std::string_view(index % 3 == 0 ? "POST" : "GET"));
        ASSERT_EQ(parser.uri(), std::string_view(std::format("/item/{}", index)));
        ASSERT_EQ(parser.get_header("Host"), std::string_view("localhost"));
        ASSERT_EQ(parser.get_header("X-Index"), std::string_view(std::to_string(index)));
        if (index % 3 == 0)
            ASSERT_EQ(parser.body(), std::string_view(std::format("body{}", index)));
        else
            ASSERT_TRUE(parser.body().empty());
    }
    ASSERT_TRUE(parser.reset_for_next_message());
    ASSERT_TRUE(parser.consume_buffered().has_value());
    ASSERT_FALSE(parser.ready());
    ASSERT_TRUE(parser.consume(tail.data() + 20, tail.size() - 20).has_value());
    ASSERT_TRUE(parser.ready());
    ASSERT_EQ(parser.uri(), std::string_view("/last"));
    ASSERT_EQ(parser.get_header("X-Index"), std::string_view("last"));
    ASSERT_FALSE(parser.reset_for_next_message());
}

// =============================================================================
// request_parser — URI with query string
// =============================================================================

TEST(request_parser_uri_with_query)
{
    request_parser p;
    std::string raw = "GET /search?q=hello&page=1 HTTP/1.1\r\n" "Host: example.com\r\n" "\r\n";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.uri(), std::string_view("/search?q=hello&page=1"));
}

// =============================================================================
// request_parser — method_enum
// =============================================================================

TEST(request_parser_method_enum)
{
    request_parser p;
    std::string raw = "DELETE /resource/42 HTTP/1.1\r\n\r\n";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    auto me = p.method_enum();
    ASSERT_TRUE(me.has_value());
    ASSERT_EQ(static_cast<int>(*me), static_cast<int>(http_method::DELETE_));
}

TEST(request_parser_method_enum_tracks_pipelined_messages_and_extensions)
{
    request_parser parser;
    const std::string raw =
        "GET /first HTTP/1.1\r\n\r\n"
        "PROPFIND /second HTTP/1.1\r\n\r\n";

    ASSERT_TRUE(parser.consume(raw.data(), raw.size()).has_value());
    ASSERT_TRUE(parser.ready());
    const auto first_method = parser.method_enum();
    ASSERT_TRUE(first_method.has_value());
    ASSERT_EQ(static_cast<int>(*first_method),
        static_cast<int>(http_method::GET));

    ASSERT_TRUE(parser.reset_for_next_message());
    ASSERT_TRUE(parser.consume_buffered().has_value());
    ASSERT_TRUE(parser.ready());
    ASSERT_EQ(parser.method(), std::string_view{"PROPFIND"});
    ASSERT_FALSE(parser.method_enum().has_value());
}

// =============================================================================
// request_parser — reset
// =============================================================================

TEST(request_parser_reset)
{
    request_parser p;
    std::string raw1 = "GET /first HTTP/1.1\r\n\r\n";
    ASSERT_TRUE(p.consume(raw1.data(), raw1.size()).has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.uri(), std::string_view("/first"));

    p.reset();
    ASSERT_FALSE(p.ready());

    std::string raw2 = "GET /second HTTP/1.1\r\n\r\n";
    ASSERT_TRUE(p.consume(raw2.data(), raw2.size()).has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.uri(), std::string_view("/second"));
}

// =============================================================================
// request_parser — Multiple headers, same key
// =============================================================================

TEST(request_parser_multi_header)
{
    request_parser p;
    std::string raw = "GET / HTTP/1.1\r\n" "Accept: text/html\r\n" "Accept: application/json\r\n" "\r\n";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    // Multi-value headers are appended with ", "
    ASSERT_EQ(p.get_header("Accept"),
        std::string_view("text/html, application/json"));
}

// =============================================================================
// response_parser — 200 OK with body
// =============================================================================

TEST(response_parser_200_ok)
{
    response_parser p;
    std::string raw = "HTTP/1.1 200 OK\r\n" "Content-Type: application/json\r\n" "Content-Length: 27\r\n" "\r\n" R"({"message":"Hello, World!"})";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.status_code(), 200);
    ASSERT_EQ(p.status_message(), std::string_view("OK"));
    ASSERT_EQ(p.get_header("Content-Type"), std::string_view("application/json"));
    ASSERT_EQ(p.body(), std::string_view(R"({"message":"Hello, World!"})"));
}

TEST(response_parser_404)
{
    response_parser p;
    std::string raw = "HTTP/1.1 404 Not Found\r\n" "Content-Length: 0\r\n" "\r\n";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.status_code(), 404);
    ASSERT_EQ(p.status_message(), std::string_view("Not Found"));
    ASSERT_TRUE(p.body().empty());
}

TEST(response_parser_no_body)
{
    response_parser p;
    std::string raw = "HTTP/1.1 204 No Content\r\n\r\n";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.status_code(), 204);
    ASSERT_TRUE(p.body().empty());
}

// =============================================================================
// response_parser — Chunked
// =============================================================================

TEST(response_parser_chunked)
{
    response_parser p;
    std::string raw = "HTTP/1.1 200 OK\r\n" "Transfer-Encoding: chunked\r\n" "\r\n" "d\r\n" "Hello, World!\r\n" "0\r\n" "\r\n";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.body(), std::string_view("Hello, World!"));
}

TEST(response_parser_waits_for_complete_chunked_terminator)
{
    response_parser parser;
    const std::string prefix =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n\r\n"
        "2\r\nOK\r\n0\r\n";

    ASSERT_TRUE(parser.consume(prefix.data(), prefix.size()).has_value());
    ASSERT_FALSE(parser.ready());

    constexpr char carriage_return = '\r';
    ASSERT_TRUE(parser.consume(&carriage_return, 1).has_value());
    ASSERT_FALSE(parser.ready());

    constexpr char line_feed = '\n';
    ASSERT_TRUE(parser.consume(&line_feed, 1).has_value());
    ASSERT_TRUE(parser.ready());
    ASSERT_EQ(parser.body(), std::string_view("OK"));
}

TEST(response_parser_fragmented_headers_survive_cursor_compaction)
{
    response_parser parser;
    std::string response = "HTTP/1.1 200 OK\r\n";
    for (int index = 0; index < 160; ++index)
        response += std::format("X-Long-Header-{}: {}\r\n", index,
            std::string(48, static_cast<char>('a' + index % 26)));
    response += "Content-Length: 2\r\n\r\nOK";

    constexpr std::size_t fragment_size = 31;
    for (std::size_t offset = 0; offset < response.size();
        offset += fragment_size)
    {
        const auto size = std::min(fragment_size, response.size() - offset);
        auto consumed = parser.consume(response.data() + offset, size);
        ASSERT_TRUE(consumed.has_value());
    }

    ASSERT_TRUE(parser.ready());
    ASSERT_EQ(parser.get_header("X-Long-Header-159"),
        std::string_view(std::string(48, 'd')));
    ASSERT_EQ(parser.body(), std::string_view("OK"));
}

// =============================================================================
// response_parser — reset
// =============================================================================

TEST(response_parser_reset)
{
    response_parser p;
    std::string raw1 = "HTTP/1.1 200 OK\r\n\r\n";
    ASSERT_TRUE(p.consume(raw1.data(), raw1.size()).has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.status_code(), 200);

    p.reset();
    ASSERT_FALSE(p.ready());

    std::string raw2 = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
    ASSERT_TRUE(p.consume(raw2.data(), raw2.size()).has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.status_code(), 500);
}

// =============================================================================
// Round-trip: build request → serialize → parse
// =============================================================================

TEST(roundtrip_request_serialize_parse)
{
    request req(http_method::POST, "/api/test");
    req.set_header("Host", "localhost");
    req.set_header("X-Custom", "value");
    req.set_body(std::string_view("test body"));

    auto raw = req.serialize();

    request_parser p;
    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.method(), std::string_view("POST"));
    ASSERT_EQ(p.uri(), std::string_view("/api/test"));
    ASSERT_EQ(p.get_header("Host"), std::string_view("localhost"));
    ASSERT_EQ(p.get_header("X-Custom"), std::string_view("value"));
    ASSERT_EQ(p.body(), std::string_view("test body"));
}

TEST(roundtrip_response_serialize_parse)
{
    response resp(200);
    resp.set_header("Server", "cnetmod");
    resp.set_header("Content-Type", "text/plain");
    resp.set_body(std::string_view("OK"));

    auto raw = resp.serialize();

    response_parser p;
    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.status_code(), 200);
    ASSERT_EQ(p.get_header("Server"), std::string_view("cnetmod"));
    ASSERT_EQ(p.body(), std::string_view("OK"));
}

TEST(upgrade_parser_preserves_binary_tail)
{
    const std::string tail{"\x82\x03\x00\x01\xff", 5};
    request_parser request;
    const auto header = std::string{"GET /ws HTTP/1.1\r\nHost: localhost\r\n\r\n"};
    ASSERT_TRUE(request.consume(header.data(), 7).has_value());
    ASSERT_TRUE(request.take_buffered_data().empty());
    const auto rest = header.substr(7) + tail;
    ASSERT_TRUE(request.consume(rest.data(), rest.size()).has_value());
    ASSERT_TRUE(request.ready());
    ASSERT_EQ(request.take_buffered_data(), tail);
    ASSERT_TRUE(request.take_buffered_data().empty());
    ASSERT_EQ(request.get_header("Host"), std::string_view{"localhost"});

    response_parser response;
    const auto raw = std::string{"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n\r\n"} + tail;
    ASSERT_TRUE(response.consume(raw.data(), raw.size()).has_value());
    ASSERT_TRUE(response.ready());
    ASSERT_EQ(response.take_buffered_data(), tail);
    ASSERT_TRUE(response.take_buffered_data().empty());
    ASSERT_EQ(response.get_header("Upgrade"), std::string_view{"websocket"});
}

RUN_TESTS()
