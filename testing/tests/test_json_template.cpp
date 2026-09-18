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

using cnetmod_test::custom_codec;
using cnetmod_test::custom_document;
using cnetmod_test::sample_document;

TEST(glaze_is_the_default_json_codec)
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
