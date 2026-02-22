/// cnetmod unit tests — HTTP router matching

#include "test_framework.hpp"

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

using namespace cnetmod::http;

// Helper: create a dummy handler that sets a marker
static auto make_handler(std::string marker) -> handler_fn {
    return [m = std::move(marker)](request_context&) -> cnetmod::task<void> {
        (void)m;
        co_return;
    };
}

// =============================================================================
// Exact match
// =============================================================================

TEST(router_exact_match) {
    router r;
    r.get("/", make_handler("root"));
    r.get("/api/users", make_handler("users"));

    auto m1 = r.match(http_method::GET, "/");
    ASSERT_TRUE(m1.has_value());

    auto m2 = r.match(http_method::GET, "/api/users");
    ASSERT_TRUE(m2.has_value());
}

TEST(router_no_match) {
    router r;
    r.get("/api/users", make_handler("users"));

    auto m = r.match(http_method::GET, "/api/posts");
    ASSERT_FALSE(m.has_value());
}

// =============================================================================
// Method filtering
// =============================================================================

TEST(router_method_filter) {
    router r;
    r.get("/data", make_handler("get"));
    r.post("/data", make_handler("post"));

    auto m_get = r.match(http_method::GET, "/data");
    ASSERT_TRUE(m_get.has_value());

    auto m_post = r.match(http_method::POST, "/data");
    ASSERT_TRUE(m_post.has_value());

    auto m_put = r.match(http_method::PUT, "/data");
    ASSERT_FALSE(m_put.has_value());
}

TEST(router_any_method) {
    router r;
    r.any("/health", make_handler("health"));

    ASSERT_TRUE(r.match(http_method::GET, "/health").has_value());
    ASSERT_TRUE(r.match(http_method::POST, "/health").has_value());
    ASSERT_TRUE(r.match(http_method::DELETE_, "/health").has_value());
}

// =============================================================================
// Param match (:id)
// =============================================================================

TEST(router_param_match) {
    router r;
    r.get("/api/users/:id", make_handler("user"));

    auto m = r.match(http_method::GET, "/api/users/42");
    ASSERT_TRUE(m.has_value());
    ASSERT_EQ(m->params.get("id"), std::string_view("42"));
}

TEST(router_param_multiple) {
    router r;
    r.get("/api/users/:uid/posts/:pid", make_handler("post"));

    auto m = r.match(http_method::GET, "/api/users/7/posts/99");
    ASSERT_TRUE(m.has_value());
    ASSERT_EQ(m->params.get("uid"), std::string_view("7"));
    ASSERT_EQ(m->params.get("pid"), std::string_view("99"));
}

TEST(router_param_no_match_extra_segment) {
    router r;
    r.get("/api/users/:id", make_handler("user"));

    // Extra segment should not match
    auto m = r.match(http_method::GET, "/api/users/42/extra");
    ASSERT_FALSE(m.has_value());
}

TEST(router_param_no_match_missing_segment) {
    router r;
    r.get("/api/users/:id", make_handler("user"));

    auto m = r.match(http_method::GET, "/api/users");
    ASSERT_FALSE(m.has_value());
}

// =============================================================================
// Wildcard match (*filepath)
// =============================================================================

TEST(router_wildcard_match) {
    router r;
    r.get("/static/*filepath", make_handler("static"));

    auto m = r.match(http_method::GET, "/static/css/style.css");
    ASSERT_TRUE(m.has_value());
    ASSERT_EQ(m->params.wildcard, std::string("css/style.css"));
}

TEST(router_wildcard_single_segment) {
    router r;
    r.get("/files/*path", make_handler("files"));

    auto m = r.match(http_method::GET, "/files/readme.txt");
    ASSERT_TRUE(m.has_value());
    ASSERT_EQ(m->params.wildcard, std::string("readme.txt"));
}

TEST(router_wildcard_empty) {
    router r;
    r.get("/files/*path", make_handler("files"));

    auto m = r.match(http_method::GET, "/files");
    ASSERT_TRUE(m.has_value());
    ASSERT_TRUE(m->params.wildcard.empty());
}

// =============================================================================
// Priority: exact > param > wildcard
// =============================================================================

TEST(router_priority_exact_over_param) {
    router r;
    r.get("/api/users/me", make_handler("me"));
    r.get("/api/users/:id", make_handler("user"));

    auto m = r.match(http_method::GET, "/api/users/me");
    ASSERT_TRUE(m.has_value());
    // Exact should win — params should be empty (no :id captured)
    ASSERT_TRUE(m->params.get("id").empty());
}

TEST(router_priority_param_over_wildcard) {
    router r;
    r.get("/api/:resource", make_handler("resource"));
    r.get("/api/*rest", make_handler("catch-all"));

    auto m = r.match(http_method::GET, "/api/users");
    ASSERT_TRUE(m.has_value());
    // Param should win
    ASSERT_EQ(m->params.get("resource"), std::string_view("users"));
    ASSERT_TRUE(m->params.wildcard.empty());
}

// =============================================================================
// String method overload
// =============================================================================

TEST(router_string_method_match) {
    router r;
    r.get("/test", make_handler("test"));

    auto m = r.match(std::string_view("GET"), std::string_view("/test"));
    ASSERT_TRUE(m.has_value());
}

TEST(router_string_method_invalid) {
    router r;
    r.get("/test", make_handler("test"));

    auto m = r.match(std::string_view("INVALID"), std::string_view("/test"));
    ASSERT_FALSE(m.has_value());
}

// =============================================================================
// All HTTP methods
// =============================================================================

TEST(router_put_del_patch) {
    router r;
    r.put("/item", make_handler("put"));
    r.del("/item", make_handler("del"));
    r.patch("/item", make_handler("patch"));

    ASSERT_TRUE(r.match(http_method::PUT, "/item").has_value());
    ASSERT_TRUE(r.match(http_method::DELETE_, "/item").has_value());
    ASSERT_TRUE(r.match(http_method::PATCH, "/item").has_value());
}

RUN_TESTS()
