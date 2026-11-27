#include "test_framework.hpp"
#include <cnetmod/json_codec.hpp>

import std;
import cnetmod.json;

namespace cnetmod_test {
struct sample_document
{
    std::string name;
    std::int32_t count{};

    auto operator==(const sample_document&) const -> bool = default;
};

struct nested_document
{
    std::string label;
};

struct aggregate_document
{
    std::optional<std::string> note;
    std::vector<nested_document> children;
    std::map<std::string, std::uint64_t> counters;
};

} // namespace cnetmod_test

CNETMOD_JSON(cnetmod_test::sample_document,
    CNETMOD_JSON_FIELD(name),
    CNETMOD_JSON_FIELD(count))

CNETMOD_JSON(cnetmod_test::nested_document,
    CNETMOD_JSON_FIELD(label))

CNETMOD_JSON(cnetmod_test::aggregate_document,
    CNETMOD_JSON_FIELD(note),
    CNETMOD_JSON_FIELD(children),
    CNETMOD_JSON_FIELD(counters))

using cnetmod_test::aggregate_document;
using cnetmod_test::sample_document;

TEST(framework_document_is_the_default_json_codec)
{
    const sample_document expected{"orders", 7};
    auto encoded = cnetmod::json::write(expected);
    ASSERT_TRUE(encoded.has_value());
    auto decoded = cnetmod::json::parse<sample_document>(*encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->name, expected.name);
    ASSERT_EQ(decoded->count, expected.count);

    auto invalid = cnetmod::json::parse<sample_document>("{");
    ASSERT_FALSE(invalid.has_value());
    ASSERT_EQ(invalid.error(),
        cnetmod::json::make_error_code(cnetmod::json::errc::parse_failed));
}

TEST(framework_document_null_lookup_and_numeric_equality_are_stable)
{
    const cnetmod::json::document empty;
    ASSERT_TRUE(cnetmod::json::find(empty, "missing") == nullptr);

    auto object = cnetmod::json::object();
    ASSERT_TRUE(object.is_object());
    object["ready"] = true;
    ASSERT_TRUE(object.at("ready").get<bool>());
    const auto initialized_object = cnetmod::json::object(
        {{"enabled", true}});
    ASSERT_TRUE(initialized_object.is_object());
    ASSERT_TRUE(initialized_object.at("enabled").is_boolean());
    ASSERT_TRUE(cnetmod::json::value_or(
        initialized_object, "enabled", false));

    auto values = cnetmod::json::array({1, 2});
    ASSERT_TRUE(values.is_array());
    values.get_array().emplace_back(3);
    ASSERT_EQ(values.size(), std::size_t{3});
    ASSERT_TRUE(cnetmod::json::find(values, "missing") == nullptr);
    ASSERT_TRUE(cnetmod::json::document(std::uint64_t{2}).as<std::int64_t>() ==
        cnetmod::json::document(std::int64_t{2}).as<std::int64_t>());
    ASSERT_FALSE(cnetmod::json::document(std::uint64_t{2}).as<std::int64_t>() ==
        cnetmod::json::document(std::int64_t{-2}).as<std::int64_t>());
}

TEST(framework_document_uses_native_parser_boundaries)
{
    const auto duplicate = cnetmod::json::parse_document(
        R"({"value":1,"value":2})");
    ASSERT_TRUE(duplicate.has_value());
    ASSERT_EQ(duplicate->at("value").get<std::uint64_t>(), 2U);

    const auto rejected_duplicate = cnetmod::json::parse_document(
        R"({"outer":{"value":1,"value":2}})",
        {.reject_duplicate_keys = true});
    ASSERT_FALSE(rejected_duplicate.has_value());

    const auto accepted_depth = cnetmod::json::parse_document(
        R"({"outer":[]})", {.max_depth = 2});
    ASSERT_TRUE(accepted_depth.has_value());
    const auto rejected_depth = cnetmod::json::parse_document(
        R"({"outer":[]})", {.max_depth = 1});
    ASSERT_FALSE(rejected_depth.has_value());

    const auto large_integer = cnetmod::json::parse_document("18446744073709551616");
    ASSERT_TRUE(large_integer.has_value());
    ASSERT_TRUE(large_integer->is_double());
    ASSERT_EQ(large_integer->get<double>(), 18446744073709551616.0);
    ASSERT_TRUE(cnetmod::json::parse_document("\xef\xbb\xbf{}").has_value());
    ASSERT_FALSE(cnetmod::json::parse_document("{}{}").has_value());
    ASSERT_FALSE(cnetmod::json::parse_document(
        std::string(257, '[') + "0" + std::string(257, ']'))
            .has_value());
}

TEST(framework_json_codec_enforces_schema_and_preserves_aggregate_values)
{
    const aggregate_document original{
        .note = std::nullopt,
        .children = {{"first"}, {"second"}},
        .counters = {{"large", std::numeric_limits<std::uint64_t>::max()}},
    };
    const auto encoded = cnetmod::json::write(original);
    ASSERT_TRUE(encoded.has_value());
    ASSERT_FALSE(encoded->contains("note"));

    const auto decoded = cnetmod::json::parse<aggregate_document>(*encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_FALSE(decoded->note.has_value());
    ASSERT_EQ(decoded->children.size(), 2U);
    ASSERT_EQ(decoded->children.at(1).label, "second");
    ASSERT_EQ(decoded->counters.at("large"),
        std::numeric_limits<std::uint64_t>::max());

    const auto unknown = cnetmod::json::parse<sample_document>(
        R"({"name":"orders","count":7,"extra":true})");
    ASSERT_FALSE(unknown.has_value());
    ASSERT_EQ(unknown.error(),
        cnetmod::json::make_error_code(cnetmod::json::errc::unknown_field));

    const auto lenient = cnetmod::json::parse_allow_unknown<sample_document>(
        R"({"name":"orders","count":7,"extra":true})");
    ASSERT_TRUE(lenient.has_value());

    const auto lenient_missing =
        cnetmod::json::parse_allow_unknown<sample_document>(
            R"({"name":"orders","extra":true})");
    ASSERT_FALSE(lenient_missing.has_value());
    ASSERT_EQ(lenient_missing.error(),
        cnetmod::json::make_error_code(cnetmod::json::errc::missing_field));

    const auto compatibility_name =
        cnetmod::json::parse_lenient<sample_document>(
            R"({"name":"orders","count":7,"extra":true})");
    ASSERT_TRUE(compatibility_name.has_value());

    const auto missing = cnetmod::json::parse<sample_document>(
        R"({"name":"orders"})");
    ASSERT_FALSE(missing.has_value());
    ASSERT_EQ(missing.error(),
        cnetmod::json::make_error_code(cnetmod::json::errc::missing_field));

    const auto overflow = cnetmod::json::parse<sample_document>(
        R"({"name":"orders","count":18446744073709551615})");
    ASSERT_FALSE(overflow.has_value());
    ASSERT_EQ(overflow.error(),
        cnetmod::json::make_error_code(cnetmod::json::errc::type_mismatch));
}

RUN_TESTS()
