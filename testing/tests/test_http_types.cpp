/// cnetmod unit tests — HTTP types: method, version, status, url, case_insensitive_less

#include "test_framework.hpp"

import cnetmod.protocol.http;

using namespace cnetmod::http;

// =============================================================================
// method_to_string / string_to_method
// =============================================================================

TEST(method_to_string_get) {
    ASSERT_EQ(method_to_string(http_method::GET), std::string_view("GET"));
}

TEST(method_to_string_post) {
    ASSERT_EQ(method_to_string(http_method::POST), std::string_view("POST"));
}

TEST(method_to_string_delete) {
    ASSERT_EQ(method_to_string(http_method::DELETE_), std::string_view("DELETE"));
}

TEST(string_to_method_get) {
    auto m = string_to_method("GET");
    ASSERT_TRUE(m.has_value());
    ASSERT_EQ(static_cast<int>(*m), static_cast<int>(http_method::GET));
}

TEST(string_to_method_post) {
    auto m = string_to_method("POST");
    ASSERT_TRUE(m.has_value());
    ASSERT_EQ(static_cast<int>(*m), static_cast<int>(http_method::POST));
}

TEST(string_to_method_roundtrip_all) {
    constexpr http_method methods[] = {
        http_method::GET, http_method::HEAD, http_method::POST,
        http_method::PUT, http_method::DELETE_, http_method::CONNECT,
        http_method::OPTIONS, http_method::TRACE, http_method::PATCH,
    };
    for (auto m : methods) {
        auto s = method_to_string(m);
        auto back = string_to_method(s);
        ASSERT_TRUE(back.has_value());
        ASSERT_EQ(static_cast<int>(*back), static_cast<int>(m));
    }
}

TEST(string_to_method_invalid) {
    auto m = string_to_method("INVALID");
    ASSERT_FALSE(m.has_value());
}

TEST(string_to_method_lowercase_fails) {
    auto m = string_to_method("get");
    ASSERT_FALSE(m.has_value());
}

// =============================================================================
// version_to_string / string_to_version
// =============================================================================

TEST(version_roundtrip_1_0) {
    auto s = version_to_string(http_version::http_1_0);
    ASSERT_EQ(s, std::string_view("HTTP/1.0"));
    auto v = string_to_version(s);
    ASSERT_TRUE(v.has_value());
    ASSERT_EQ(static_cast<int>(*v), static_cast<int>(http_version::http_1_0));
}

TEST(version_roundtrip_1_1) {
    auto s = version_to_string(http_version::http_1_1);
    ASSERT_EQ(s, std::string_view("HTTP/1.1"));
    auto v = string_to_version(s);
    ASSERT_TRUE(v.has_value());
    ASSERT_EQ(static_cast<int>(*v), static_cast<int>(http_version::http_1_1));
}

TEST(string_to_version_invalid) {
    auto v = string_to_version("HTTP/2.0");
    ASSERT_FALSE(v.has_value());
}

// =============================================================================
// status_reason
// =============================================================================

TEST(status_reason_200) {
    ASSERT_EQ(status_reason(200), std::string_view("OK"));
}

TEST(status_reason_404) {
    ASSERT_EQ(status_reason(404), std::string_view("Not Found"));
}

TEST(status_reason_500) {
    ASSERT_EQ(status_reason(500), std::string_view("Internal Server Error"));
}

TEST(status_reason_unknown) {
    ASSERT_EQ(status_reason(999), std::string_view("Unknown"));
}

TEST(status_reason_common_codes) {
    // Ensure all common codes have non-"Unknown" reasons
    int codes[] = {100, 101, 200, 201, 202, 204, 206,
                   301, 302, 303, 304, 307, 308,
                   400, 401, 403, 404, 405, 408, 409, 410, 411, 413, 414, 415, 426, 429,
                   500, 501, 502, 503, 504};
    for (auto c : codes) {
        auto r = status_reason(c);
        ASSERT_NE(r, std::string_view("Unknown"));
    }
}

// =============================================================================
// case_insensitive_less
// =============================================================================

TEST(case_insensitive_less_basic) {
    case_insensitive_less cmp;
    ASSERT_TRUE(cmp("Content-Type", "date"));       // 'c' < 'd'
    ASSERT_FALSE(cmp("Date", "content-type"));       // 'd' > 'c'
    ASSERT_FALSE(cmp("Content-Type", "content-type"));// equal → not less
}

TEST(case_insensitive_less_header_map) {
    header_map hm;
    hm["Content-Type"] = "text/html";
    hm["content-type"] = "overwritten";
    // Case-insensitive: same key, should have 1 entry
    ASSERT_EQ(hm.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(hm.begin()->second, std::string("overwritten"));
}

// =============================================================================
// url::parse
// =============================================================================

TEST(url_parse_basic) {
    auto r = url::parse("http://example.com:8080/path?q=1#frag");
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->scheme, std::string("http"));
    ASSERT_EQ(r->host, std::string("example.com"));
    ASSERT_EQ(r->port, static_cast<std::uint16_t>(8080));
    ASSERT_EQ(r->path, std::string("/path"));
    ASSERT_EQ(r->query, std::string("q=1"));
    ASSERT_EQ(r->fragment, std::string("frag"));
}

TEST(url_parse_default_port_http) {
    auto r = url::parse("http://example.com/");
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->port, static_cast<std::uint16_t>(80));
}

TEST(url_parse_default_port_https) {
    auto r = url::parse("https://example.com/");
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->port, static_cast<std::uint16_t>(443));
}

TEST(url_parse_no_path) {
    auto r = url::parse("http://host");
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->path, std::string("/"));
}

TEST(url_parse_ipv6) {
    auto r = url::parse("http://[::1]:9090/test");
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->host, std::string("::1"));
    ASSERT_EQ(r->port, static_cast<std::uint16_t>(9090));
    ASSERT_EQ(r->path, std::string("/test"));
}

TEST(url_parse_missing_scheme) {
    auto r = url::parse("example.com/path");
    ASSERT_FALSE(r.has_value());
}

RUN_TESTS()
