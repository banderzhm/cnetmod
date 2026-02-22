/// cnetmod unit tests — HTTP request_parser and response_parser

#include "test_framework.hpp"

import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

// =============================================================================
// request_parser — Basic GET
// =============================================================================

TEST(request_parser_simple_get) {
    request_parser p;
    std::string raw = "GET /index.html HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "Connection: close\r\n"
                      "\r\n";

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

TEST(request_parser_http_1_0) {
    request_parser p;
    std::string raw = "GET / HTTP/1.0\r\n\r\n";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(static_cast<int>(p.version()), static_cast<int>(http_version::http_1_0));
}

// =============================================================================
// request_parser — POST with body
// =============================================================================

TEST(request_parser_post_with_body) {
    request_parser p;
    std::string raw = "POST /api/data HTTP/1.1\r\n"
                      "Host: localhost\r\n"
                      "Content-Length: 13\r\n"
                      "\r\n"
                      "Hello, World!";

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

TEST(request_parser_incremental) {
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

TEST(request_parser_chunked) {
    request_parser p;
    std::string raw = "POST /upload HTTP/1.1\r\n"
                      "Host: localhost\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "\r\n"
                      "5\r\n"
                      "Hello\r\n"
                      "7\r\n"
                      ", World\r\n"
                      "0\r\n"
                      "\r\n";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.body(), std::string_view("Hello, World"));
}

// =============================================================================
// request_parser — URI with query string
// =============================================================================

TEST(request_parser_uri_with_query) {
    request_parser p;
    std::string raw = "GET /search?q=hello&page=1 HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "\r\n";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.uri(), std::string_view("/search?q=hello&page=1"));
}

// =============================================================================
// request_parser — method_enum
// =============================================================================

TEST(request_parser_method_enum) {
    request_parser p;
    std::string raw = "DELETE /resource/42 HTTP/1.1\r\n\r\n";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    auto me = p.method_enum();
    ASSERT_TRUE(me.has_value());
    ASSERT_EQ(static_cast<int>(*me), static_cast<int>(http_method::DELETE_));
}

// =============================================================================
// request_parser — reset
// =============================================================================

TEST(request_parser_reset) {
    request_parser p;
    std::string raw1 = "GET /first HTTP/1.1\r\n\r\n";
    p.consume(raw1.data(), raw1.size());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.uri(), std::string_view("/first"));

    p.reset();
    ASSERT_FALSE(p.ready());

    std::string raw2 = "GET /second HTTP/1.1\r\n\r\n";
    p.consume(raw2.data(), raw2.size());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.uri(), std::string_view("/second"));
}

// =============================================================================
// request_parser — Multiple headers, same key
// =============================================================================

TEST(request_parser_multi_header) {
    request_parser p;
    std::string raw = "GET / HTTP/1.1\r\n"
                      "Accept: text/html\r\n"
                      "Accept: application/json\r\n"
                      "\r\n";

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

TEST(response_parser_200_ok) {
    response_parser p;
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: 27\r\n"
                      "\r\n"
                      R"({"message":"Hello, World!"})";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.status_code(), 200);
    ASSERT_EQ(p.status_message(), std::string_view("OK"));
    ASSERT_EQ(p.get_header("Content-Type"), std::string_view("application/json"));
    ASSERT_EQ(p.body(), std::string_view(R"({"message":"Hello, World!"})"));
}

TEST(response_parser_404) {
    response_parser p;
    std::string raw = "HTTP/1.1 404 Not Found\r\n"
                      "Content-Length: 0\r\n"
                      "\r\n";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.status_code(), 404);
    ASSERT_EQ(p.status_message(), std::string_view("Not Found"));
    ASSERT_TRUE(p.body().empty());
}

TEST(response_parser_no_body) {
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

TEST(response_parser_chunked) {
    response_parser p;
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "\r\n"
                      "d\r\n"
                      "Hello, World!\r\n"
                      "0\r\n"
                      "\r\n";

    auto r = p.consume(raw.data(), raw.size());
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.body(), std::string_view("Hello, World!"));
}

// =============================================================================
// response_parser — reset
// =============================================================================

TEST(response_parser_reset) {
    response_parser p;
    std::string raw1 = "HTTP/1.1 200 OK\r\n\r\n";
    p.consume(raw1.data(), raw1.size());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.status_code(), 200);

    p.reset();
    ASSERT_FALSE(p.ready());

    std::string raw2 = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
    p.consume(raw2.data(), raw2.size());
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.status_code(), 500);
}

// =============================================================================
// Round-trip: build request → serialize → parse
// =============================================================================

TEST(roundtrip_request_serialize_parse) {
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

TEST(roundtrip_response_serialize_parse) {
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

RUN_TESTS()
