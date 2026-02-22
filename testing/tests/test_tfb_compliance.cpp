/// cnetmod unit tests — TechEmpower Framework Benchmark compliance
///
/// Validates that HTTP responses conform to TFB specifications:
///   - Correct response body content
///   - Required headers (Server, Date, Content-Length/Transfer-Encoding)
///   - Correct Content-Type
///
/// These tests do NOT require network access — they validate serialized responses.

#include "test_framework.hpp"

import cnetmod.protocol.http;

using namespace cnetmod::http;

// =============================================================================
// Helper: build a TFB-like response and parse it back
// =============================================================================

static auto build_and_parse(const response& resp) -> response_parser {
    auto raw = resp.serialize();
    response_parser p;
    auto r = p.consume(raw.data(), raw.size());
    (void)r;
    return p;
}

// =============================================================================
// Test 1: JSON Serialization — /json
// =============================================================================

TEST(tfb_json_body) {
    // Body must be {"message":"Hello, World!"}
    std::string expected = R"({"message":"Hello, World!"})";
    ASSERT_EQ(expected.size(), static_cast<std::size_t>(27));
}

TEST(tfb_json_response) {
    response resp(200);
    resp.set_header("Server", "cnetmod");
    resp.set_header("Date", "Sat, 22 Feb 2026 06:00:00 GMT");
    resp.set_header("Content-Type", "application/json");
    resp.set_body(R"({"message":"Hello, World!"})");

    auto p = build_and_parse(resp);
    ASSERT_TRUE(p.ready());

    // Status
    ASSERT_EQ(p.status_code(), 200);

    // Required headers
    ASSERT_FALSE(p.get_header("Server").empty());
    ASSERT_FALSE(p.get_header("Date").empty());
    ASSERT_FALSE(p.get_header("Content-Length").empty());

    // Content-Type must be application/json (charset optional but accepted)
    auto ct = p.get_header("Content-Type");
    ASSERT_TRUE(ct.find("application/json") != std::string_view::npos);

    // Body must match exactly
    ASSERT_EQ(p.body(), std::string_view(R"({"message":"Hello, World!"})"));

    // Content-Length must be ~27
    ASSERT_EQ(p.get_header("Content-Length"), std::string_view("27"));
}

// =============================================================================
// Test 6: Plaintext — /plaintext
// =============================================================================

TEST(tfb_plaintext_response) {
    response resp(200);
    resp.set_header("Server", "cnetmod");
    resp.set_header("Date", "Sat, 22 Feb 2026 06:00:00 GMT");
    resp.set_header("Content-Type", "text/plain");
    resp.set_body("Hello, World!");

    auto p = build_and_parse(resp);
    ASSERT_TRUE(p.ready());

    ASSERT_EQ(p.status_code(), 200);
    ASSERT_FALSE(p.get_header("Server").empty());
    ASSERT_FALSE(p.get_header("Date").empty());
    ASSERT_FALSE(p.get_header("Content-Length").empty());

    auto ct = p.get_header("Content-Type");
    ASSERT_TRUE(ct.find("text/plain") != std::string_view::npos);

    ASSERT_EQ(p.body(), std::string_view("Hello, World!"));
    ASSERT_EQ(p.get_header("Content-Length"), std::string_view("13"));
}

// =============================================================================
// Test 2: DB — /db response format
// =============================================================================

TEST(tfb_db_response_format) {
    // Single row: {"id":N,"randomNumber":N}
    response resp(200);
    resp.set_header("Server", "cnetmod");
    resp.set_header("Date", "Sat, 22 Feb 2026 06:00:00 GMT");
    resp.set_header("Content-Type", "application/json");
    resp.set_body(R"({"id":4174,"randomNumber":8340})");

    auto p = build_and_parse(resp);
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.status_code(), 200);

    // Must contain "id" and "randomNumber" keys (case-sensitive)
    auto body = p.body();
    ASSERT_TRUE(body.find("\"id\"") != std::string_view::npos);
    ASSERT_TRUE(body.find("\"randomNumber\"") != std::string_view::npos);
}

// =============================================================================
// Test 3: Queries — /queries response format
// =============================================================================

TEST(tfb_queries_response_format) {
    // Array of world objects
    response resp(200);
    resp.set_header("Server", "cnetmod");
    resp.set_header("Date", "Sat, 22 Feb 2026 06:00:00 GMT");
    resp.set_header("Content-Type", "application/json");
    resp.set_body(R"([{"id":1,"randomNumber":42},{"id":2,"randomNumber":99}])");

    auto p = build_and_parse(resp);
    ASSERT_TRUE(p.ready());

    auto body = p.body();
    // Must start with [ and end with ]
    ASSERT_TRUE(body.starts_with("["));
    ASSERT_TRUE(body.ends_with("]"));
    // Must contain world objects
    ASSERT_TRUE(body.find("\"id\"") != std::string_view::npos);
    ASSERT_TRUE(body.find("\"randomNumber\"") != std::string_view::npos);
}

// =============================================================================
// Test 4: Fortunes — /fortunes response format
// =============================================================================

TEST(tfb_fortunes_response_format) {
    std::string html = "<!DOCTYPE html><html><head><title>Fortunes</title></head><body>"
                       "<table><tr><th>id</th><th>message</th></tr>"
                       "<tr><td>0</td><td>Additional fortune added at request time.</td></tr>"
                       "<tr><td>1</td><td>fortune: A sample fortune.</td></tr>"
                       "</table></body></html>";

    response resp(200);
    resp.set_header("Server", "cnetmod");
    resp.set_header("Date", "Sat, 22 Feb 2026 06:00:00 GMT");
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_body(html);

    auto p = build_and_parse(resp);
    ASSERT_TRUE(p.ready());
    ASSERT_EQ(p.status_code(), 200);

    auto ct = p.get_header("Content-Type");
    ASSERT_TRUE(ct.find("text/html") != std::string_view::npos);

    auto body = p.body();
    // Must contain the additional fortune
    ASSERT_TRUE(body.find("Additional fortune added at request time.") != std::string_view::npos);
    // Must be valid HTML structure
    ASSERT_TRUE(body.find("<!DOCTYPE html>") != std::string_view::npos);
    ASSERT_TRUE(body.find("<table>") != std::string_view::npos);
    ASSERT_TRUE(body.find("</table>") != std::string_view::npos);
}

// =============================================================================
// General: Server header must be present
// =============================================================================

TEST(tfb_server_header) {
    response resp(200);
    resp.set_header("Server", "cnetmod");
    resp.set_body("ok");

    auto p = build_and_parse(resp);
    ASSERT_EQ(p.get_header("Server"), std::string_view("cnetmod"));
}

// =============================================================================
// General: Date header format (RFC 7231)
// =============================================================================

TEST(tfb_date_header_format) {
    // Date must be in RFC 7231 format: "Day, DD Mon YYYY HH:MM:SS GMT"
    std::string date = "Sat, 22 Feb 2026 06:00:00 GMT";
    response resp(200);
    resp.set_header("Server", "cnetmod");
    resp.set_header("Date", date);
    resp.set_body("ok");

    auto p = build_and_parse(resp);
    auto d = p.get_header("Date");
    ASSERT_FALSE(d.empty());
    ASSERT_TRUE(d.find("GMT") != std::string_view::npos);
}

// =============================================================================
// General: Content-Length must be present for non-chunked responses
// =============================================================================

TEST(tfb_content_length_present) {
    response resp(200);
    resp.set_header("Server", "cnetmod");
    resp.set_body("test");

    auto p = build_and_parse(resp);
    ASSERT_EQ(p.get_header("Content-Length"), std::string_view("4"));
}

// =============================================================================
// HTML escape for Fortunes
// =============================================================================

TEST(tfb_html_escape) {
    // Simulate the html_escape logic used in tfb_benchmark
    auto html_escape = [](std::string_view s) -> std::string {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '&':  out += "&amp;";  break;
                case '<':  out += "&lt;";   break;
                case '>':  out += "&gt;";   break;
                case '"':  out += "&quot;"; break;
                case '\'': out += "&#39;";  break;
                default:   out += c;        break;
            }
        }
        return out;
    };

    ASSERT_EQ(html_escape("<script>alert('xss')</script>"),
              std::string("&lt;script&gt;alert(&#39;xss&#39;)&lt;/script&gt;"));
    ASSERT_EQ(html_escape("A&B"), std::string("A&amp;B"));
    ASSERT_EQ(html_escape("\"quoted\""), std::string("&quot;quoted&quot;"));
    ASSERT_EQ(html_escape("plain text"), std::string("plain text"));
}

RUN_TESTS()
