/// cnetmod unit tests — HTTP response builder and serialization

#include "test_framework.hpp"

import std;
import cnetmod.protocol.http;
import cnetmod.utils;

using namespace cnetmod::http;
using cnetmod::utils::R;

TEST(application_result_http_adapter)
{
    auto success = to_http_response(R<std::string>::ok("alice"),
        [](const std::string& value) { return R"({"user":")" + value + R"("})"; },
        [](const auto& error) { return R"({"error":")" + error.message + R"("})"; });
    ASSERT_EQ(success.status_code(), status::ok);
    ASSERT_EQ(success.get_header("Content-Type"), std::string_view("application/json"));
    ASSERT_EQ(success.body(), std::string_view(R"({"user":"alice"})"));

    auto failure = to_http_response(R<std::string>::error(404, "missing"),
        [](const std::string& value) { return value; },
        [](const auto& error) { return R"({"error":")" + error.message + R"("})"; },
        {.error_status = status::not_found});
    ASSERT_EQ(failure.status_code(), status::not_found);
    ASSERT_EQ(failure.body(), std::string_view(R"({"error":"missing"})"));

    auto no_content = to_http_response(R<void>::success(),
        [] { return std::string{}; },
        [](const auto& error) { return error.message; },
        {.success_status = status::no_content, .content_type = "text/plain"});
    ASSERT_EQ(no_content.status_code(), status::no_content);
    ASSERT_EQ(no_content.get_header("Content-Type"), std::string_view("text/plain"));
    ASSERT_TRUE(no_content.body().empty());
}

// =============================================================================
// response builder
// =============================================================================

TEST(response_default_status)
{
    response resp;
    ASSERT_EQ(resp.status_code(), 200);
    ASSERT_EQ(static_cast<int>(resp.version()), static_cast<int>(http_version::http_1_1));
    ASSERT_TRUE(resp.body().empty());
}

TEST(response_constructor_status)
{
    response resp(404);
    ASSERT_EQ(resp.status_code(), 404);
}

TEST(response_set_status)
{
    response resp;
    resp.set_status(301);
    ASSERT_EQ(resp.status_code(), 301);
}

TEST(response_set_status_message)
{
    response resp(200);
    resp.set_status_message("Custom OK");
    auto s = resp.serialize();
    ASSERT_TRUE(s.find("200 Custom OK\r\n") != std::string::npos);
}

TEST(response_set_header)
{
    response resp;
    resp.set_header("Content-Type", "text/plain");
    ASSERT_EQ(resp.get_header("Content-Type"), std::string_view("text/plain"));
}

TEST(response_append_header)
{
    response resp;
    resp.set_header("Set-Cookie", "a=1");
    resp.append_header("Set-Cookie", "b=2");
    ASSERT_EQ(resp.get_header("Set-Cookie"), std::string_view("a=1, b=2"));
}

TEST(response_remove_header)
{
    response resp;
    resp.set_header("X-Custom", "val");
    resp.remove_header("X-Custom");
    ASSERT_TRUE(resp.get_header("X-Custom").empty());
}

TEST(response_get_header_missing)
{
    response resp;
    ASSERT_TRUE(resp.get_header("Nonexistent").empty());
}

// =============================================================================
// set_body auto Content-Length
// =============================================================================

TEST(response_set_body_string_view)
{
    response resp;
    resp.set_body(std::string_view("hello"));
    ASSERT_EQ(resp.body(), std::string_view("hello"));
    ASSERT_EQ(resp.get_header("Content-Length"), std::string_view("5"));
}

TEST(response_set_body_string)
{
    response resp;
    resp.set_body(std::string("world!"));
    ASSERT_EQ(resp.body(), std::string_view("world!"));
    ASSERT_EQ(resp.get_header("Content-Length"), std::string_view("6"));
}

// =============================================================================
// serialize
// =============================================================================

TEST(response_serialize_200)
{
    response resp(200);
    resp.set_header("Server", "cnetmod");
    resp.set_header("Content-Type", "application/json");
    resp.set_body(std::string_view(R"({"message":"Hello, World!"})"));

    auto s = resp.serialize();
    ASSERT_TRUE(s.starts_with("HTTP/1.1 200 OK\r\n"));
    ASSERT_TRUE(s.find("Server: cnetmod\r\n") != std::string::npos);
    ASSERT_TRUE(s.find("Content-Type: application/json\r\n") != std::string::npos);
    ASSERT_TRUE(s.find("Content-Length: 27\r\n") != std::string::npos);
    ASSERT_TRUE(s.ends_with(R"({"message":"Hello, World!"})"));
}

TEST(response_serialize_404_no_body)
{
    response resp(404);
    auto s = resp.serialize();
    ASSERT_TRUE(s.starts_with("HTTP/1.1 404 Not Found\r\n"));
    ASSERT_TRUE(s.ends_with("\r\n\r\n"));
}

TEST(response_serialize_301_redirect)
{
    response resp(301);
    resp.set_header("Location", "https://example.com/");

    auto s = resp.serialize();
    ASSERT_TRUE(s.starts_with("HTTP/1.1 301 Moved Permanently\r\n"));
    ASSERT_TRUE(s.find("Location: https://example.com/\r\n") != std::string::npos);
}

TEST(response_serialize_http10)
{
    response resp(200, http_version::http_1_0);
    auto s = resp.serialize();
    ASSERT_TRUE(s.starts_with("HTTP/1.0 200 OK\r\n"));
}

TEST(response_reset_and_serialize_to_reuse_storage)
{
    cnetmod::http::response response{201};
    response.set_header("X-Test", "first");
    response.set_body(std::string_view{"first"});
    std::string wire;
    response.serialize_to(wire);

    response.reset();
    response.set_header("X-Test", "second");
    response.set_body(std::string_view{"second"});
    response.serialize_to(wire);

    ASSERT_TRUE(wire.contains("HTTP/1.1 200 OK\r\n"));
    ASSERT_TRUE(wire.contains("X-Test: second\r\n"));
    ASSERT_TRUE(wire.contains("Content-Length: 6\r\n"));
    ASSERT_TRUE(wire.ends_with("\r\n\r\nsecond"));
    ASSERT_FALSE(wire.contains("first"));
}

// =============================================================================
// TFB compliance helpers
// =============================================================================

TEST(response_tfb_json_format)
{
    // Verify the exact TFB JSON response format
    response resp(200);
    resp.set_header("Server", "cnetmod");
    resp.set_header("Content-Type", "application/json");
    resp.set_body(std::string_view(R"({"message":"Hello, World!"})"));

    ASSERT_EQ(resp.status_code(), 200);
    ASSERT_EQ(resp.body(), std::string_view(R"({"message":"Hello, World!"})"));
    ASSERT_EQ(resp.body().size(), static_cast<std::size_t>(27));
}

TEST(response_tfb_plaintext_format)
{
    response resp(200);
    resp.set_header("Server", "cnetmod");
    resp.set_header("Content-Type", "text/plain");
    resp.set_body(std::string_view("Hello, World!"));

    ASSERT_EQ(resp.body(), std::string_view("Hello, World!"));
    ASSERT_EQ(resp.body().size(), static_cast<std::size_t>(13));
}

RUN_TESTS()
