/// cnetmod unit tests — HTTP response builder and serialization

#include "test_framework.hpp"

import std;
import cnetmod.core.buffer;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.utils;

using namespace cnetmod::http;
using cnetmod::utils::R;

TEST(application_result_http_adapter)
{
    auto success = to_http_response(R<std::string>::ok("alice"), [](const std::string& value)
        {
            return R"({"user":")" + value + R"("})";
        },
        [](const auto& error)
        {
            return R"({"error":")" + error.message + R"("})";
        });
    ASSERT_EQ(success.status_code(), status::ok);
    ASSERT_EQ(success.get_header("Content-Type"), std::string_view("application/json"));
    ASSERT_EQ(success.body(), std::string_view(R"({"user":"alice"})"));

    auto failure = to_http_response(R<std::string>::error(404, "missing"), [](const std::string& value)
        {
            return value;
        },
        [](const auto& error)
        {
            return R"({"error":")" + error.message + R"("})";
        },
        {.error_status = status::not_found});
    ASSERT_EQ(failure.status_code(), status::not_found);
    ASSERT_EQ(failure.body(), std::string_view(R"({"error":"missing"})"));

    auto no_content = to_http_response(R<void>::success(), []
        {
            return std::string{};
        },
        [](const auto& error)
        {
            return error.message;
        },
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

TEST(response_set_header_owned)
{
    response resp;
    auto date = std::string{"Mon, 10 Aug 2026 12:34:56 GMT"};
    resp.set_header_owned("Date", std::move(date));
    ASSERT_TRUE(date.empty());
    ASSERT_EQ(resp.get_header("Date"),
        std::string_view("Mon, 10 Aug 2026 12:34:56 GMT"));
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

TEST(response_generate_body_reuses_storage_and_discards_partial_failure)
{
    response resp;
    const char* first_storage{};
    auto generated = resp.generate_body([&](std::string& output)
                                            -> std::expected<void, std::error_code>
        {
            output.assign(16U * 1024U, 'a');
            first_storage = output.data();
            return {};
        });
    ASSERT_TRUE(generated.has_value());
    ASSERT_EQ(resp.body().size(), std::size_t{16U * 1024U});
    ASSERT_EQ(resp.get_header("Content-Length"), std::string_view("16384"));

    resp.reset();
    const char* second_storage{};
    generated = resp.generate_body([&](std::string& output)
                                       -> std::expected<void, std::error_code>
        {
            output.assign(4096U, 'b');
            second_storage = output.data();
            return {};
        });
    ASSERT_TRUE(generated.has_value());
    ASSERT_TRUE(second_storage == first_storage);
    ASSERT_EQ(resp.get_header("Content-Length"), std::string_view("4096"));

    generated = resp.generate_body([](std::string& output)
                                       -> std::expected<void, std::error_code>
        {
            output = "partial";
            return std::unexpected(
                std::make_error_code(std::errc::invalid_argument));
        });
    ASSERT_FALSE(generated.has_value());
    ASSERT_TRUE(resp.body().empty());
    ASSERT_EQ(resp.get_header("Content-Length"), std::string_view("0"));
}

TEST(response_transform_body_reuses_both_buffers_and_commits_atomically)
{
    response resp;
    const char* source_storage{};
    auto generated = resp.generate_body([&](std::string& output)
                                            -> std::expected<void, std::error_code>
        {
            output.assign(16U * 1024U, 'a');
            source_storage = output.data();
            return {};
        });
    ASSERT_TRUE(generated.has_value());

    const char* encoded_storage{};
    auto transformed = resp.transform_body(
        [&](std::string_view input, std::string& output)
            -> std::expected<bool, std::error_code>
        {
            ASSERT_EQ(input.size(), std::size_t{16U * 1024U});
            output.assign(1024U, 'z');
            encoded_storage = output.data();
            return true;
        });
    ASSERT_TRUE(transformed.has_value() && *transformed);
    ASSERT_EQ(resp.body().size(), std::size_t{1024U});
    ASSERT_TRUE(std::ranges::all_of(resp.body(),
        [](char value)
        {
            return value == 'z';
        }));
    ASSERT_EQ(resp.get_header("Content-Length"), std::string_view("1024"));

    resp.reset();
    const char* reused_source{};
    generated = resp.generate_body([&](std::string& output)
                                       -> std::expected<void, std::error_code>
        {
            output.assign(4096U, 'b');
            reused_source = output.data();
            return {};
        });
    ASSERT_TRUE(generated.has_value());
    ASSERT_TRUE(reused_source == source_storage);

    const char* reused_encoded{};
    transformed = resp.transform_body(
        [&](std::string_view, std::string& output)
            -> std::expected<bool, std::error_code>
        {
            output.assign(512U, 'y');
            reused_encoded = output.data();
            return true;
        });
    ASSERT_TRUE(transformed.has_value() && *transformed);
    ASSERT_TRUE(reused_encoded == encoded_storage);

    resp.reset();
    resp.set_body(std::string_view{"original"});
    transformed = resp.transform_body(
        [](std::string_view, std::string& output)
            -> std::expected<bool, std::error_code>
        {
            output = "larger-output";
            return false;
        });
    ASSERT_TRUE(transformed.has_value() && !*transformed);
    ASSERT_EQ(resp.body(), std::string_view{"original"});
    ASSERT_EQ(resp.get_header("Content-Length"), std::string_view{"8"});
}

TEST(response_shared_body_reuses_immutable_storage)
{
    response resp;
    auto content = std::make_shared<const std::string>("cached-file");
    resp.set_shared_body(content);
    ASSERT_TRUE(resp.shared_body() == content);
    ASSERT_EQ(resp.body(), std::string_view("cached-file"));
    ASSERT_EQ(resp.get_header("Content-Length"), std::string_view("11"));
    ASSERT_TRUE(resp.serialize().ends_with("\r\ncached-file"));

    auto taken = resp.take_shared_body();
    ASSERT_TRUE(taken == content);
    ASSERT_FALSE(resp.shared_body());

    resp.set_shared_body(std::move(content));
    resp.set_body(std::string_view{"replacement"});
    ASSERT_FALSE(resp.shared_body());
    ASSERT_EQ(resp.body(), std::string_view{"replacement"});

    resp.reset();
    resp.set_header("Content-Length", "11");
    resp.set_shared_body_preserve_headers(
        std::make_shared<const std::string>("cached-file"));
    ASSERT_EQ(resp.get_header("Content-Length"), std::string_view{"11"});
    ASSERT_EQ(resp.body(), std::string_view{"cached-file"});
}

TEST(prepared_response_preserves_semantics_and_invalidates_on_mutation)
{
    const prepared_response prepared{
        status::ok, "text/plain", "ok"};
    response resp;
    resp.set_prepared(prepared);

    ASSERT_EQ(resp.status_code(), status::ok);
    ASSERT_EQ(resp.get_header("Content-Type"), std::string_view{"text/plain"});
    ASSERT_EQ(resp.get_header("Content-Length"), std::string_view{"2"});
    ASSERT_EQ(resp.body(), std::string_view{"ok"});
    ASSERT_EQ(resp.serialize(),
        std::string{"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n"
                    "Content-Type: text/plain\r\n\r\nok"});

    resp.set_header("X-Test", "changed");
    ASSERT_TRUE(resp.serialize().find("X-Test: changed\r\n") !=
        std::string::npos);

    resp.reset();
    resp.set_header("Server", "test");
    resp.set_prepared(prepared);
    const auto with_dynamic_header = resp.serialize();
    ASSERT_TRUE(with_dynamic_header.find("Server: test\r\n") !=
        std::string::npos);
    ASSERT_TRUE(with_dynamic_header.ends_with("\r\nok"));
}

TEST(response_body_source_is_exclusive_and_sets_length)
{
    response resp;
    auto source = std::make_shared<response_body_source>(
        [](cnetmod::cancel_token&)
            -> cnetmod::task<std::optional<request_body_chunk>>
        {
            co_return std::nullopt;
        },
        42U);
    resp.set_body(std::string_view{"buffered"});
    resp.set_body_source(source);
    ASSERT_TRUE(resp.body().empty());
    ASSERT_TRUE(resp.body_source() == source);
    ASSERT_EQ(resp.get_header("Content-Length"), std::string_view{"42"});

    resp.set_body(std::string_view{"replacement"});
    ASSERT_FALSE(resp.body_source());
    ASSERT_EQ(resp.body(), std::string_view{"replacement"});

    auto unknown_length = std::make_shared<response_body_source>(
        [](cnetmod::cancel_token&)
            -> cnetmod::task<std::optional<request_body_chunk>>
        {
            co_return std::nullopt;
        });
    resp.set_body_source(std::move(unknown_length));
    ASSERT_TRUE(resp.get_header("Content-Length").empty());

    resp.set_body_source(std::move(source));
    auto taken = resp.take_body_source();
    ASSERT_TRUE(taken);
    ASSERT_FALSE(resp.body_source());
    resp.reset();
    ASSERT_FALSE(resp.body_source());
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

TEST(response_cached_date_header_serializes_without_duplicate)
{
    response response{200};
    std::array<char, 29U> date{};
    std::memcpy(date.data(), "Mon, 10 Aug 2026 12:34:56 GMT", date.size());
    response.set_cached_date_header(date);
    response.set_body(std::string_view{"Hello, World!"});
    std::string wire;
    response.serialize_to(wire);

    ASSERT_TRUE(wire.contains("Date: Mon, 10 Aug 2026 12:34:56 GMT\r\n"));
    ASSERT_TRUE(wire.contains("Content-Length: 13\r\n"));
    ASSERT_EQ(wire.find("Date:"), wire.rfind("Date:"));
    ASSERT_TRUE(wire.ends_with("\r\n\r\nHello, World!"));
}

TEST(response_explicit_date_overrides_cached_date)
{
    response response{200};
    std::array<char, 29U> date{};
    std::memcpy(date.data(), "Mon, 10 Aug 2026 12:34:56 GMT", date.size());
    response.set_cached_date_header(date);
    response.set_header("Date", "route-supplied-date");
    ASSERT_EQ(response.get_header("Date"), std::string_view{"route-supplied-date"});

    const auto wire = response.serialize();
    ASSERT_TRUE(wire.contains("Date: route-supplied-date\r\n"));
    ASSERT_FALSE(wire.contains("Mon, 10 Aug 2026 12:34:56 GMT"));
}

TEST(response_common_headers_materialize_and_reset_without_leaking_state)
{
    response value{200};
    value.set_header("content-type", "text/plain");
    value.set_header("Connection", "keep-alive");
    value.set_body(std::string_view{"ok"});

    ASSERT_EQ(value.headers().at("Content-Type"), std::string("text/plain"));
    ASSERT_EQ(value.headers().at("content-length"), std::string("2"));
    ASSERT_EQ(value.headers().at("connection"), std::string("keep-alive"));

    value.reset();
    ASSERT_TRUE(value.headers().empty());
    ASSERT_TRUE(value.get_header("Content-Type").empty());
}

TEST(response_append_serialized_to_builds_pipeline_batch)
{
    response value{200};
    std::string wire;
    value.set_body(std::string_view{"one"});
    value.append_serialized_to(wire);
    value.reset();
    value.set_body(std::string_view{"two"});
    value.append_serialized_to(wire);

    const auto first = wire.find("HTTP/1.1 200 OK\r\n");
    const auto second = wire.find("HTTP/1.1 200 OK\r\n", first + 1);
    ASSERT_EQ(first, std::size_t{0});
    ASSERT_TRUE(second != std::string::npos);
    ASSERT_TRUE(wire.substr(0, second).ends_with("\r\n\r\none"));
    ASSERT_TRUE(wire.ends_with("\r\n\r\ntwo"));
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
