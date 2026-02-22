/// cnetmod unit tests — HTTP request builder and serialization

#include "test_framework.hpp"

import cnetmod.protocol.http;

using namespace cnetmod::http;

// =============================================================================
// request builder
// =============================================================================

TEST(request_default_values) {
    request req;
    ASSERT_EQ(static_cast<int>(req.method()), static_cast<int>(http_method::GET));
    ASSERT_EQ(req.uri(), std::string_view("/"));
    ASSERT_EQ(static_cast<int>(req.version()), static_cast<int>(http_version::http_1_1));
    ASSERT_TRUE(req.body().empty());
}

TEST(request_constructor) {
    request req(http_method::POST, "/api/data");
    ASSERT_EQ(static_cast<int>(req.method()), static_cast<int>(http_method::POST));
    ASSERT_EQ(req.uri(), std::string_view("/api/data"));
}

TEST(request_set_method) {
    request req;
    req.set_method(http_method::PUT);
    ASSERT_EQ(static_cast<int>(req.method()), static_cast<int>(http_method::PUT));
}

TEST(request_set_uri) {
    request req;
    req.set_uri("/new/path?q=1");
    ASSERT_EQ(req.uri(), std::string_view("/new/path?q=1"));
}

TEST(request_set_header) {
    request req;
    req.set_header("Host", "example.com");
    ASSERT_EQ(req.get_header("Host"), std::string_view("example.com"));
}

TEST(request_append_header) {
    request req;
    req.set_header("Accept", "text/html");
    req.append_header("Accept", "application/json");
    ASSERT_EQ(req.get_header("Accept"), std::string_view("text/html, application/json"));
}

TEST(request_remove_header) {
    request req;
    req.set_header("X-Custom", "value");
    ASSERT_EQ(req.get_header("X-Custom"), std::string_view("value"));
    req.remove_header("X-Custom");
    ASSERT_TRUE(req.get_header("X-Custom").empty());
}

TEST(request_get_header_missing) {
    request req;
    ASSERT_TRUE(req.get_header("Nonexistent").empty());
}

// =============================================================================
// set_body auto Content-Length
// =============================================================================

TEST(request_set_body_string_view) {
    request req;
    req.set_body("hello");
    ASSERT_EQ(req.body(), std::string_view("hello"));
    ASSERT_EQ(req.get_header("Content-Length"), std::string_view("5"));
}

TEST(request_set_body_string) {
    request req;
    req.set_body(std::string("world!"));
    ASSERT_EQ(req.body(), std::string_view("world!"));
    ASSERT_EQ(req.get_header("Content-Length"), std::string_view("6"));
}

TEST(request_set_body_empty) {
    request req;
    req.set_body("");
    ASSERT_TRUE(req.body().empty());
    ASSERT_EQ(req.get_header("Content-Length"), std::string_view("0"));
}

// =============================================================================
// serialize
// =============================================================================

TEST(request_serialize_get) {
    request req(http_method::GET, "/index.html");
    req.set_header("Host", "example.com");
    req.set_header("Connection", "close");

    auto s = req.serialize();
    // Must start with request line
    ASSERT_TRUE(s.starts_with("GET /index.html HTTP/1.1\r\n"));
    // Must end with empty line (no body)
    ASSERT_TRUE(s.ends_with("\r\n\r\n"));
    // Must contain headers
    ASSERT_TRUE(s.find("Host: example.com\r\n") != std::string::npos);
    ASSERT_TRUE(s.find("Connection: close\r\n") != std::string::npos);
}

TEST(request_serialize_post_with_body) {
    request req(http_method::POST, "/api/echo");
    req.set_header("Host", "localhost");
    req.set_body("payload");

    auto s = req.serialize();
    ASSERT_TRUE(s.starts_with("POST /api/echo HTTP/1.1\r\n"));
    ASSERT_TRUE(s.find("Content-Length: 7\r\n") != std::string::npos);
    ASSERT_TRUE(s.ends_with("payload"));
}

TEST(request_serialize_http10) {
    request req(http_method::GET, "/", http_version::http_1_0);
    auto s = req.serialize();
    ASSERT_TRUE(s.starts_with("GET / HTTP/1.0\r\n"));
}

RUN_TESTS()
