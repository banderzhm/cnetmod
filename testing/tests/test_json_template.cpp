#include "test_framework.hpp"
#include <cnetmod/json_codec.hpp>

import std;
import cnetmod.core;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.executor.pool;
import cnetmod.io;
import cnetmod.json;
import cnetmod.application.json_template;

namespace cnetmod_test {
struct sample_document
{
    std::string name;
    std::int32_t count{};

    auto operator==(const sample_document&) const -> bool = default;
};

struct custom_document
{
    std::string value;
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

struct custom_codec
{
    template <typename T>
    static auto decode(std::string_view input)
        -> std::expected<T, std::error_code>
    {
        if constexpr (std::same_as<T, custom_document>)
            return custom_document{std::string{input}};
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }

    template <typename T>
    static auto encode(const T& value)
        -> std::expected<std::string, std::error_code>
    {
        if constexpr (std::same_as<T, custom_document>)
            return "custom:" + value.value;
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
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

using cnetmod_test::custom_codec;
using cnetmod_test::custom_document;
using cnetmod_test::sample_document;
using cnetmod_test::aggregate_document;

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
    ASSERT_TRUE(empty.find("missing") == empty.end());

    const auto values = cnetmod::json::document::array({1, 2});
    ASSERT_TRUE(values.find("missing") == values.end());
    ASSERT_TRUE(cnetmod::json::document(std::uint64_t{2}) ==
        cnetmod::json::document(std::int64_t{2}));
    ASSERT_FALSE(cnetmod::json::document(std::uint64_t{2}) ==
        cnetmod::json::document(std::int64_t{-2}));
}

TEST(json_codec_spi_accepts_an_application_codec)
{
    auto encoded = cnetmod::json::write<custom_document, custom_codec>(
        custom_document{"payload"});
    ASSERT_TRUE(encoded.has_value());
    ASSERT_EQ(*encoded, std::string{"custom:payload"});

    auto decoded = cnetmod::json::parse<custom_document, custom_codec>("wire");
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->value, std::string{"wire"});
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

    const auto lenient = cnetmod::json::parse<sample_document,
        cnetmod::json::lenient_codec>(
        R"({"name":"orders","count":7,"extra":true})");
    ASSERT_TRUE(lenient.has_value());

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

TEST(json_template_offloads_and_honours_cancellation)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::thread_pool cpu_pool{2};
    cnetmod::application::json_template json{*io, cpu_pool};
    std::optional<std::expected<sample_document, std::error_code>> parsed;
    std::optional<std::expected<std::string, std::error_code>> written;
    std::optional<std::expected<sample_document, std::error_code>> cancelled;

    auto run = [&]() -> cnetmod::task<void>
    {
        parsed = co_await json.parse<sample_document>(
            R"({"name":"orders","count":7})");
        written = co_await json.write(sample_document{"events", 9});
        cnetmod::cancel_token cancellation;
        cancellation.cancel();
        cancelled = co_await json.parse<sample_document>("{}", &cancellation);
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    operation.handle().promise().result();
    cpu_pool.request_stop();

    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->has_value());
    ASSERT_EQ(parsed->value().name, std::string{"orders"});
    ASSERT_EQ(parsed->value().count, 7);
    ASSERT_TRUE(written.has_value());
    ASSERT_TRUE(written->has_value());
    ASSERT_TRUE(written->value().contains("events"));
    ASSERT_TRUE(cancelled.has_value());
    ASSERT_FALSE(cancelled->has_value());
    ASSERT_EQ(cancelled->error(),
        std::make_error_code(std::errc::operation_canceled));
}

RUN_TESTS()
