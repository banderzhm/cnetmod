#include "test_framework.hpp"

import std;
import cnetmod.application;
import cnetmod.coro.task;

namespace application = cnetmod::application;

namespace {

struct greeting_service
{
    virtual ~greeting_service() = default;
    [[nodiscard]] virtual auto greeting() const -> std::string_view = 0;
};

struct default_greeting_service final : greeting_service
{
    explicit default_greeting_service(std::string value)
        : value_(std::move(value))
    {
    }

    [[nodiscard]] auto greeting() const -> std::string_view override
    {
        return value_;
    }

private:
    std::string value_;
};

} // namespace

TEST(application_service_registry_is_typed_and_freezable)
{
    application::service_registry services;
    auto& greeting = services.emplace<greeting_service,
        default_greeting_service>("hello");

    ASSERT_EQ(greeting.greeting(), "hello");
    ASSERT_EQ(services.require<greeting_service>().greeting(), "hello");
    ASSERT_TRUE(services.find<int>() == nullptr);
    services.freeze();
    ASSERT_TRUE(services.frozen());

    bool rejected = false;
    try
    {
        services.emplace<int>(42);
    }
    catch (const std::logic_error&)
    {
        rejected = true;
    }
    ASSERT_TRUE(rejected);
}

TEST(application_lifecycle_starts_in_order_and_stops_in_reverse)
{
    application::application_lifecycle lifecycle;
    std::vector<int> events;
    lifecycle.on_start([&events]() -> cnetmod::task<
                                       std::expected<void, std::error_code>>
        {
            events.push_back(1);
            co_return {};
        });
    lifecycle.on_start([&events]() -> cnetmod::task<
                                       std::expected<void, std::error_code>>
        {
            events.push_back(2);
            co_return {};
        });
    lifecycle.on_stop([&events]() -> cnetmod::task<
                                      std::expected<void, std::error_code>>
        {
            events.push_back(3);
            co_return {};
        });
    lifecycle.on_stop([&events]() -> cnetmod::task<
                                      std::expected<void, std::error_code>>
        {
            events.push_back(4);
            co_return {};
        });

    ASSERT_TRUE(cnetmod::sync_wait(lifecycle.start()).has_value());
    ASSERT_TRUE(cnetmod::sync_wait(lifecycle.stop()).has_value());
    ASSERT_TRUE(events == std::vector<int>({1, 2, 4, 3}));
}

TEST(http_application_runs_hooks_and_accepts_programmatic_shutdown)
{
    application::http_application app{{
        .name = "application-test",
        .logging = {.manage_lifecycle = false},
        .http = {.address = "127.0.0.1", .port = 0, .access_logging = false},
        .management = {.enabled = false},
        .install_signal_handlers = false,
        .shutdown_timeout = std::chrono::seconds{1},
    }};
    std::atomic<int> lifecycle_calls{};
    app.lifecycle().on_start([&lifecycle_calls]() -> cnetmod::task<
                                                      std::expected<void, std::error_code>>
        {
            lifecycle_calls.fetch_add(1, std::memory_order_relaxed);
            co_return {};
        });
    app.lifecycle().on_stop([&lifecycle_calls]() -> cnetmod::task<
                                                     std::expected<void, std::error_code>>
        {
            lifecycle_calls.fetch_add(1, std::memory_order_relaxed);
            co_return {};
        });

    std::optional<std::expected<void, std::error_code>> result;
    std::jthread runner([&]
        {
            result = app.run();
        });
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds{2};
    while (app.state() != application::application_state::running &&
        std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{5});

    ASSERT_TRUE(app.state() == application::application_state::running);
    app.stop();
    runner.join();

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->has_value());
    ASSERT_EQ(lifecycle_calls.load(std::memory_order_relaxed), 2);
    ASSERT_TRUE(app.services().frozen());
    ASSERT_TRUE(app.state() == application::application_state::stopped);
}

RUN_TESTS();
