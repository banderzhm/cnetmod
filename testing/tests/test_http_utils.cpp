/// cnetmod unit tests — HTTP utility functions

#include "test_framework.hpp"

import cnetmod.protocol.http;

using namespace cnetmod::http;

// =============================================================================
// parse_query_param
// =============================================================================

TEST(parse_query_param_basic) {
    auto val = parse_query_param("type=json&size=10", "type");
    ASSERT_EQ(val, std::string("json"));
}

TEST(parse_query_param_second_key) {
    auto val = parse_query_param("type=json&size=10", "size");
    ASSERT_EQ(val, std::string("10"));
}

TEST(parse_query_param_missing_key) {
    auto val = parse_query_param("type=json&size=10", "missing");
    ASSERT_TRUE(val.empty());
}

TEST(parse_query_param_empty_value) {
    auto val = parse_query_param("key=&other=1", "key");
    ASSERT_TRUE(val.empty());
}

TEST(parse_query_param_single_param) {
    auto val = parse_query_param("queries=5", "queries");
    ASSERT_EQ(val, std::string("5"));
}

TEST(parse_query_param_no_boundary_confusion) {
    // "mykey" should not match when looking for "key"
    auto val = parse_query_param("mykey=bad&key=good", "key");
    ASSERT_EQ(val, std::string("good"));
}

TEST(parse_query_param_empty_query) {
    auto val = parse_query_param("", "key");
    ASSERT_TRUE(val.empty());
}

TEST(parse_query_param_url_encoded_value) {
    // Does NOT decode %xx — that's by design
    auto val = parse_query_param("email=test%40example.com", "email");
    ASSERT_EQ(val, std::string("test%40example.com"));
}

// =============================================================================
// parse_query_params
// =============================================================================

TEST(parse_query_params_basic) {
    auto params = parse_query_params("a=1&b=hello&c=");
    ASSERT_EQ(params.size(), static_cast<std::size_t>(3));
    ASSERT_EQ(params["a"], std::string("1"));
    ASSERT_EQ(params["b"], std::string("hello"));
    ASSERT_EQ(params["c"], std::string(""));
}

TEST(parse_query_params_single) {
    auto params = parse_query_params("queries=20");
    ASSERT_EQ(params.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(params["queries"], std::string("20"));
}

TEST(parse_query_params_empty) {
    auto params = parse_query_params("");
    ASSERT_TRUE(params.empty());
}

TEST(parse_query_params_duplicate_key) {
    // Later value overwrites earlier
    auto params = parse_query_params("x=1&x=2");
    ASSERT_EQ(params.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(params["x"], std::string("2"));
}

TEST(parse_query_params_no_value) {
    // Segments without '=' should be skipped
    auto params = parse_query_params("a=1&noval&b=2");
    ASSERT_EQ(params.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(params["a"], std::string("1"));
    ASSERT_EQ(params["b"], std::string("2"));
}

RUN_TESTS()
