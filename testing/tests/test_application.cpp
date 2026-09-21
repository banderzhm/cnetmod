#include "test_framework.hpp"
#include <cnetmod/config.hpp>

import std;
import nlohmann.json;
import cnetmod.application;
import cnetmod.core;
import cnetmod.core.error;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.executor.pool;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.compress;
import cnetmod.protocol.http.middleware.graceful_shutdown;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.observability;
import cnetmod.observability.otlp;
#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
import cnetmod.protocol.mysql;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
import cnetmod.protocol.mongodb;
#endif

namespace application = cnetmod::application;

TEST(request_drain_never_schedules_beyond_remaining_budget)
{
    cnetmod::net_init network;
    for (const auto budget : {std::chrono::milliseconds{0}, std::chrono::milliseconds{5}})
    {
        auto io = cnetmod::make_io_context();
        cnetmod::shutdown_handler shutdown;
        cnetmod::socket peer;
        cnetmod::http::header_map headers;
        cnetmod::http::response response;
        cnetmod::http::request_context request{*io, peer, "GET", "/", headers, {}, response, {}};
        bool children_cancelled = false;
        auto next = [&]() -> cnetmod::task<void>
        {
            auto child = [&](cnetmod::cancel_token& token)
                -> cnetmod::task<std::expected<void, std::error_code>>
            {
                co_return co_await cnetmod::async_timer_wait(*io, std::chrono::seconds{10}, token);
            };
            const auto [first, second, direct] = co_await cnetmod::when_all(
                request.with_deadline(child), request.with_deadline(child),
                cnetmod::async_timer_wait(*io, std::chrono::seconds{10}, request.cancellation_token()));
            const auto cancelled = cnetmod::make_error_code(cnetmod::errc::operation_aborted);
            children_cancelled = !first && !second && !direct &&
                first.error() == cancelled && second.error() == cancelled && direct.error() == cancelled;
        };
        auto middleware = shutdown.track_middleware();
        auto tracked = middleware(request, next);
        tracked.handle().resume();
        ASSERT_EQ(shutdown.in_flight(), 1);
        unsigned sleeps = 0;
        bool drained = true;
        bool bounded = true;
        auto run = [&]() -> cnetmod::task<void>
        {
            auto sleep = [&](auto duration) -> cnetmod::task<void>
            {
                ++sleeps;
                bounded = bounded && duration > std::chrono::steady_clock::duration::zero() && duration <= budget;
                co_await cnetmod::async_sleep(*io, duration);
            };
            drained = co_await shutdown.drain(sleep, budget);
            shutdown.cancel_requests();
            while (!tracked.handle().done())
                co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
            tracked.handle().promise().result();
            io->stop();
        };
        auto operation = run();
        operation.handle().resume();
        io->run();
        operation.handle().promise().result();
        ASSERT_FALSE(drained);
        ASSERT_TRUE(request.cancellation_token().is_cancelled());
        ASSERT_TRUE(children_cancelled);
        ASSERT_TRUE(bounded);
        ASSERT_TRUE(budget.count() == 0 ? sleeps == 0 : sleeps > 0);
        ASSERT_EQ(shutdown.in_flight(), 0);
    }
}

TEST(request_cancellation_can_resume_completion_inline)
{
    struct inline_wait
    {
        cnetmod::cancel_token& token;

        auto await_ready() const noexcept -> bool
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> continuation) noexcept
        {
            token.coroutine_ = continuation;
            token.cancel_fn_ = [](cnetmod::cancel_token& cancelled) noexcept
            {
                if (cancelled.pending_.exchange(false))
                    cancelled.coroutine_.resume();
            };
            token.pending_.store(true);
        }

        void await_resume() noexcept {}
    };

    cnetmod::net_init network;
    for (bool child : {false, true})
    {
        auto io = cnetmod::make_io_context();
        cnetmod::shutdown_handler shutdown;
        cnetmod::socket peer;
        cnetmod::http::header_map headers;
        cnetmod::http::response response;
        cnetmod::http::request_context request{*io, peer, "GET", "/", headers, {}, response, {}};
        bool nested_cancelled = false;
        bool late_rejected = false;
        auto next = [&]() -> cnetmod::task<void>
        {
            auto wait = [&](cnetmod::cancel_token& token)
                -> cnetmod::task<std::expected<void, std::error_code>>
            {
                co_await inline_wait{token};
                shutdown.cancel_requests();
                request.cancel_pending_operations();
                cnetmod::http::response late_response;
                cnetmod::http::request_context late_request{*io, peer, "GET", "/late", headers, {}, late_response, {}};
                bool late_called = false;
                auto late_next = [&]() -> cnetmod::task<void>
                {
                    late_called = true;
                    co_return;
                };
                auto late_middleware = shutdown.track_middleware();
                co_await late_middleware(late_request, late_next);
                late_rejected = !late_called && late_response.status_code() == 503 && shutdown.in_flight() == 1;
                auto nested = co_await request.with_deadline([](cnetmod::cancel_token& inner)
                                                                 -> cnetmod::task<std::expected<void, std::error_code>>
                    {
                        if (inner.is_cancelled())
                            co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
                        co_return std::expected<void, std::error_code>{};
                    });
                nested_cancelled = !nested && nested.error() == cnetmod::make_error_code(cnetmod::errc::operation_aborted);
                co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
            };
            if (child)
                (void)co_await request.with_deadline(wait);
            else
                (void)co_await wait(request.cancellation_token());
        };
        auto middleware = shutdown.track_middleware();
        auto run = [&]() -> cnetmod::task<void>
        {
            co_await middleware(request, next);
            io->stop();
        };
        auto operation = run();
        operation.handle().resume();
        shutdown.cancel_requests();
        io->run();
        operation.handle().promise().result();
        ASSERT_TRUE(nested_cancelled);
        ASSERT_TRUE(late_rejected);
        ASSERT_EQ(shutdown.in_flight(), 0);
    }
}

#ifndef CNETMOD_PLATFORM_MACOS
// Xcode 15's coroutine ABI terminates inside noexcept final_suspend when a
// std::system_error crosses nested Clang module task continuations. cnetmod's
// portable application error contract remains std::expected/error_code.
TEST(request_middleware_preserves_handler_system_error)
{
    auto io = cnetmod::make_io_context();
    cnetmod::shutdown_handler shutdown;
    cnetmod::socket peer;
    cnetmod::http::header_map headers;
    cnetmod::http::response response;
    cnetmod::http::request_context request{*io, peer, "GET", "/", headers, {}, response, {}};
    const auto original = std::make_error_code(std::errc::permission_denied);
    auto next = [&]() -> cnetmod::task<void>
    {
        throw std::system_error(original);
        co_return;
    };
    bool preserved = false;
    auto middleware = shutdown.track_middleware();
    auto operation = middleware(request, next);
    try
    {
        operation.handle().resume();
        if (operation.handle().done())
            operation.handle().promise().result();
    }
    catch (const std::system_error& error)
    {
        preserved = error.code() == original;
    }
    ASSERT_TRUE(operation.handle().done());
    ASSERT_TRUE(preserved);
    ASSERT_EQ(shutdown.in_flight(), 0);
}
#endif

TEST(request_child_cancellation_handles_completion_and_late_registration)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::socket peer;
    cnetmod::http::header_map headers;
    cnetmod::http::response response;
    cnetmod::http::request_context request{*io, peer, "GET", "/", headers, {}, response, {}};
    bool completed = false;
    bool late_cancelled = false;
    std::weak_ptr<int> factory_lifetime;
    auto run = [&]() -> cnetmod::task<void>
    {
        {
            auto result = co_await request.with_deadline([](cnetmod::cancel_token& token)
                                                             -> cnetmod::task<std::expected<void, std::error_code>>
                {
                    if (token.is_cancelled())
                        co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
                    co_return std::expected<void, std::error_code>{};
                });
            completed = result.has_value();
        }
        request.cancel_pending_operations();
        request.cancel_pending_operations();
        auto ownership = std::make_shared<int>(42);
        factory_lifetime = ownership;
        {
            auto operation = request.with_deadline(
                [retained = std::move(ownership), &io](cnetmod::cancel_token& token)
                    -> cnetmod::task<std::expected<void, std::error_code>>
                {
                    if (*retained != 42)
                        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
                    co_return co_await cnetmod::async_timer_wait(*io, std::chrono::seconds{10}, token);
                });
            auto result = co_await std::move(operation);
            late_cancelled = !result && result.error() == cnetmod::make_error_code(cnetmod::errc::operation_aborted);
        }
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    operation.handle().promise().result();
    ASSERT_TRUE(completed);
    ASSERT_TRUE(late_cancelled);
    ASSERT_TRUE(factory_lifetime.expired());
}

TEST(metric_aggregation_limits_are_validated_and_require_restart)
{
    application::application_configuration active;
    auto candidate = active;
    candidate.observability.otlp.max_metric_instruments = 0;
    ASSERT_FALSE(application::validate_configuration(candidate).has_value());
    candidate.observability.otlp.max_metric_instruments = 16;
    candidate.observability.otlp.max_metric_attribute_sets = 0;
    ASSERT_TRUE(application::validate_configuration(candidate).has_value());
    const auto change = application::reload_safe_configuration(active, candidate);
    ASSERT_TRUE(change.has_value());
    ASSERT_TRUE(change->restart_required);
    ASSERT_EQ(active.observability.otlp.max_metric_instruments, 128U);
    ASSERT_EQ(active.observability.otlp.max_metric_attribute_sets, 256U);
}

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

class fake_service final : public application::managed_service
{
public:
    application::recovery_policy recovery_options;
    mutable std::size_t key_reads = 0;
    bool reject_key_reads = false;
    bool reject_dependencies_after_start = false;
    bool has_started = false;
    bool retain_failed_start = false;
    bool pending_cleanup = false;
    std::string reserved_task_name;
    bool reserved_task_settled = false;
    std::error_code stop_error;
    unsigned stop_failures_remaining = 0;
    std::error_code start_error;
    std::chrono::milliseconds start_delay{0};
    bool start_cancelled = false;
    bool signal_start_deadline = false;
    std::chrono::milliseconds stop_delay{0};
    bool stop_cancelled = false;
    bool finish_stop_after_cancellation = false;
    bool stop_settled = false;
    std::size_t probes = 0;
    bool reject_probe = false;
    std::error_code probe_error;
    unsigned probe_failure_kind = 0;
    std::chrono::milliseconds probe_delay{0};
    bool probe_cancelled = false;
    bool probe_settled = false;
    std::atomic<bool> probe_entered{false};
    std::atomic<bool> dependency_available{true};

    auto recovery() const noexcept -> application::recovery_policy override
    {
        return recovery_options;
    }

    fake_service(application::service_key key,
        std::vector<application::service_key> dependencies,
        application::service_requirement requirement,
        std::shared_ptr<std::vector<std::string>> events,
        std::size_t failures_before_success = 0, bool background_failure = false)
        : key_(std::move(key)), dependencies_(std::move(dependencies)), requirement_(requirement), events_(std::move(events)), failures_remaining_(failures_before_success), background_failure_(background_failure)
    {
    }

    [[nodiscard]] auto key() const -> application::service_key override
    {
        ++key_reads;
        if (reject_key_reads)
            throw std::runtime_error("service identity must be cached after registration");
        return key_;
    }

    [[nodiscard]] auto dependencies() const
        -> std::vector<application::service_key> override
    {
        if (reject_dependencies_after_start && has_started)
            throw std::bad_alloc{};
        return dependencies_;
    }

    [[nodiscard]] auto requirement() const noexcept
        -> application::service_requirement override
    {
        return requirement_;
    }

    auto start(application::service_context& context)
        -> cnetmod::task<std::expected<void, std::error_code>> override
    {
        events_->push_back("start:" + key_.canonical_name());
        pending_cleanup = retain_failed_start;
        if (signal_start_deadline)
        {
            context.cancellation.cancel_due_to_deadline();
            co_return std::unexpected(std::make_error_code(std::errc::timed_out));
        }
        if (start_error)
            co_return std::unexpected(start_error);
        if (start_delay.count() > 0)
        {
            (void)co_await cnetmod::async_timer_wait(context.io, start_delay, context.cancellation);
            start_cancelled = context.cancellation.is_cancelled();
        }
        if (!dependency_available.load())
            co_return std::unexpected(std::make_error_code(std::errc::connection_refused));
        if (failures_remaining_ > 0U)
        {
            --failures_remaining_;
            co_return std::unexpected(
                std::make_error_code(std::errc::connection_refused));
        }
        up_ = true;
        pending_cleanup = false;
        has_started = true;
        if (!reserved_task_name.empty())
            co_return context.supervisor.supervise(reserved_task_name,
                [this, io = &context.io](cnetmod::cancel_token& token)
                    -> cnetmod::task<std::expected<void, std::error_code>>
                {
                    (void)co_await cnetmod::async_timer_wait(*io, std::chrono::seconds{30}, token);
                    reserved_task_settled = true;
                    co_return {};
                });
        if (background_failure_)
            co_return context.supervisor.supervise("required-worker", [io = &context.io](cnetmod::cancel_token& token) -> cnetmod::task<std::expected<void, std::error_code>>
                {
                    auto waited = co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{25}, token);
                    if (!waited)
                        co_return std::unexpected(waited.error());
                    co_return std::unexpected(std::make_error_code(std::errc::permission_denied));
                },
                {.budget = std::chrono::milliseconds{0}}, true);
        co_return {};
    }

    auto cleanup_required() const noexcept -> bool override
    {
        return pending_cleanup;
    }

    auto stop(application::service_context& context)
        -> cnetmod::task<std::expected<void, std::error_code>> override
    {
        events_->push_back("stop:" + key_.canonical_name());
        if (stop_failures_remaining != 0)
        {
            --stop_failures_remaining;
            co_return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
        }
        if (stop_error)
            co_return std::unexpected(stop_error);
        stop_cancelled = context.cancellation.is_cancelled();
        if (stop_delay.count() > 0)
        {
            const auto waited = co_await cnetmod::async_timer_wait(context.io,
                stop_delay, context.cancellation);
            stop_cancelled = context.cancellation.is_cancelled();
            stop_settled = !context.cancellation.pending_.load();
            if (!waited && !finish_stop_after_cancellation)
                co_return std::unexpected(waited.error());
        }
        up_ = false;
        if (background_failure_)
            co_return std::unexpected(std::make_error_code(std::errc::io_error));
        pending_cleanup = false;
        co_return {};
    }

    auto probe(application::service_context& context)
        -> cnetmod::task<application::health_report> override
    {
        ++probes;
        probe_entered.store(true, std::memory_order_release);
        if (probe_failure_kind == 1)
            throw std::bad_alloc{};
        if (probe_failure_kind == 2)
            throw std::runtime_error("private-probe-detail");
        if (probe_error)
            co_return application::health_report{
                .status = application::service_health::down,
                .message = {},
                .error = probe_error,
            };
        if (probe_delay.count() > 0)
        {
            const auto result = co_await cnetmod::async_timer_wait(context.io,
                probe_delay, context.cancellation);
            probe_cancelled = context.cancellation.is_cancelled();
            probe_settled = !context.cancellation.pending_.load();
            if (!result)
                co_return application::health_report{.status = application::service_health::down,
                    .message = "probe interrupted",
                    .error = result.error()};
        }
        co_return application::health_report{
            .status = up_ && dependency_available.load() && !reject_probe ? application::service_health::up
                                                                          : application::service_health::down,
            .message = up_ ? "available" : "unavailable",
        };
    }

private:
    application::service_key key_;
    std::vector<application::service_key> dependencies_;
    application::service_requirement requirement_;
    std::shared_ptr<std::vector<std::string>> events_;
    std::size_t failures_remaining_ = 0;
    bool up_ = false;
    bool background_failure_ = false;
};

} // namespace

TEST(application_late_stop_success_is_not_repeated)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io};
    application::service_registry services;
    application::health_registry health;
    application::task_supervisor supervisor{*io};
    auto events = std::make_shared<std::vector<std::string>>();
    auto dependency = std::make_shared<fake_service>(application::service_key{"dependency"},
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    auto service = std::make_shared<fake_service>(application::service_key{"late-stop"},
        std::vector<application::service_key>{dependency->key()}, application::service_requirement::required, events);
    service->stop_delay = std::chrono::seconds{10};
    service->finish_stop_after_cancellation = true;
    ASSERT_TRUE(services.manage(dependency));
    ASSERT_TRUE(health.add(dependency));
    ASSERT_TRUE(services.manage(service));
    ASSERT_TRUE(health.add(service));
    services.freeze();
    application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health,
        {.service_stop_timeout = std::chrono::milliseconds{10}}};
    auto run = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE(co_await lifecycle.start());
        const auto result = co_await lifecycle.stop();
        ASSERT_FALSE(result.has_value());
        if (!result)
            ASSERT_TRUE(result.error() == std::errc::timed_out);
        ASSERT_TRUE(service->stop_cancelled);
        ASSERT_TRUE(service->stop_settled);
        ASSERT_TRUE(lifecycle.started_services().empty());
        for (const auto& snapshot : health.snapshots())
            ASSERT_TRUE(snapshot.report.status == application::service_health::stopped);
        ASSERT_TRUE(co_await lifecycle.stop());
        ASSERT_EQ(events->size(), 4U);
        if (events->size() == 4)
        {
            ASSERT_EQ(events->at(0), "start:" + dependency->key().canonical_name());
            ASSERT_EQ(events->at(1), "start:" + service->key().canonical_name());
            ASSERT_EQ(events->at(2), "stop:" + service->key().canonical_name());
            ASSERT_EQ(events->at(3), "stop:" + dependency->key().canonical_name());
        }
        io->stop();
    };
    auto operation = run();
    io->post(operation.handle());
    io->run();
    ASSERT_TRUE(operation.handle().done());
    operation.handle().promise().result();
}

TEST(application_late_start_success_remains_owned_for_rollback)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io};
    application::service_registry services;
    application::health_registry health;
    application::task_supervisor supervisor{*io};
    auto events = std::make_shared<std::vector<std::string>>();
    auto service = std::make_shared<fake_service>(application::service_key{"late-success"},
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    service->start_delay = std::chrono::seconds{10};
    ASSERT_TRUE(services.manage(service));
    ASSERT_TRUE(health.add(service));
    services.freeze();
    application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health,
        {.service_start_timeout = std::chrono::milliseconds{10}}};
    auto run = [&]() -> cnetmod::task<void>
    {
        auto result = co_await lifecycle.start();
        ASSERT_FALSE(result.has_value());
        if (!result)
            ASSERT_TRUE(result.error() == std::errc::timed_out);
        ASSERT_TRUE(service->has_started);
        ASSERT_TRUE(service->start_cancelled);
        ASSERT_EQ(events->size(), 2U);
        if (events->size() == 2)
            ASSERT_TRUE(events->back().starts_with("stop:"));
        ASSERT_TRUE(lifecycle.started_services().empty());
        io->stop();
    };
    auto operation = run();
    io->post(operation.handle());
    io->run();
    ASSERT_TRUE(operation.handle().done());
    operation.handle().promise().result();
}

TEST(application_rejects_invalid_custom_service_policy_without_partial_registration)
{
    auto events = std::make_shared<std::vector<std::string>>();
    auto service = std::make_shared<fake_service>(application::service_key{"custom", "test"},
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    service->recovery_options.jitter = std::numeric_limits<double>::quiet_NaN();
    application::service_registry registry;
    auto untyped = registry.manage(service);
    ASSERT_FALSE(untyped.has_value());
    ASSERT_EQ(untyped.error(), std::make_error_code(std::errc::invalid_argument));
    auto typed = registry.add_managed_named("test", service);
    ASSERT_FALSE(typed.has_value());
    ASSERT_EQ(typed.error(), std::make_error_code(std::errc::invalid_argument));
    ASSERT_EQ(registry.managed_size(), 0U);
    ASSERT_EQ(registry.size(), 0U);
    bool routes_configured = false;
    auto built = application::application_builder{"invalid-custom-policy"}
                     .service(service)
                     .routes([&](cnetmod::http::router&)
                         {
                             routes_configured = true;
                         })
                     .build();
    ASSERT_FALSE(built.has_value());
    ASSERT_EQ(built.error(), std::make_error_code(std::errc::invalid_argument));
    ASSERT_FALSE(routes_configured);
    ASSERT_TRUE(events->empty());
    service->recovery_options = {};
    service->has_started = true;
    service->reject_dependencies_after_start = true;
    untyped = registry.manage(service);
    ASSERT_FALSE(untyped.has_value());
    ASSERT_EQ(untyped.error(), std::make_error_code(std::errc::not_enough_memory));
    typed = registry.add_managed_named("test", service);
    ASSERT_FALSE(typed.has_value());
    ASSERT_EQ(typed.error(), std::make_error_code(std::errc::not_enough_memory));
    ASSERT_EQ(registry.managed_size(), 0U);
    ASSERT_EQ(registry.size(), 0U);
    service->has_started = false;
    service->reject_dependencies_after_start = false;
    ASSERT_TRUE(registry.add_managed_named("test", service));
    ASSERT_EQ(registry.managed_size(), 1U);
    ASSERT_EQ(registry.size(), 1U);
}

TEST(application_service_registry_supports_named_bindings_and_freeze)
{
    application::service_registry services;
    auto primary = services.emplace_named<greeting_service,
        default_greeting_service>("primary", "hello");
    auto secondary = services.emplace_named<greeting_service,
        default_greeting_service>("secondary", "world");

    ASSERT_TRUE(primary.has_value());
    ASSERT_TRUE(secondary.has_value());
    ASSERT_EQ(primary->get().greeting(), "hello");
    ASSERT_EQ(secondary->get().greeting(), "world");
    ASSERT_TRUE(services.find<greeting_service>("missing") == nullptr);

    const auto duplicate = services.emplace_named<greeting_service,
        default_greeting_service>("primary", "duplicate");
    ASSERT_FALSE(duplicate.has_value());
    ASSERT_EQ(duplicate.error(), std::make_error_code(std::errc::file_exists));
    services.freeze();
    ASSERT_TRUE(services.frozen());
}

TEST(application_service_registry_rejects_missing_and_cyclic_dependencies)
{
    const auto events = std::make_shared<std::vector<std::string>>();
    application::service_registry missing;
    ASSERT_TRUE(missing.manage(std::make_shared<fake_service>(
        application::service_key{"consumer"},
        std::vector<application::service_key>{{"database"}},
        application::service_requirement::required, events)));
    ASSERT_FALSE(missing.validate_dependencies().has_value());

    application::service_registry cyclic;
    ASSERT_TRUE(cyclic.manage(std::make_shared<fake_service>(
        application::service_key{"a"},
        std::vector<application::service_key>{{"b"}},
        application::service_requirement::required, events)));
    ASSERT_TRUE(cyclic.manage(std::make_shared<fake_service>(
        application::service_key{"b"},
        std::vector<application::service_key>{{"a"}},
        application::service_requirement::required, events)));
    ASSERT_FALSE(cyclic.validate_dependencies().has_value());
}

TEST(application_lifecycle_rolls_back_only_successful_services)
{
    for (const bool enabled : {false, true})
    {
        auto io = cnetmod::make_io_context();
        cnetmod::observability::telemetry_hub telemetry{*io,
            {.endpoint = "http://127.0.0.1:1/v1/traces",
                .export_traces = enabled,
                .export_metrics = enabled,
                .export_logs = enabled}};
        ASSERT_EQ(static_cast<bool>(telemetry.spans()), enabled);
        application::service_registry services;
        application::health_registry health;
        application::task_supervisor supervisor{*io};
        const auto events = std::make_shared<std::vector<std::string>>();
        const application::service_key database{"database"};
        const application::service_key api{"api"};
        auto database_service = std::make_shared<fake_service>(database,
            std::vector<application::service_key>{},
            application::service_requirement::required, events);
        auto api_service = std::make_shared<fake_service>(api,
            std::vector<application::service_key>{database},
            application::service_requirement::required, events, true);
        ASSERT_TRUE(services.manage(database_service));
        ASSERT_TRUE(services.manage(api_service));
        ASSERT_TRUE(health.add(database_service));
        ASSERT_TRUE(health.add(api_service));
        services.freeze();
        application::service_lifecycle lifecycle{*io, telemetry, services,
            supervisor, health};

        std::optional<std::expected<void, std::error_code>> result;
        auto run = [&]() -> cnetmod::task<void>
        {
            result = co_await lifecycle.start();
            const auto settled = co_await telemetry.shutdown(std::chrono::milliseconds{20},
                std::chrono::milliseconds{100});
            ASSERT_TRUE(settled.has_value());
            io->stop();
        };
        auto operation = run();
        operation.handle().resume();
        io->run();

        ASSERT_TRUE(result.has_value());
        ASSERT_FALSE(result->has_value());
        ASSERT_TRUE(*events == std::vector<std::string>({"start:database:default", "start:api:default", "stop:database:default"}));
        ASSERT_TRUE(lifecycle.started_services().empty());
        ASSERT_TRUE(lifecycle.last_failure().has_value());
        ASSERT_TRUE(lifecycle.last_failure()->service == api);
        ASSERT_TRUE(lifecycle.last_failure()->phase ==
            application::lifecycle_phase::startup);
        ASSERT_EQ(lifecycle.last_failure()->error,
            std::make_error_code(std::errc::connection_refused));
        if (!enabled)
        {
            ASSERT_FALSE(telemetry.metrics().render_openmetrics().contains("application_service_"));
            ASSERT_EQ(telemetry.statistics().accepted, std::uint64_t{0});
        }
    }
}

TEST(application_health_hysteresis_controls_readiness)
{
    const auto events = std::make_shared<std::vector<std::string>>();
    auto service = std::make_shared<fake_service>(
        application::service_key{"optional"},
        std::vector<application::service_key>{},
        application::service_requirement::optional, events);
    application::health_registry health{{
        .failures_before_down = 2,
        .successes_before_up = 2,
    }};
    ASSERT_TRUE(health.add(service));
    health.mark_running();
    health.update(service->key(), {.status = application::service_health::up});
    ASSERT_FALSE(health.ready());
    health.update(service->key(), {.status = application::service_health::up});
    ASSERT_TRUE(health.ready());
    health.update(service->key(), {.status = application::service_health::down});
    ASSERT_FALSE(health.ready());
    ASSERT_TRUE(health.live());
}

TEST(application_health_sampling_skips_metadata_without_skipping_probes)
{
    for (const int mode : {0, 1, 2})
    {
        auto io = cnetmod::make_io_context();
        cnetmod::observability::telemetry_hub telemetry{*io,
            {.endpoint = "http://127.0.0.1:1/v1/traces",
                .export_traces = mode != 0,
                .export_metrics = false,
                .export_logs = false}};
        telemetry.set_sampling_ratio(mode == 1 ? 0.0 : 1.0);
        application::task_supervisor supervisor{*io};
        cnetmod::cancel_token cancellation;
        application::service_context context{*io, telemetry, supervisor,
            cancellation, {}};
        const auto events = std::make_shared<std::vector<std::string>>();
        auto service = std::make_shared<fake_service>(
            application::service_key{"health-metadata-service"},
            std::vector<application::service_key>{},
            application::service_requirement::required, events);
        application::health_registry health{{.successes_before_up = 1}};
        ASSERT_TRUE(health.add(service));
        health.mark_running();
        bool completed = false;
        auto run = [&]() -> cnetmod::task<void>
        {
            ASSERT_TRUE(co_await service->start(context));
            service->key_reads = 0;
            service->reject_key_reads = true;
            co_await health.refresh(context);
            ASSERT_EQ(service->key_reads, 0U);
            ASSERT_EQ(service->probes, 1U);
            ASSERT_TRUE(health.ready());
            ASSERT_TRUE(co_await service->stop(context));
            completed = true;
            io->stop();
        };
        auto operation = run();
        operation.handle().resume();
        io->run();
        ASSERT_TRUE(completed);
        if (mode != 2)
            ASSERT_EQ(telemetry.statistics().accepted, 0U);
    }
}

TEST(application_service_shutdown_preserves_expired_parent_deadline)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    application::service_registry services;
    application::health_registry health;
    application::task_supervisor supervisor{*io};
    const auto events = std::make_shared<std::vector<std::string>>();
    auto service = std::make_shared<fake_service>(
        application::service_key{"deadline-service"},
        std::vector<application::service_key>{},
        application::service_requirement::required, events);
    ASSERT_TRUE(services.manage(service));
    ASSERT_TRUE(health.add(service));
    services.freeze();
    application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health};
    bool completed = false;
    auto run = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE(co_await lifecycle.start());
        const auto stopped = co_await lifecycle.stop(
            cnetmod::deadline::at(std::chrono::steady_clock::now()));
        ASSERT_FALSE(stopped.has_value());
        ASSERT_EQ(stopped.error(), std::make_error_code(std::errc::timed_out));
        ASSERT_EQ(events->size(), 1U);
        ASSERT_EQ(lifecycle.started_services().size(), 1U);
        ASSERT_TRUE(co_await lifecycle.stop());
        ASSERT_TRUE(lifecycle.started_services().empty());
        ASSERT_EQ(events->size(), 2U);
        completed = true;
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(completed);
}

TEST(application_optional_timeout_preserves_owning_startup_budget)
{
    for (const bool total_budget : {false, true})
    {
        auto io = cnetmod::make_io_context();
        cnetmod::observability::telemetry_hub telemetry{*io,
            {.export_traces = false, .export_metrics = false, .export_logs = false}};
        application::service_registry services;
        application::health_registry health;
        application::task_supervisor supervisor{*io};
        const auto events = std::make_shared<std::vector<std::string>>();
        auto service = std::make_shared<fake_service>(application::service_key{"budget-owner"},
            std::vector<application::service_key>{}, application::service_requirement::optional, events);
        service->signal_start_deadline = true;
        ASSERT_TRUE(services.manage(service));
        ASSERT_TRUE(health.add(service));
        services.freeze();
        application::lifecycle_policy policy;
        policy.service_start_timeout = std::chrono::seconds{total_budget ? 20 : 10};
        policy.total_start_timeout = std::chrono::seconds{total_budget ? 10 : 20};
        application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health, policy};
        auto run = [&]() -> cnetmod::task<void>
        {
            // Inject the timer's cancellation cause without depending on which
            // of the child/group timer callbacks the platform dispatches first.
            const auto result = co_await lifecycle.start();
            ASSERT_EQ(result.has_value(), !total_budget);
            if (total_budget && !result)
                ASSERT_EQ(result.error(), std::make_error_code(std::errc::timed_out));
            ASSERT_TRUE(co_await lifecycle.stop());
            ASSERT_TRUE(lifecycle.started_services().empty());
            io->stop();
        };
        auto operation = run();
        operation.handle().resume();
        io->run();
        operation.handle().promise().result();
    }
}

TEST(application_service_stop_error_retains_health_and_ownership)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    application::service_registry services;
    application::health_registry health;
    application::task_supervisor supervisor{*io};
    const auto events = std::make_shared<std::vector<std::string>>();
    auto service = std::make_shared<fake_service>(application::service_key{"stop-error"},
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    ASSERT_TRUE(services.manage(service));
    ASSERT_TRUE(health.add(service));
    services.freeze();
    application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health};
    bool completed = false;
    auto run = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE(co_await lifecycle.start());
        service->stop_error = std::make_error_code(std::errc::permission_denied);
        const auto stopped = co_await lifecycle.stop();
        ASSERT_FALSE(stopped.has_value());
        ASSERT_EQ(stopped.error(), service->stop_error);
        ASSERT_TRUE(lifecycle.last_failure().has_value());
        ASSERT_EQ(lifecycle.last_failure()->error, service->stop_error);
        ASSERT_EQ(lifecycle.started_services().size(), 1U);
        ASSERT_TRUE(health.snapshots().front().report.status == application::service_health::stopping);
        ASSERT_EQ(health.snapshots().front().report.error, service->stop_error);
        service->stop_error.clear();
        ASSERT_TRUE(co_await lifecycle.stop());
        ASSERT_TRUE(lifecycle.started_services().empty());
        ASSERT_TRUE(health.snapshots().front().report.status == application::service_health::stopped);
        ASSERT_FALSE(health.snapshots().front().report.error);
        completed = true;
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(completed);
}

TEST(application_stop_deadline_cancels_and_settles_each_service_independently)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    application::service_registry services;
    application::health_registry health;
    application::task_supervisor supervisor{*io};
    const auto events = std::make_shared<std::vector<std::string>>();
    auto slow = std::make_shared<fake_service>(application::service_key{"z-slow"},
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    auto fast = std::make_shared<fake_service>(application::service_key{"a-fast"},
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    for (const auto& service : {slow, fast})
    {
        ASSERT_TRUE(services.manage(service));
        ASSERT_TRUE(health.add(service));
    }
    services.freeze();
    application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health,
        {.service_stop_timeout = std::chrono::milliseconds{20}}};
    bool completed = false;
    auto run = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE(co_await lifecycle.start());
        slow->stop_delay = std::chrono::seconds{10};
        const auto begin = std::chrono::steady_clock::now();
        const auto stopped = co_await lifecycle.stop();
        ASSERT_FALSE(stopped.has_value());
        ASSERT_EQ(stopped.error(), std::make_error_code(std::errc::timed_out));
        ASSERT_TRUE(std::chrono::steady_clock::now() - begin < std::chrono::seconds{2});
        ASSERT_TRUE(slow->stop_cancelled);
        ASSERT_TRUE(slow->stop_settled);
        ASSERT_FALSE(fast->stop_cancelled);
        ASSERT_EQ(lifecycle.started_services().size(), 1U);
        ASSERT_TRUE(lifecycle.started_services().front() == slow->key());
        slow->stop_delay = std::chrono::milliseconds{0};
        ASSERT_TRUE(co_await lifecycle.stop());
        ASSERT_FALSE(slow->stop_cancelled);
        ASSERT_TRUE(lifecycle.started_services().empty());
        completed = true;
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(completed);
}

TEST(application_failed_stop_retains_transitive_dependencies_but_not_independent_services)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    application::service_registry services;
    application::health_registry health;
    application::task_supervisor supervisor{*io};
    auto events = std::make_shared<std::vector<std::string>>();
    auto database = std::make_shared<fake_service>(application::service_key{"database"},
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    auto repository = std::make_shared<fake_service>(application::service_key{"repository"},
        std::vector<application::service_key>{database->key()}, application::service_requirement::required, events);
    auto worker = std::make_shared<fake_service>(application::service_key{"worker"},
        std::vector<application::service_key>{repository->key()}, application::service_requirement::required, events);
    auto independent = std::make_shared<fake_service>(application::service_key{"independent"},
        std::vector<application::service_key>{}, application::service_requirement::optional, events);
    for (const auto& service : {database, repository, worker, independent})
    {
        ASSERT_TRUE(services.manage(service));
        ASSERT_TRUE(health.add(service));
    }
    services.freeze();
    application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health};
    bool completed = false;
    auto run = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE(co_await lifecycle.start());
        events->clear();
        worker->stop_error = std::make_error_code(std::errc::permission_denied);
        const auto stopped = co_await lifecycle.stop();
        ASSERT_FALSE(stopped.has_value());
        ASSERT_EQ(stopped.error(), worker->stop_error);
        ASSERT_TRUE(*events == std::vector<std::string>({"stop:worker:default", "stop:independent:default"}));
        ASSERT_EQ(lifecycle.started_services().size(), 3U);
        worker->stop_error.clear();
        events->clear();
        ASSERT_TRUE(co_await lifecycle.stop());
        ASSERT_TRUE(*events == std::vector<std::string>({"stop:worker:default", "stop:repository:default", "stop:database:default"}));
        ASSERT_TRUE(lifecycle.started_services().empty());
        completed = true;
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(completed);
}

#include "application_partial_start_cases.inc"

TEST(application_rollback_failure_does_not_replace_startup_failure)
{
    for (const bool explicit_error : {false, true})
    {
        auto io = cnetmod::make_io_context();
        cnetmod::observability::telemetry_hub telemetry{*io,
            {.export_traces = false, .export_metrics = false, .export_logs = false}};
        application::service_registry services;
        application::health_registry health;
        application::task_supervisor supervisor{*io};
        auto events = std::make_shared<std::vector<std::string>>();
        auto database = std::make_shared<fake_service>(application::service_key{"database"},
            std::vector<application::service_key>{}, application::service_requirement::required, events);
        auto worker = std::make_shared<fake_service>(application::service_key{"worker"},
            std::vector<application::service_key>{database->key()}, application::service_requirement::required, events, 1U);
        database->stop_error = std::make_error_code(std::errc::permission_denied);
        if (explicit_error)
            worker->start_error = std::make_error_code(std::errc::connection_refused);
        for (const auto& service : {database, worker})
        {
            ASSERT_TRUE(services.manage(service));
            ASSERT_TRUE(health.add(service));
        }
        services.freeze();
        application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health};
        bool completed = false;
        auto run = [&]() -> cnetmod::task<void>
        {
            const auto result = co_await lifecycle.start();
            ASSERT_FALSE(result.has_value());
            ASSERT_EQ(result.error(), std::make_error_code(std::errc::connection_refused));
            const auto failure = lifecycle.last_failure();
            const auto rollback = lifecycle.last_rollback_failure();
            ASSERT_TRUE(failure.has_value());
            ASSERT_TRUE(rollback.has_value());
            if (failure && rollback)
            {
                ASSERT_TRUE(failure->service == worker->key());
                ASSERT_TRUE(failure->phase == application::lifecycle_phase::startup);
                ASSERT_EQ(failure->error, result.error());
                ASSERT_TRUE(rollback->service == database->key());
                ASSERT_TRUE(rollback->phase == application::lifecycle_phase::rollback);
                ASSERT_EQ(rollback->error, database->stop_error);
            }
            ASSERT_EQ(lifecycle.started_services().size(), 1U);
            database->stop_error.clear();
            ASSERT_TRUE(co_await lifecycle.stop());
            completed = true;
            io->stop();
        };
        auto operation = run();
        operation.handle().resume();
        io->run();
        ASSERT_TRUE(completed);
    }
}

TEST(application_startup_rollback_retains_callers_cleanup_reserve)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io, {.export_traces = false, .export_metrics = false, .export_logs = false}};
    application::service_registry services;
    application::health_registry health;
    application::task_supervisor supervisor{*io};
    auto events = std::make_shared<std::vector<std::string>>();
    auto database = std::make_shared<fake_service>(application::service_key{"database"},
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    auto worker = std::make_shared<fake_service>(application::service_key{"worker"},
        std::vector<application::service_key>{database->key()}, application::service_requirement::required, events, 1U);
    database->stop_delay = std::chrono::seconds{10};
    for (const auto& service : {database, worker})
    {
        ASSERT_TRUE(services.manage(service));
        ASSERT_TRUE(health.add(service));
    }
    services.freeze();
    application::lifecycle_policy policy;
    policy.total_stop_timeout = std::chrono::seconds{2};
    application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health, policy};
    auto run = [&]() -> cnetmod::task<void>
    {
        const auto invalid = co_await lifecycle.start(std::chrono::milliseconds{-1});
        ASSERT_FALSE(invalid.has_value());
        ASSERT_TRUE(events->empty());
        const auto result = co_await lifecycle.start(std::chrono::milliseconds{1500});
        ASSERT_FALSE(result.has_value());
        ASSERT_EQ(result.error(), std::make_error_code(std::errc::connection_refused));
        ASSERT_FALSE(lifecycle.rollback_deadline().expired());
        ASSERT_TRUE(database->stop_cancelled && database->stop_settled);
        ASSERT_EQ(lifecycle.active_service_count(), std::size_t{1});
        database->stop_delay = std::chrono::milliseconds{0};
        ASSERT_TRUE(co_await lifecycle.stop(lifecycle.rollback_deadline()));
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(operation.handle().done());
    operation.handle().promise().result();
}

TEST(application_rollback_uses_frozen_dependency_metadata)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    application::task_supervisor supervisor{*io};
    application::service_registry services;
    application::health_registry health;
    auto events = std::make_shared<std::vector<std::string>>();
    auto parent = std::make_shared<fake_service>(application::service_key{"parent"},
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    auto child = std::make_shared<fake_service>(application::service_key{"child"},
        std::vector<application::service_key>{parent->key()}, application::service_requirement::required, events);
    parent->reject_dependencies_after_start = true;
    child->start_error = std::make_error_code(std::errc::permission_denied);
    ASSERT_TRUE(services.manage(parent));
    ASSERT_TRUE(services.manage(child));
    ASSERT_TRUE(health.add(parent));
    ASSERT_TRUE(health.add(child));
    services.freeze();
    application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health};
    bool completed = false;
    auto run = [&]() -> cnetmod::task<void>
    {
        const auto result = co_await lifecycle.start();
        ASSERT_FALSE(result.has_value());
        ASSERT_EQ(result.error(), child->start_error);
        const auto failure = lifecycle.last_failure();
        ASSERT_TRUE(failure.has_value());
        ASSERT_TRUE(failure->service == child->key());
        ASSERT_EQ(failure->error, child->start_error);
        ASSERT_FALSE(lifecycle.last_rollback_error());
        ASSERT_FALSE(lifecycle.last_rollback_failure().has_value());
        ASSERT_TRUE(lifecycle.started_services().empty());
        parent->reject_dependencies_after_start = false;
        ASSERT_TRUE(co_await lifecycle.stop());
        ASSERT_TRUE(lifecycle.started_services().empty());
        ASSERT_EQ(std::ranges::count(*events, "stop:" + parent->key().canonical_name()), 1);
        completed = true;
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(completed);
}

TEST(application_recovery_waits_for_consecutive_healthy_probes)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    application::service_registry services;
    application::health_registry health{{.failures_before_down = 1, .successes_before_up = 2}};
    application::task_supervisor supervisor{*io};
    auto events = std::make_shared<std::vector<std::string>>();
    auto service = std::make_shared<fake_service>(application::service_key{"recovering"},
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    ASSERT_TRUE(services.manage(service));
    ASSERT_TRUE(health.add(service));
    services.freeze();
    application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health};
    bool completed = false;
    auto run = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE(co_await lifecycle.start());
        health.mark_running();
        health.update(service->key(), {.status = application::service_health::down});
        ASSERT_TRUE(lifecycle.schedule_recovery(service->key()));
        ASSERT_TRUE(co_await supervisor.join());
        ASSERT_FALSE(health.ready());
        ASSERT_EQ(health.snapshots().front().consecutive_successes, 0U);
        cnetmod::cancel_token token;
        application::service_context context{*io, telemetry, supervisor, token, {}};
        co_await health.refresh(context);
        ASSERT_FALSE(health.ready());
        ASSERT_EQ(health.snapshots().front().consecutive_successes, 1U);
        co_await health.refresh(context);
        ASSERT_TRUE(health.ready());
        ASSERT_EQ(service->probes, 3U);
        ASSERT_TRUE(co_await lifecycle.stop());
        completed = true;
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(completed);
}

TEST(application_health_probe_preserves_error_codes_without_exception_details)
{
#ifdef CNETMOD_PLATFORM_MACOS
    // Xcode's module coroutine ABI cannot unwind exceptions across a child
    // task continuation. The portable explicit-error path remains covered.
    constexpr unsigned failure_kinds = 1;
#else
    constexpr unsigned failure_kinds = 3;
#endif
    for (const bool observed : {false, true})
    {
        for (unsigned failure = 0; failure < failure_kinds; ++failure)
        {
            auto io = cnetmod::make_io_context();
            cnetmod::observability::telemetry_hub telemetry{*io,
                {.export_traces = observed, .export_metrics = observed, .export_logs = observed}};
            application::task_supervisor supervisor{*io};
            application::health_registry health{{.failures_before_down = 1, .successes_before_up = 1}};
            auto events = std::make_shared<std::vector<std::string>>();
            auto service = std::make_shared<fake_service>(application::service_key{"probe-errors"},
                std::vector<application::service_key>{}, application::service_requirement::required, events);
            service->probe_failure_kind = failure;
            service->probe_error = std::make_error_code(std::errc::permission_denied);
            ASSERT_TRUE(health.add(service));
            bool completed = false;
            auto run = [&]() -> cnetmod::task<void>
            {
                cnetmod::cancel_token token;
                application::service_context context{*io, telemetry, supervisor, token, {}};
                co_await health.refresh(context);
                const auto snapshot = health.snapshots().front();
                ASSERT_TRUE(snapshot.report.status == application::service_health::down);
                ASSERT_EQ(snapshot.report.error, std::make_error_code(failure == 0 ? std::errc::permission_denied : failure == 1 ? std::errc::not_enough_memory
                                                                                                                                 : std::errc::io_error));
                ASSERT_TRUE(snapshot.report.message.empty());
                ASSERT_EQ(snapshot.consecutive_failures, 1U);
                ASSERT_TRUE(health.json().find("private-probe-detail") == std::string::npos);
                completed = true;
                io->stop();
            };
            auto operation = run();
            operation.handle().resume();
            io->run();
            ASSERT_TRUE(completed);
        }
    }
}

TEST(application_late_health_probe_does_not_overwrite_newer_state)
{
    for (const auto status : {application::service_health::down,
             application::service_health::stopping, application::service_health::stopped})
    {
        auto io = cnetmod::make_io_context();
        cnetmod::observability::telemetry_hub telemetry{*io,
            {.export_traces = false, .export_metrics = false, .export_logs = false}};
        application::task_supervisor supervisor{*io};
        application::health_registry health{{.failures_before_down = 1, .successes_before_up = 1}};
        auto events = std::make_shared<std::vector<std::string>>();
        auto service = std::make_shared<fake_service>(application::service_key{"late-probe"},
            std::vector<application::service_key>{}, application::service_requirement::required, events);
        service->probe_delay = std::chrono::milliseconds{30};
        ASSERT_TRUE(health.add(service));
        bool completed = false;
        auto run = [&]() -> cnetmod::task<void>
        {
            cnetmod::cancel_token token;
            application::service_context context{*io, telemetry, supervisor, token, {}};
            ASSERT_TRUE(co_await service->start(context));
            auto pending = health.refresh(context);
            pending.handle().resume();
            while (!service->probe_entered.load())
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
            health.update(service->key(), {.status = status, .message = "newer state"});
            const auto newer = health.snapshots().front();
            while (!pending.handle().done())
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
            const auto after = health.snapshots().front();
            ASSERT_TRUE(after.report.status == status);
            ASSERT_EQ(after.report.message, "newer state");
            ASSERT_EQ(after.revision, newer.revision);
            ASSERT_EQ(after.consecutive_successes, 0U);
            ASSERT_TRUE(service->probe_settled);
            if (status != application::service_health::down)
            {
                co_await health.refresh(context);
                ASSERT_EQ(service->probes, 1U);
                ASSERT_EQ(health.snapshots().front().revision, newer.revision);
            }
            ASSERT_TRUE(co_await service->stop(context));
            completed = true;
            io->stop();
        };
        auto operation = run();
        operation.handle().resume();
        io->run();
        ASSERT_TRUE(completed);
    }
}

TEST(application_optional_recovery_continues_after_exhausted_cycles)
{
    for (const bool observed : {false, true})
    {
        auto io = cnetmod::make_io_context();
        cnetmod::observability::telemetry_hub telemetry{*io,
            {.export_traces = observed, .export_metrics = observed, .export_logs = observed}};
        application::service_registry services;
        application::health_registry health{{.failures_before_down = 1, .successes_before_up = 2}};
        application::task_supervisor supervisor{*io};
        auto events = std::make_shared<std::vector<std::string>>();
        auto service = std::make_shared<fake_service>(application::service_key{"optional-cycles"},
            std::vector<application::service_key>{}, application::service_requirement::optional, events);
        service->recovery_options = {.initial_delay = std::chrono::milliseconds{1},
            .maximum_delay = std::chrono::milliseconds{2},
            .budget = std::chrono::milliseconds{30},
            .jitter = 0};
        service->dependency_available.store(false);
        ASSERT_TRUE(services.manage(service));
        ASSERT_TRUE(health.add(service));
        services.freeze();
        health.mark_running();
        health.update(service->key(), {.status = application::service_health::down});
        application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health};
        unsigned shutdown_notifications = 0;
        supervisor.on_recovery_exhausted([&](std::string_view, std::error_code)
            {
                ++shutdown_notifications;
            });
        bool completed = false;
        auto run = [&]() -> cnetmod::task<void>
        {
            for (unsigned cycle = 0; cycle < 2; ++cycle)
            {
                const auto attempts_before = events->size();
                ASSERT_TRUE(lifecycle.reconcile_health(health.snapshots().front()));
                ASSERT_TRUE(co_await supervisor.join());
                ASSERT_TRUE(supervisor.state("service-recovery:" + service->key().canonical_name()) == application::supervised_task_state::failed);
                ASSERT_TRUE(events->size() > attempts_before);
                ASSERT_EQ(shutdown_notifications, 0U);
                ASSERT_TRUE(health.live());
                ASSERT_FALSE(health.ready());
                ASSERT_TRUE(lifecycle.started_services().empty());
            }
            service->dependency_available.store(true);
            ASSERT_TRUE(lifecycle.reconcile_health(health.snapshots().front()));
            ASSERT_TRUE(co_await supervisor.join());
            ASSERT_EQ(lifecycle.started_services().size(), 1U);
            ASSERT_FALSE(health.ready());
            cnetmod::cancel_token token;
            application::service_context context{*io, telemetry, supervisor, token, {}};
            co_await health.refresh(context);
            ASSERT_TRUE(lifecycle.reconcile_health(health.snapshots().front()));
            ASSERT_FALSE(health.ready());
            co_await health.refresh(context);
            ASSERT_TRUE(lifecycle.reconcile_health(health.snapshots().front()));
            ASSERT_TRUE(health.ready());
            ASSERT_EQ(shutdown_notifications, 0U);
            ASSERT_TRUE(co_await lifecycle.stop());
            ASSERT_TRUE(lifecycle.started_services().empty());
            ASSERT_EQ(std::ranges::count(*events, "stop:" + service->key().canonical_name()), 1);
            completed = true;
            io->stop();
        };
        auto operation = run();
        operation.handle().resume();
        io->run();
        ASSERT_TRUE(completed);
    }
}

TEST(application_health_confirmation_preserves_recovery_episode_budget)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    application::service_registry services;
    application::health_registry health{{.failures_before_down = 1, .successes_before_up = 2}};
    application::task_supervisor supervisor{*io};
    auto events = std::make_shared<std::vector<std::string>>();
    auto service = std::make_shared<fake_service>(application::service_key{"confirmation"},
        std::vector<application::service_key>{}, application::service_requirement::required, events);
    service->recovery_options.budget = std::chrono::milliseconds{40};
    ASSERT_TRUE(services.manage(service));
    ASSERT_TRUE(health.add(service));
    services.freeze();
    application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health};
    unsigned exhausted = 0;
    supervisor.on_recovery_exhausted([&](std::string_view, std::error_code error)
        {
            ASSERT_EQ(error, std::make_error_code(std::errc::timed_out));
            ++exhausted;
        });
    bool completed = false;
    auto run = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE(lifecycle.schedule_recovery(service->key()));
        ASSERT_TRUE(co_await supervisor.join());
        ASSERT_EQ(service->probes, 1U);
        (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{80});
        ASSERT_TRUE(lifecycle.reconcile_health(health.snapshots().front()));
        const auto expired = co_await supervisor.join();
        ASSERT_FALSE(expired.has_value());
        ASSERT_EQ(expired.error(), std::make_error_code(std::errc::timed_out));
        ASSERT_EQ(service->probes, 1U);
        ASSERT_EQ(exhausted, 1U);
        ASSERT_TRUE(lifecycle.reconcile_health(health.snapshots().front()));
        (void)co_await supervisor.join();
        ASSERT_EQ(exhausted, 1U);

        cnetmod::cancel_token token;
        application::service_context context{*io, telemetry, supervisor, token, {}};
        co_await health.refresh(context);
        ASSERT_TRUE(lifecycle.reconcile_health(health.snapshots().front()));
        ASSERT_EQ(exhausted, 1U);
        co_await health.refresh(context);
        ASSERT_TRUE(health.snapshots().front().report.status == application::service_health::up);
        const auto confirmed = health.snapshots().front();
        ASSERT_TRUE(health.is_current(confirmed));
        ASSERT_TRUE(lifecycle.reconcile_health(confirmed));
        health.update(service->key(), {.status = application::service_health::down});
        ASSERT_FALSE(health.is_current(confirmed));
        ASSERT_TRUE(lifecycle.reconcile_health(health.snapshots().front()));
        ASSERT_TRUE(co_await supervisor.join());
        ASSERT_EQ(service->probes, 4U);
        ASSERT_EQ(exhausted, 1U);
        (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{80});
        ASSERT_TRUE(lifecycle.reconcile_health(confirmed));
        ASSERT_TRUE(lifecycle.reconcile_health(health.snapshots().front()));
        const auto second_expiry = co_await supervisor.join();
        ASSERT_FALSE(second_expiry.has_value());
        ASSERT_EQ(second_expiry.error(), std::make_error_code(std::errc::timed_out));
        ASSERT_EQ(service->probes, 4U);
        ASSERT_EQ(exhausted, 2U);
        (void)co_await lifecycle.stop();
        ASSERT_TRUE(lifecycle.started_services().empty());
        completed = true;
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(completed);
}

TEST(application_reconnect_success_does_not_reset_failed_probe_recovery_budget)
{
    for (unsigned mode = 0; mode < 4; ++mode)
    {
        const bool timeout = mode == 1 || mode == 3;
        auto io = cnetmod::make_io_context();
        cnetmod::observability::telemetry_hub telemetry{*io,
            {.export_traces = false, .export_metrics = false, .export_logs = false}};
        application::service_registry services;
        application::health_registry health;
        application::task_supervisor supervisor{*io};
        auto events = std::make_shared<std::vector<std::string>>();
        auto service = std::make_shared<fake_service>(application::service_key{"unhealthy"},
            std::vector<application::service_key>{}, application::service_requirement::required, events);
        service->recovery_options = {.initial_delay = std::chrono::milliseconds{1},
            .maximum_delay = std::chrono::milliseconds{2},
            .budget = std::chrono::milliseconds{150},
            .jitter = 0};
        service->reject_probe = true;
        if (mode == 2)
            service->probe_error = std::make_error_code(std::errc::permission_denied);
        if (timeout)
            service->probe_delay = std::chrono::seconds{10};
        ASSERT_TRUE(services.manage(service));
        ASSERT_TRUE(health.add(service));
        services.freeze();
        application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health,
            {.service_start_timeout = std::chrono::milliseconds{mode == 1 ? 5 : 30000}}};
        unsigned exhausted = 0;
        supervisor.on_recovery_exhausted([&](std::string_view, std::error_code)
            {
                ++exhausted;
            });
        bool completed = false;
        auto run = [&]() -> cnetmod::task<void>
        {
            const auto begin = std::chrono::steady_clock::now();
            ASSERT_TRUE(lifecycle.schedule_recovery(service->key()));
            const auto result = co_await supervisor.join();
            ASSERT_TRUE(std::chrono::steady_clock::now() - begin < std::chrono::seconds{2});
            ASSERT_FALSE(result.has_value());
            ASSERT_EQ(result.error(), std::make_error_code(timeout ? std::errc::timed_out : mode == 2 ? std::errc::permission_denied
                                                                                                      : std::errc::resource_unavailable_try_again));
            const auto failure = lifecycle.last_failure();
            ASSERT_TRUE(failure.has_value());
            if (failure)
            {
                ASSERT_TRUE(failure->service == service->key());
                ASSERT_TRUE(failure->phase == application::lifecycle_phase::health_recovery);
                ASSERT_EQ(failure->error, result.error());
            }
            ASSERT_EQ(exhausted, 1U);
            ASSERT_TRUE(service->probes > 0U);
            if (timeout)
            {
                if (mode == 1)
                    ASSERT_TRUE(service->probes >= 2U);
                else
                    ASSERT_EQ(service->probes, 1U);
                ASSERT_TRUE(service->probe_cancelled);
                ASSERT_TRUE(service->probe_settled);
            }
            ASSERT_EQ(lifecycle.started_services().size(), 1U);
            ASSERT_FALSE(health.ready());
            (void)co_await lifecycle.stop();
            ASSERT_TRUE(lifecycle.started_services().empty());
            completed = true;
            io->stop();
        };
        auto operation = run();
        operation.handle().resume();
        io->run();
        ASSERT_TRUE(completed);
    }
}

TEST(application_stop_cancels_pending_recovery_probe_before_join_returns)
{
    for (unsigned iteration = 0; iteration < 32; ++iteration)
    {
        auto io = cnetmod::make_io_context();
        cnetmod::observability::telemetry_hub telemetry{*io,
            {.export_traces = false, .export_metrics = false, .export_logs = false}};
        application::service_registry services;
        application::health_registry health;
        application::task_supervisor supervisor{*io};
        auto events = std::make_shared<std::vector<std::string>>();
        auto service = std::make_shared<fake_service>(application::service_key{"pending-probe"},
            std::vector<application::service_key>{}, application::service_requirement::required, events);
        service->probe_delay = std::chrono::seconds{10};
        ASSERT_TRUE(services.manage(service));
        ASSERT_TRUE(health.add(service));
        services.freeze();
        application::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health};
        bool completed = false;
        std::jthread stopper;
        if (iteration != 0)
            stopper = std::jthread([&]
                {
                    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
                    while (!service->probe_entered.load(std::memory_order_acquire) &&
                        std::chrono::steady_clock::now() < limit)
                        std::this_thread::yield();
                    if (iteration % 2 == 0)
                        std::this_thread::sleep_for(std::chrono::milliseconds{1});
                    supervisor.request_stop();
                });
        auto run = [&]() -> cnetmod::task<void>
        {
            const auto begin = std::chrono::steady_clock::now();
            ASSERT_TRUE(lifecycle.schedule_recovery(service->key()));
            while (service->probes == 0 && std::chrono::steady_clock::now() - begin < std::chrono::seconds{2})
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
            ASSERT_EQ(service->probes, 1U);
            if (iteration == 0)
                supervisor.request_stop();
            ASSERT_TRUE(co_await supervisor.join());
            ASSERT_TRUE(service->probe_cancelled);
            ASSERT_TRUE(service->probe_settled);
            ASSERT_TRUE(std::chrono::steady_clock::now() - begin < std::chrono::seconds{2});
            ASSERT_EQ(lifecycle.started_services().size(), 1U);
            ASSERT_TRUE(co_await lifecycle.stop());
            ASSERT_TRUE(lifecycle.started_services().empty());
            completed = true;
            io->stop();
        };
        auto operation = run();
        operation.handle().resume();
        io->run();
        if (stopper.joinable())
            stopper.join();
        ASSERT_TRUE(completed);
    }
}

TEST(application_optional_service_recovers_under_supervision)
{
    for (const bool enabled : {false, true})
    {
        auto io = cnetmod::make_io_context();
        cnetmod::observability::telemetry_hub telemetry{*io,
            {.endpoint = "http://127.0.0.1:1/v1/traces",
                .export_traces = enabled,
                .export_metrics = enabled,
                .export_logs = enabled}};
        ASSERT_EQ(static_cast<bool>(telemetry.spans()), enabled);
        application::service_registry services;
        application::health_registry health;
        application::task_supervisor supervisor{*io};
        const auto events = std::make_shared<std::vector<std::string>>();
        auto service = std::make_shared<fake_service>(
            application::service_key{"optional"},
            std::vector<application::service_key>{},
            application::service_requirement::optional, events, 1U);
        ASSERT_TRUE(services.manage(service));
        ASSERT_TRUE(health.add(service));
        services.freeze();
        application::service_lifecycle lifecycle{*io, telemetry, services,
            supervisor, health};
        std::optional<std::expected<void, std::error_code>> started;
        auto run = [&]() -> cnetmod::task<void>
        {
            started = co_await lifecycle.start();
            (void)co_await supervisor.join();
            (void)co_await lifecycle.stop();
            io->stop();
        };
        auto operation = run();
        operation.handle().resume();
        io->run();
        ASSERT_TRUE(started.has_value());
        ASSERT_TRUE(started->has_value());
        ASSERT_TRUE(*events == std::vector<std::string>({"start:optional:default", "start:optional:default", "stop:optional:default"}));
        if (!enabled)
        {
            ASSERT_FALSE(telemetry.metrics().render_openmetrics().contains("application_service_"));
            ASSERT_EQ(telemetry.statistics().accepted, std::uint64_t{0});
        }
    }
}

TEST(application_builder_validates_before_creating_host)
{
    auto host = application::application_builder{"invalid"}
                    .configure([](application::application_configuration& value)
                        {
                            value.http.port = 0;
                        })
                    .build();
    ASSERT_FALSE(host.has_value());
}

TEST(application_runtime_supervises_tasks_and_offloads_json)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::thread_pool cpu_pool{2};
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    application::task_supervisor supervisor{*io};
    application::service_registry services;
    std::stop_source stopping;
    application::application_runtime runtime{*io, cpu_pool, supervisor,
        telemetry, services, stopping.get_token()};
    bool background_ran = false;
    auto accepted = runtime.spawn_managed("runtime-test",
        [&](cnetmod::cancel_token& token)
            -> cnetmod::task<std::expected<void, std::error_code>>
        {
            background_ran = !token.is_cancelled();
            co_return std::expected<void, std::error_code>{};
        });
    ASSERT_TRUE(accepted.has_value());

    std::optional<std::expected<nlohmann::json, std::error_code>> parsed;
    std::optional<std::expected<std::string, std::error_code>> dumped;
    std::optional<std::expected<void, std::error_code>> file_written;
    std::optional<std::expected<void, std::error_code>> file_flushed;
    std::optional<std::expected<void, std::error_code>> file_closed;
    std::optional<std::expected<std::string, std::error_code>> file_read;
    std::optional<std::expected<void, std::error_code>> file_removed;
    std::optional<std::expected<void, std::error_code>> directory_rejected;
    std::thread::id event_loop_thread;
    std::thread::id cpu_thread;
    std::thread::id restored_thread;
    const auto file_path = std::filesystem::temp_directory_path() /
        ("cnetmod-runtime-" +
            std::to_string(std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()) +
            ".json");
    const auto directory_path = file_path.string() + ".directory";
    ASSERT_TRUE(std::filesystem::create_directory(directory_path));
    auto run = [&]() -> cnetmod::task<void>
    {
        event_loop_thread = std::this_thread::get_id();
        co_await runtime.schedule_on_cpu();
        cpu_thread = std::this_thread::get_id();
        co_await runtime.resume_to_event_loop();
        restored_thread = std::this_thread::get_id();
        cnetmod::cancel_token file_cancellation;
        parsed = co_await application::parse_offloaded(runtime,
            R"({"value":42})");
        if (parsed)
            dumped = co_await application::dump_offloaded(runtime, **parsed);
        file_written = co_await runtime.files().write_all(
            file_path, "payload", file_cancellation);
        auto opened = co_await runtime.files().open(
            file_path, cnetmod::open_mode::read_write);
        if (opened)
        {
            file_flushed = co_await runtime.files().flush(*opened);
            file_closed = co_await runtime.files().close(*opened);
        }
        file_read = co_await runtime.files().read_all(
            file_path, file_cancellation);
        file_removed = co_await runtime.files().remove(
            file_path, file_cancellation);
        auto removed_again = co_await runtime.files().remove(file_path);
        ASSERT_TRUE(removed_again.has_value());
        directory_rejected = co_await runtime.files().remove(directory_path);
        supervisor.request_stop();
        (void)co_await supervisor.join();
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    operation.handle().promise().result();
    cpu_pool.request_stop();

    ASSERT_TRUE(background_ran);
    ASSERT_TRUE(cpu_thread != event_loop_thread);
    ASSERT_EQ(restored_thread, event_loop_thread);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->has_value());
    ASSERT_EQ(parsed->value().at("value").get<int>(), 42);
    ASSERT_TRUE(dumped.has_value());
    ASSERT_TRUE(dumped->has_value());
    ASSERT_TRUE(dumped->value().contains("42"));
    ASSERT_TRUE(file_written.has_value());
    ASSERT_TRUE(file_written->has_value());
    ASSERT_TRUE(file_flushed.has_value());
    ASSERT_TRUE(file_flushed->has_value());
    ASSERT_TRUE(file_closed.has_value());
    ASSERT_TRUE(file_closed->has_value());
    ASSERT_TRUE(file_read.has_value());
    ASSERT_TRUE(file_read->has_value());
    ASSERT_EQ(file_read->value(), std::string{"payload"});
    ASSERT_TRUE(file_removed.has_value());
    ASSERT_TRUE(file_removed->has_value());
    ASSERT_TRUE(directory_rejected.has_value());
    ASSERT_FALSE(directory_rejected->has_value());
    ASSERT_TRUE(std::filesystem::is_directory(directory_path));
    ASSERT_TRUE(std::filesystem::remove(directory_path));

    stopping.request_stop();
    ASSERT_TRUE(runtime.stop_requested());
    ASSERT_TRUE(runtime.cancellation().stop_requested());
    auto rejected = runtime.spawn_managed("too-late",
        [](cnetmod::cancel_token&)
            -> cnetmod::task<std::expected<void, std::error_code>>
        {
            co_return std::expected<void, std::error_code>{};
        });
    ASSERT_FALSE(rejected.has_value());
    ASSERT_EQ(rejected.error(),
        std::make_error_code(std::errc::operation_canceled));
}

TEST(application_builder_runtime_routes_receive_host_runtime)
{
    application::application_runtime* observed = nullptr;
    auto host = application::application_builder{"runtime-routes"}
                    .routes(application::runtime_route_configurer{
                        [&observed](cnetmod::http::router& routes,
                            application::application_runtime& runtime)
                        {
                            observed = &runtime;
                            routes.get("/runtime",
                                [](cnetmod::http::request_context& context)
                                    -> cnetmod::task<void>
                                {
                                    context.text(cnetmod::http::status::ok, "ok");
                                    co_return;
                                });
                        }})
                    .build();
    ASSERT_TRUE(host.has_value());
    ASSERT_EQ(observed, &host->runtime());
}

TEST(application_builder_runtime_middleware_uses_host_runtime)
{
    application::application_runtime* observed = nullptr;
    auto host = application::application_builder{"runtime-middleware"}
                    .runtime_middleware(application::runtime_middleware_factory{
                        [&observed](application::application_runtime& runtime)
                        {
                            observed = &runtime;
                            return runtime.compression({
                                .min_size = 128,
                                .max_concurrency = 2,
                            });
                        }})
                    .build();
    ASSERT_TRUE(host.has_value());
    ASSERT_EQ(observed, &host->runtime());

    auto rejected = application::application_builder{"invalid-runtime-middleware"}
                        .runtime_middleware(application::runtime_middleware_factory{
                            [](application::application_runtime&)
                            {
                                return application::application_middleware{};
                            }})
                        .build();
    ASSERT_FALSE(rejected.has_value());

    bool empty_rejected = false;
    try
    {
        application::application_builder{"empty-runtime-middleware"}
            .runtime_middleware(application::runtime_middleware_factory{});
    }
    catch (const std::invalid_argument&)
    {
        empty_rejected = true;
    }
    ASSERT_TRUE(empty_rejected);
}

TEST(application_builder_composes_host_owned_service_factories_before_freeze)
{
    auto events = std::make_shared<std::vector<std::string>>();
    cnetmod::io_context* observed_io = nullptr;
    application::task_supervisor* observed_supervisor = nullptr;
    auto service = std::make_shared<fake_service>(
        application::service_key{"factory", "primary"},
        std::vector<application::service_key>{},
        application::service_requirement::required, events);
    auto host = application::application_builder{"factory-composition"}
                    .configure([](application::application_configuration& value)
                        {
                            value.logging.manage_lifecycle = false;
                            value.management.enabled = false;
                        })
                    .service_factory([&](application::application_service_context& context) -> std::expected<std::shared_ptr<application::managed_service>, std::error_code>
                        {
                            observed_io = &context.io;
                            observed_supervisor = &context.supervisor;
                            if (context.configuration.name != "factory-composition")
                                return std::unexpected(std::make_error_code(
                                    std::errc::invalid_argument));
                            return service;
                        })
                    .build();
    ASSERT_TRUE(host.has_value());
    ASSERT_TRUE(observed_io != nullptr);
    ASSERT_TRUE(observed_supervisor != nullptr);
    if (host)
    {
        ASSERT_TRUE(host->services().frozen());
        ASSERT_TRUE(host->services().managed(
                        {"factory", "primary"}) == service);
    }

    auto rejected = application::application_builder{"invalid-factory"}
                        .service_factory(
                            [](application::application_service_context&)
                                -> std::expected<std::shared_ptr<application::managed_service>,
                                    std::error_code>
                            {
                                return std::shared_ptr<application::managed_service>{};
                            })
                        .build();
    ASSERT_FALSE(rejected.has_value());
}

TEST(application_builder_executes_ordered_business_middleware)
{
    cnetmod::net_init network;
    auto reservation = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(reservation.has_value());
    ASSERT_TRUE(reservation->bind(
                               {cnetmod::ipv4_address::loopback(), 0})
            .has_value());
    const auto endpoint = reservation->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    std::vector<std::string> events;
    auto first = [&events](cnetmod::http::request_context&,
                     cnetmod::http::next_fn next) -> cnetmod::task<void>
    {
        events.push_back("first-enter");
        co_await next();
        events.push_back("first-exit");
    };
    auto second = [&events](cnetmod::http::request_context&,
                      cnetmod::http::next_fn next) -> cnetmod::task<void>
    {
        events.push_back("second-enter");
        co_await next();
        events.push_back("second-exit");
    };

    auto host = application::application_builder{"middleware-composition"}
                    .configure([&endpoint](application::application_configuration& value)
                        {
                            value.http.address = "127.0.0.1";
                            value.http.port = endpoint->port();
                            value.http.access_logging = false;
                            value.logging.manage_lifecycle = false;
                            value.install_signal_handlers = false;
                            value.management.enabled = false;
                            value.observability.tracing = false;
                            value.observability.metrics = false;
                            value.observability.logs = false;
                        })
                    .routes([&events](cnetmod::http::router& routes)
                        {
                            routes.get("/middleware-order",
                                [&events](cnetmod::http::request_context& request)
                                    -> cnetmod::task<void>
                                {
                                    events.push_back("route");
                                    const auto headers_ok =
                                        request.get_header("X-Template") == "request" &&
                                        request.get_header("X-Default") == "present" &&
                                        request.get_header("X-Request") == "present";
                                    request.text(headers_ok
                                            ? cnetmod::http::status::ok
                                            : cnetmod::http::status::bad_request,
                                        headers_ok ? "ok" : "missing headers");
                                    co_return;
                                });
                        })
                    .middleware(first)
                    .middleware(second)
                    .build();

    ASSERT_TRUE(host.has_value());
    if (!host)
        return;
    reservation->close();
    std::optional<std::expected<void, std::error_code>> completed;
    std::jthread runner([&]
        {
            completed = host->run();
        });
    const auto startup_timeout = std::chrono::steady_clock::now() +
        std::chrono::seconds{2};
    while (host->state() == application::application_state::built ||
        host->state() == application::application_state::starting)
    {
        if (std::chrono::steady_clock::now() >= startup_timeout)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    bool response_ok = false;
    bool failed_client_discarded = false;
    if (host->state() == application::application_state::running)
    {
        auto io = cnetmod::make_io_context();
        auto request = [&]() -> cnetmod::task<void>
        {
            cnetmod::observability::otlp_http_options telemetry_options;
            telemetry_options.export_traces = false;
            telemetry_options.export_metrics = false;
            telemetry_options.export_logs = false;
            cnetmod::observability::telemetry_hub telemetry{
                *io, std::move(telemetry_options)};
            application::rest_template rest{*io, telemetry,
                application::rest_template_options{
                    .client = {.request_timeout =
                                   std::chrono::milliseconds{500},
                        .keep_alive = false},
                    .default_headers = {{"X-Template", "default"},
                        {"X-Default", "present"}}}};
            auto response = co_await rest.get(std::format(
                                                  "http://127.0.0.1:{}/middleware-order",
                                                  endpoint->port()),
                {.headers = {{"X-Template", "request"},
                     {"X-Request", "present"}}});
            response_ok = response && response->status_code() == 200 &&
                response->body() == "ok";
            const auto failed = co_await rest.get("not-a-valid-http-url");
            failed_client_discarded = !failed && rest.idle_count() == 0;
            io->stop();
        };
        cnetmod::spawn(*io, request());
        io->run();
    }
    host->request_stop();
    runner.join();
    ASSERT_TRUE(completed.has_value() && completed->has_value());
    ASSERT_TRUE(response_ok);
    ASSERT_TRUE(failed_client_discarded);
    ASSERT_TRUE(events == std::vector<std::string>({"first-enter", "second-enter", "route", "second-exit", "first-exit"}));

    bool rejected = false;
    try
    {
        application::application_builder{"empty-middleware"}
            .middleware(application::application_middleware{});
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    ASSERT_TRUE(rejected);
}

TEST(application_configuration_precedence_and_redaction)
{
    const auto path = std::filesystem::temp_directory_path() /
        "cnetmod-application-test.json";
    {
        std::ofstream output{path};
        output << R"({"application":{"name":"json-name","cpu_threads":3},"crash_dump":{"directory":"application-crashes"},"http":{"port":18080,"sse":{"max_duration_ms":45000,"write_timeout_ms":2500}},"observability":{"otlp":{"capture_framework_logs":true}},"services":{"primary":{"type":"redis","instance":"cache","enabled":false,"password":"secret"}}})";
    }
#ifdef _WIN32
    _putenv_s("CNETMOD_HTTP_PORT", "18081");
    _putenv_s("CNETMOD_OTLP_CAPTURE_FRAMEWORK_LOGS", "false");
    _putenv_s("CNETMOD_CPU_THREADS", "4");
#else
    setenv("CNETMOD_HTTP_PORT", "18081", 1);
    setenv("CNETMOD_OTLP_CAPTURE_FRAMEWORK_LOGS", "false", 1);
    setenv("CNETMOD_CPU_THREADS", "4", 1);
#endif
    auto host = application::application_builder{"builder-name"}
                    .configuration_file(path)
                    .configure([](application::application_configuration& value)
                        {
                            value.http.port = 18082;
                            value.logging.manage_lifecycle = false;
                            value.management.enabled = false;
                            value.execution.cpu_threads = 5;
                            value.observability.otlp.capture_framework_logs = true;
                        })
                    .build();
#ifdef _WIN32
    _putenv_s("CNETMOD_HTTP_PORT", "");
    _putenv_s("CNETMOD_OTLP_CAPTURE_FRAMEWORK_LOGS", "");
    _putenv_s("CNETMOD_CPU_THREADS", "");
#else
    unsetenv("CNETMOD_HTTP_PORT");
    unsetenv("CNETMOD_OTLP_CAPTURE_FRAMEWORK_LOGS");
    unsetenv("CNETMOD_CPU_THREADS");
#endif
    std::filesystem::remove(path);
    ASSERT_TRUE(host.has_value());
    ASSERT_EQ(host->configuration().name, "builder-name");
    ASSERT_EQ(host->configuration().http.port, std::uint16_t{18082});
    ASSERT_EQ(host->configuration().http.sse_max_duration,
        std::chrono::milliseconds{45000});
    ASSERT_EQ(host->configuration().http.sse_write_timeout,
        std::chrono::milliseconds{2500});
    ASSERT_EQ(host->configuration().execution.cpu_threads, 5U);
    ASSERT_EQ(host->configuration().crash_dump.directory,
        std::filesystem::path{"application-crashes"});
    ASSERT_TRUE(host->configuration().observability.otlp.capture_framework_logs);
    ASSERT_EQ(host->configuration().services.at("primary").name, "redis");

    auto secrets = nlohmann::json::object();
    secrets["password"] = "secret";
    secrets["endpoint"] = "postgres://user:password@localhost/database";
    const auto redacted = application::redact_configuration(secrets);
    ASSERT_EQ(redacted.at("password").get<std::string>(), "[REDACTED]");
    ASSERT_FALSE(redacted.at("endpoint").get<std::string>().contains("password"));
}

TEST(application_configuration_parses_opt_in_orm_sharding)
{
    const auto path = std::filesystem::temp_directory_path() /
        "cnetmod-application-sharding-test.json";
    {
        std::ofstream output{path};
        output << R"({"orm":{"sharding":{"enabled":true,"topologies":{"orders":{"logical_table":"order_records","table_count":32,"databases":["orders-0","orders-1"],"scatter_gather":true,"distributed_transactions":true}}}},"management":{"enabled":false},"logging":{"manage_lifecycle":false}})";
    }
    auto host = application::application_builder{"sharding-configuration-test"}
                    .configuration_file(path)
                    .build();
    std::filesystem::remove(path);
    ASSERT_TRUE(host.has_value());
    if (!host)
        return;
    const auto& sharding = host->configuration().orm.sharding;
    ASSERT_TRUE(sharding.enabled);
    ASSERT_EQ(sharding.topologies.size(), 1U);
    const auto& orders = sharding.topologies.at("orders");
    ASSERT_EQ(orders.logical_table, "order_records");
    ASSERT_EQ(orders.table_count, 32U);
    ASSERT_EQ(orders.databases.size(), 2U);
    ASSERT_TRUE(orders.scatter_gather);
    ASSERT_TRUE(orders.distributed_transactions);
}

TEST(application_configuration_rejects_invalid_orm_sharding)
{
    auto empty = application::application_builder{"invalid-sharding"}
                     .configure([](application::application_configuration& value)
                         {
                             value.orm.sharding.enabled = true;
                         })
                     .build();
    ASSERT_FALSE(empty.has_value());

    auto duplicate = application::application_builder{"invalid-sharding"}
                         .configure([](application::application_configuration& value)
                             {
                                 value.orm.sharding.enabled = true;
                                 value.orm.sharding.topologies.emplace("orders",
                                     application::orm_shard_topology_configuration{
                                         .logical_table = "orders",
                                         .table_count = 16,
                                         .databases = {"orders-0", "orders-0"},
                                     });
                             })
                         .build();
    ASSERT_FALSE(duplicate.has_value());
}

TEST(application_yaml_configuration_uses_the_json_validation_pipeline)
{
    const auto path = std::filesystem::temp_directory_path() /
        "cnetmod-application-test.yaml";
    {
        std::ofstream output{path};
        output << R"(application:
  name: yaml-name
http:
  address: 127.0.0.1
  port: 18083
management:
  enabled: false
logging:
  manage_lifecycle: false
services:
  primary:
    type: redis
    instance: cache
    enabled: false
    password: ${CNETMOD_YAML_TEST_PASSWORD}
)";
    }
#ifdef _WIN32
    _putenv_s("CNETMOD_YAML_TEST_PASSWORD", "yaml-secret");
#else
    setenv("CNETMOD_YAML_TEST_PASSWORD", "yaml-secret", 1);
#endif
    auto host = application::application_builder{"builder-name"}
                    .configuration_file(path)
                    .configure([](application::application_configuration& value)
                        {
                            value.observability.tracing = false;
                            value.observability.metrics = false;
                            value.observability.logs = false;
                        })
                    .build();
#ifdef _WIN32
    _putenv_s("CNETMOD_YAML_TEST_PASSWORD", "");
#else
    unsetenv("CNETMOD_YAML_TEST_PASSWORD");
#endif
    std::filesystem::remove(path);
    ASSERT_TRUE(host.has_value());
    ASSERT_EQ(host->configuration().name, "builder-name");
    ASSERT_EQ(host->configuration().http.port, std::uint16_t{18083});
    ASSERT_EQ(host->configuration().services.at("primary").instance, "cache");
    ASSERT_EQ(host->configuration().services.at("primary").properties.at("password"),
        "yaml-secret");
}

TEST(application_task_supervisor_rejects_invalid_recovery_before_registration)
{
    auto io = cnetmod::make_io_context();
    application::task_supervisor supervisor{*io};
    for (unsigned variant = 0; variant < 9; ++variant)
    {
        application::recovery_policy policy;
        switch (variant)
        {
        case 0:
            policy.multiplier = std::numeric_limits<double>::quiet_NaN();
            break;
        case 1:
            policy.multiplier = std::numeric_limits<double>::infinity();
            break;
        case 2:
            policy.multiplier = 0.5;
            break;
        case 3:
            policy.jitter = std::numeric_limits<double>::quiet_NaN();
            break;
        case 4:
            policy.jitter = 1.1;
            break;
        case 5:
            policy.initial_delay = std::chrono::milliseconds{0};
            break;
        case 6:
            policy.maximum_delay = std::chrono::milliseconds{1};
            break;
        case 7:
            policy.budget = std::chrono::milliseconds{-1};
            break;
        case 8:
            policy.budget = std::chrono::milliseconds::max();
            break;
        }
        auto added = supervisor.supervise("invalid-policy", [](cnetmod::cancel_token&) -> cnetmod::task<std::expected<void, std::error_code>>
            {
                co_return {};
            },
            policy);
        ASSERT_FALSE(added.has_value());
        ASSERT_EQ(added.error(), std::make_error_code(std::errc::invalid_argument));
        ASSERT_FALSE(supervisor.state("invalid-policy").has_value());
        auto joining = supervisor.join();
        joining.handle().resume();
        ASSERT_TRUE(joining.handle().done());
    }
}

TEST(application_task_supervisor_restarts_until_success)
{
    auto io = cnetmod::make_io_context();
    application::task_supervisor supervisor{*io};
    std::atomic<int> attempts{};
    auto added = supervisor.supervise("recovering-task",
        [&attempts](cnetmod::cancel_token&)
            -> cnetmod::task<std::expected<void, std::error_code>>
        {
            if (attempts.fetch_add(1, std::memory_order_relaxed) < 2)
                co_return std::unexpected(
                    std::make_error_code(std::errc::connection_refused));
            co_return {};
        },
        {.initial_delay = std::chrono::milliseconds{1},
            .maximum_delay = std::chrono::milliseconds{2},
            .budget = std::chrono::seconds{1},
            .jitter = 0.0});
    ASSERT_TRUE(added.has_value());
    auto wait = [&]() -> cnetmod::task<void>
    {
        (void)co_await supervisor.join();
        io->stop();
    };
    auto operation = wait();
    operation.handle().resume();
    io->run();
    ASSERT_EQ(attempts.load(std::memory_order_relaxed), 3);
    ASSERT_TRUE(supervisor.state("recovering-task") ==
        application::supervised_task_state::stopped);
}

TEST(application_task_supervisor_recovery_budget_starts_at_failure_and_bounds_backoff)
{
    for (bool long_backoff : {false, true})
    {
        auto io = cnetmod::make_io_context();
        application::task_supervisor supervisor{*io};
        unsigned attempts = 0;
        auto added = supervisor.supervise("budget-test",
            [&](cnetmod::cancel_token& token) -> cnetmod::task<std::expected<void, std::error_code>>
            {
                if (++attempts == 1)
                {
                    (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{100}, token);
                    co_return std::unexpected(std::make_error_code(std::errc::connection_refused));
                }
                co_return {};
            },
            {.initial_delay = long_backoff ? std::chrono::milliseconds{1000} : std::chrono::milliseconds{1},
                .maximum_delay = std::chrono::milliseconds{1000},
                .budget = std::chrono::milliseconds{50},
                .jitter = 0.0});
        ASSERT_TRUE(added.has_value());
        std::expected<void, std::error_code> result;
        auto wait = [&]() -> cnetmod::task<void>
        {
            result = co_await supervisor.join();
            io->stop();
        };
        const auto began = std::chrono::steady_clock::now();
        auto joining = wait();
        joining.handle().resume();
        io->run();
        ASSERT_EQ(result.has_value(), !long_backoff);
        ASSERT_EQ(attempts, long_backoff ? 1U : 2U);
        if (long_backoff)
        {
            ASSERT_EQ(result.error(), std::make_error_code(std::errc::connection_refused));
            ASSERT_TRUE(std::chrono::steady_clock::now() - began < std::chrono::milliseconds{750});
        }
    }
}

TEST(application_task_supervisor_stop_racing_registration_cancels_every_accepted_task)
{
    for (unsigned iteration = 0; iteration < 64; ++iteration)
    {
        auto io = cnetmod::make_io_context();
        application::task_supervisor supervisor{*io};
        std::atomic<bool> ready{false};
        bool accepted = false, cancelled = false;
        unsigned stop_callbacks = 0;
        std::thread registration([&]
            {
                while (!ready.load(std::memory_order_acquire))
                    std::this_thread::yield();
                accepted = supervisor.supervise("racing-task", [&](cnetmod::cancel_token& token) -> cnetmod::task<std::expected<void, std::error_code>>
                                         {
                                             cancelled = token.is_cancelled();
                                             co_return {};
                                         },
                                         {}, true, [&]() noexcept
                                         {
                                             ++stop_callbacks;
                                         })
                               .has_value();
            });
        ready.store(true, std::memory_order_release);
        supervisor.request_stop();
        registration.join();
        auto wait = [&]() -> cnetmod::task<void>
        {
            (void)co_await supervisor.join();
            io->stop();
        };
        auto joining = wait();
        joining.handle().resume();
        io->run();
        ASSERT_EQ(stop_callbacks, accepted ? 1U : 0U);
        if (accepted)
        {
            ASSERT_TRUE(cancelled);
            ASSERT_TRUE(joining.handle().done());
        }
    }
}

TEST(application_task_supervisor_reports_exhausted_required_task)
{
    auto io = cnetmod::make_io_context();
    application::task_supervisor supervisor{*io};
    std::atomic<bool> exhausted{};
    supervisor.on_recovery_exhausted(
        [&exhausted](std::string_view, std::error_code)
        {
            exhausted.store(true, std::memory_order_release);
        });
    ASSERT_TRUE(supervisor.supervise("required-task", [](cnetmod::cancel_token&) -> cnetmod::task<std::expected<void, std::error_code>>
        {
            co_return std::unexpected(
                std::make_error_code(std::errc::connection_aborted));
        },
        {.initial_delay = std::chrono::milliseconds{1}, .maximum_delay = std::chrono::milliseconds{1}, .budget = std::chrono::milliseconds{3}, .jitter = 0.0}, true));
    auto wait = [&]() -> cnetmod::task<void>
    {
        (void)co_await supervisor.join();
        io->stop();
    };
    auto operation = wait();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(exhausted.load(std::memory_order_acquire));
    ASSERT_TRUE(supervisor.state("required-task") ==
        application::supervised_task_state::failed);
    ASSERT_EQ(supervisor.last_error("required-task"),
        std::make_error_code(std::errc::connection_aborted));
}

#ifndef CNETMOD_PLATFORM_MACOS
// Xcode's module coroutine ABI terminates when an exception leaves a child
// task. Managed tasks use expected/error_code for portable failures.
TEST(application_task_supervisor_preserves_system_error_codes)
{
    auto io = cnetmod::make_io_context();
    application::task_supervisor supervisor{*io};
    const auto original = std::make_error_code(std::errc::permission_denied);
    std::error_code reported;
    supervisor.on_recovery_exhausted([&](std::string_view, std::error_code error)
        {
            reported = error;
        });
    auto registered = supervisor.supervise("throwing-task", [&](cnetmod::cancel_token&) -> cnetmod::task<std::expected<void, std::error_code>>
        {
            throw std::system_error(original);
            co_return {};
        },
        {.budget = std::chrono::milliseconds{0}}, true);
    ASSERT_TRUE(registered.has_value());
    auto wait = [&]() -> cnetmod::task<void>
    {
        (void)co_await supervisor.join();
        io->stop();
    };
    auto joining = wait();
    joining.handle().resume();
    io->run();
    ASSERT_EQ(reported, original);
    ASSERT_EQ(supervisor.last_error("throwing-task"), original);
    ASSERT_TRUE(supervisor.state("throwing-task") == application::supervised_task_state::failed);
}
#endif

TEST(application_task_supervisor_joins_after_recovery_callback_throws)
{
    auto io = cnetmod::make_io_context();
    application::task_supervisor supervisor{*io};
    unsigned notifications = 0;
    supervisor.on_recovery_exhausted([&](std::string_view, std::error_code)
        {
            ++notifications;
            throw std::runtime_error("recovery observer failed");
        });
    ASSERT_TRUE(supervisor.supervise("failed-observer", [](cnetmod::cancel_token&) -> cnetmod::task<std::expected<void, std::error_code>>
        {
            co_return std::unexpected(std::make_error_code(std::errc::connection_refused));
        },
        {.budget = std::chrono::milliseconds{0}}, true));
    auto wait = [&]() -> cnetmod::task<void>
    {
        (void)co_await supervisor.join();
        io->stop();
    };
    auto joining = wait();
    joining.handle().resume();
    io->run();
    ASSERT_TRUE(joining.handle().done());
    ASSERT_EQ(notifications, 1U);
    ASSERT_EQ(supervisor.last_error("failed-observer"), std::make_error_code(std::errc::connection_refused));
}

TEST(application_task_supervisor_notifies_without_copying_registered_handler)
{
    struct handler
    {
        bool& reject_copy;
        unsigned& notifications;

        handler(bool& reject, unsigned& count) : reject_copy(reject), notifications(count) {}

        handler(const handler& other) : reject_copy(other.reject_copy), notifications(other.notifications)
        {
            if (reject_copy)
                throw std::bad_alloc{};
        }

        void operator()(std::string_view, std::error_code) const
        {
            ++notifications;
        }
    };

    auto io = cnetmod::make_io_context();
    application::task_supervisor supervisor{*io};
    bool reject_copy = false;
    unsigned notifications = 0;
    supervisor.on_recovery_exhausted(handler{reject_copy, notifications});
    reject_copy = true;
    ASSERT_TRUE(supervisor.supervise("failure", [](cnetmod::cancel_token&) -> cnetmod::task<std::expected<void, std::error_code>>
        {
            co_return std::unexpected(std::make_error_code(std::errc::permission_denied));
        },
        {.budget = std::chrono::milliseconds{0}}, true));
    bool completed = false;
    auto run = [&]() -> cnetmod::task<void>
    {
        const auto result = co_await supervisor.join();
        ASSERT_FALSE(result.has_value());
        ASSERT_EQ(result.error(), std::make_error_code(std::errc::permission_denied));
        ASSERT_EQ(notifications, 1U);
        completed = true;
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(completed);
}

TEST(application_throwing_stop_callbacks_do_not_prevent_other_cancellations)
{
    auto io = cnetmod::make_io_context();
    application::task_supervisor supervisor{*io};
    unsigned callbacks = 0;
    unsigned settled = 0;
    for (const auto name : {"a-task", "b-task"})
        ASSERT_TRUE(supervisor.supervise(name, [&](cnetmod::cancel_token& token) -> cnetmod::task<std::expected<void, std::error_code>>
            {
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::seconds{10}, token);
                ++settled;
                co_return std::expected<void, std::error_code>{};
            },
            {}, true, [&]
            {
                ++callbacks;
                throw std::system_error(std::make_error_code(std::errc::permission_denied));
            }));
    bool completed = false;
    auto run = [&]() -> cnetmod::task<void>
    {
        supervisor.request_stop();
        supervisor.request_stop();
        const auto result = co_await supervisor.join();
        ASSERT_FALSE(result.has_value());
        ASSERT_EQ(result.error(), std::make_error_code(std::errc::permission_denied));
        ASSERT_EQ(callbacks, 2U);
        ASSERT_EQ(settled, 2U);
        for (const auto name : {"a-task", "b-task"})
        {
            ASSERT_EQ(supervisor.last_error(name), result.error());
            ASSERT_TRUE(supervisor.state(name) == application::supervised_task_state::failed);
        }
        completed = true;
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(completed);
}

TEST(application_supervisor_join_waits_for_foreign_thread_stop_callback)
{
    auto io = cnetmod::make_io_context();
    application::task_supervisor supervisor{*io};
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    ASSERT_TRUE(supervisor.supervise("callback", [](cnetmod::cancel_token&) -> cnetmod::task<std::expected<void, std::error_code>>
        {
            co_return std::expected<void, std::error_code>{};
        },
        {}, true, [&]
        {
            entered.store(true);
            const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            while (!release.load() && std::chrono::steady_clock::now() < limit)
                std::this_thread::yield();
            throw std::system_error(std::make_error_code(std::errc::permission_denied));
        }));
    std::jthread stopper([&]
        {
            supervisor.request_stop();
        });
    bool completed = false;
    auto run = [&]() -> cnetmod::task<void>
    {
        const auto thread = std::this_thread::get_id();
        while (!entered.load())
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
        std::thread::id resumed;
        auto wait = [&]() -> cnetmod::task<void>
        {
            const auto result = co_await supervisor.join();
            resumed = std::this_thread::get_id();
            ASSERT_FALSE(result.has_value());
            ASSERT_EQ(result.error(), std::make_error_code(std::errc::permission_denied));
        };
        auto join = wait();
        join.handle().resume();
        (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{10});
        ASSERT_FALSE(join.handle().done());
        release.store(true);
        while (!join.handle().done())
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
        ASSERT_EQ(resumed, thread);
        ASSERT_EQ(supervisor.last_error("callback"), std::make_error_code(std::errc::permission_denied));
        completed = true;
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    stopper.join();
    ASSERT_TRUE(completed);
}

TEST(application_task_supervisor_join_distinguishes_required_and_optional_failures)
{
    for (bool required : {false, true})
    {
        auto io = cnetmod::make_io_context();
        application::task_supervisor supervisor{*io};
        auto add = [&](std::string name, std::errc error)
        {
            return supervisor.supervise(std::move(name), [error](cnetmod::cancel_token&) -> cnetmod::task<std::expected<void, std::error_code>>
                {
                    co_return std::unexpected(std::make_error_code(error));
                },
                {.budget = std::chrono::milliseconds{0}}, required);
        };
        ASSERT_TRUE(add("z-task", std::errc::connection_refused));
        ASSERT_TRUE(add("a-task", std::errc::permission_denied));
        std::expected<void, std::error_code> result;
        auto wait = [&]() -> cnetmod::task<void>
        {
            result = co_await supervisor.join();
            io->stop();
        };
        auto joining = wait();
        joining.handle().resume();
        io->run();
        ASSERT_EQ(result.has_value(), !required);
        if (required)
            ASSERT_EQ(result.error(), std::make_error_code(std::errc::permission_denied));
        ASSERT_EQ(supervisor.last_error("z-task"), std::make_error_code(std::errc::connection_refused));
        ASSERT_TRUE(supervisor.state("z-task") == application::supervised_task_state::failed);
    }
}

TEST(application_host_preserves_required_worker_failure_through_cleanup)
{
    cnetmod::net_init network;
    auto port_reservation = cnetmod::socket::create(
        cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(port_reservation.has_value());
    if (!port_reservation)
        return;
    ASSERT_TRUE(port_reservation->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    const auto reserved_endpoint = port_reservation->local_endpoint();
    ASSERT_TRUE(reserved_endpoint.has_value());
    if (!reserved_endpoint)
        return;

    auto events = std::make_shared<std::vector<std::string>>();
    auto service = std::make_shared<fake_service>(application::service_key{"worker", "test"},
        std::vector<application::service_key>{}, application::service_requirement::required, events, 0, true);
    const auto port = reserved_endpoint->port();
    auto built = application::application_builder{"worker-failure-test"}
                     .configure([port](application::application_configuration& value)
                         {
                             value.http.port = port;
                             value.management.enabled = false;
                             value.install_signal_handlers = false;
                             value.logging.manage_lifecycle = false;
                             value.observability.tracing = false;
                             value.observability.metrics = false;
                             value.observability.logs = false;
                             value.lifecycle.http_drain_timeout = std::chrono::milliseconds{100};
                             value.lifecycle.total_stop_timeout = std::chrono::milliseconds{80};
                         })
                     .service(service)
                     .build();
    ASSERT_TRUE(built.has_value());
    port_reservation->close();
    const auto begin = std::chrono::steady_clock::now();
    const auto result = built->run();
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), std::make_error_code(std::errc::permission_denied));
    ASSERT_TRUE(events->size() >= 3U);
    ASSERT_TRUE(events->size() < 30U);
    ASSERT_TRUE(std::chrono::steady_clock::now() - begin < std::chrono::seconds{2});
    ASSERT_TRUE(built->state() == application::application_state::cleanup_failed);
    ASSERT_TRUE(events->back().starts_with("stop:"));
}

TEST(application_host_accepts_concurrent_stop_requests)
{
    cnetmod::net_init network;
    auto port_reservation = cnetmod::socket::create(
        cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(port_reservation.has_value());
    if (!port_reservation)
        return;
    ASSERT_TRUE(port_reservation->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    const auto reserved_endpoint = port_reservation->local_endpoint();
    ASSERT_TRUE(reserved_endpoint.has_value());
    if (!reserved_endpoint)
        return;
    const auto port = reserved_endpoint->port();
    auto built = application::application_builder{"concurrent-stop-test"}
                     .configure([port](application::application_configuration& value)
                         {
                             value.http.port = port;
                             value.logging.manage_lifecycle = false;
                             value.management.enabled = false;
                             value.install_signal_handlers = false;
                             value.lifecycle.http_drain_timeout =
                                 std::chrono::milliseconds{100};
                         })
                     .build();
    ASSERT_TRUE(built.has_value());
    if (!built)
        return;
    auto host = std::move(*built);
    port_reservation->close();
    std::optional<std::expected<void, std::error_code>> result;
    std::jthread runner([&]
        {
            result = host.run();
        });
    const auto timeout = std::chrono::steady_clock::now() +
        std::chrono::seconds{2};
    while (host.state() != application::application_state::running &&
        std::chrono::steady_clock::now() < timeout)
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    ASSERT_TRUE(host.state() == application::application_state::running);
    std::array<std::jthread, 4> stoppers{
        std::jthread{[&]
            {
                host.request_stop();
            }},
        std::jthread{[&]
            {
                host.request_stop();
            }},
        std::jthread{[&]
            {
                host.request_stop();
            }},
        std::jthread{[&]
            {
                host.request_stop();
            }},
    };
    for (auto& stopper : stoppers)
        stopper.join();
    runner.join();
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->has_value());
    ASSERT_TRUE(host.state() == application::application_state::stopped);
}

TEST(application_management_scrape_exposes_exporter_statistics_only_when_enabled)
{
    cnetmod::net_init network;
    for (const bool metrics_enabled : {false, true})
    {
        auto business_reservation = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
        auto management_reservation = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
        auto collector_reservation = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
        ASSERT_TRUE(business_reservation.has_value() && management_reservation.has_value() &&
            collector_reservation.has_value());
        if (!business_reservation || !management_reservation || !collector_reservation)
            return;
        ASSERT_TRUE(business_reservation->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
        ASSERT_TRUE(management_reservation->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
        ASSERT_TRUE(collector_reservation->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
        const auto business_endpoint = business_reservation->local_endpoint();
        const auto management_endpoint = management_reservation->local_endpoint();
        const auto collector_endpoint = collector_reservation->local_endpoint();
        ASSERT_TRUE(business_endpoint.has_value() && management_endpoint.has_value() &&
            collector_endpoint.has_value());
        if (!business_endpoint || !management_endpoint || !collector_endpoint)
            return;
        std::atomic<unsigned> collector_requests{};
        auto collector_io = cnetmod::make_io_context();
        cnetmod::http::router collector_routes;
        collector_routes.post("/test-collector", [&](cnetmod::http::request_context& request) -> cnetmod::task<void>
            {
                (void)co_await request.read_full_body();
                const auto index = collector_requests.fetch_add(1U, std::memory_order_relaxed);
                request.json(cnetmod::http::status::ok,
                    index == 0 ? "private-invalid-acknowledgement" : "{}");
                co_return;
            });
        cnetmod::http::server collector{*collector_io};
        collector.set_router(std::move(collector_routes));
        collector_reservation->close();
        ASSERT_TRUE(collector.listen("127.0.0.1", collector_endpoint->port()).has_value());
        auto built = application::application_builder{"management-scrape-test"}
                         .configure([&](auto& value)
                             {
                                 value.http.address = "127.0.0.1";
                                 value.http.port = business_endpoint->port();
                                 value.http.access_logging = false;
                                 value.management.enabled = true;
                                 value.management.same_port = false;
                                 value.management.address = "127.0.0.1";
                                 value.management.port = management_endpoint->port();
                                 value.logging.manage_lifecycle = false;
                                 value.install_signal_handlers = false;
                                 value.observability.metrics = metrics_enabled;
                                 value.observability.tracing = false;
                                 value.observability.logs = metrics_enabled;
                                 value.observability.otlp = {};
                                 if (metrics_enabled)
                                     value.observability.otlp.logs_endpoint = std::format(
                                         "http://127.0.0.1:{}/test-collector", collector_endpoint->port());
                                 value.health.interval = std::chrono::milliseconds{10};
                                 value.lifecycle.http_drain_timeout = std::chrono::milliseconds{100};
                             })
                         .build();
        ASSERT_TRUE(built.has_value());
        if (!built)
            return;
        auto collector_accept = collector.run();
        collector_accept.handle().resume();
        std::jthread collector_runner([&]
            {
                collector_io->run();
            });
        auto host = std::move(*built);
        business_reservation->close();
        management_reservation->close();
        std::optional<std::expected<void, std::error_code>> completed;
        std::jthread runner([&]
            {
                completed = host.run();
            });
        const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (host.state() != application::application_state::running &&
            host.state() != application::application_state::stopped && std::chrono::steady_clock::now() < timeout)
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        const bool started = host.state() == application::application_state::running;
        if (started && metrics_enabled)
            ASSERT_TRUE(host.telemetry().submit_log({.body = "test-only-log-body"}));
        bool scraped{};
        bool isolated{};
        bool ready{};
        bool recovered = !metrics_enabled;
        auto io = cnetmod::make_io_context();
        auto inspect = [&]() -> cnetmod::task<void>
        {
            cnetmod::observability::otlp_http_options telemetry_options;
            telemetry_options.export_traces = false;
            telemetry_options.export_metrics = false;
            telemetry_options.export_logs = false;
            cnetmod::observability::telemetry_hub telemetry{
                *io, std::move(telemetry_options)};
            application::rest_template rest{*io, telemetry,
                {.request_timeout = std::chrono::milliseconds{300},
                    .keep_alive = false}};
            const auto url = std::format("http://127.0.0.1:{}", management_endpoint->port());
            for (unsigned attempt = 0; attempt < 20; ++attempt)
            {
                const auto response = co_await rest.get(url + "/actuator/prometheus");
                scraped = response && (metrics_enabled ? response->status_code() == 200 && response->body().contains("otel_exporter_invalid_responses_total 1\n") && response->body().contains("otel_exporter_failed_batches_total 1\n") && response->body().contains("otel_exporter_accepted_logs_total 1\n") && response->body().contains("otel_exporter_exported_records_total 0\n") && response->body().contains("otel_exporter_retries_total 0\n") && !response->body().contains("private-invalid-acknowledgement") && !response->body().contains("test-only-log-body") : response->status_code() == 404);
                if (scraped)
                    break;
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{10});
            }
            const auto readiness = co_await rest.get(url + "/actuator/ready");
            ready = readiness && readiness->status_code() == 200;
            if (metrics_enabled && scraped)
            {
                const bool queued = host.telemetry().submit_log({.body = "recovery-test-log"});
                for (unsigned attempt = 0; queued && attempt < 20; ++attempt)
                {
                    const auto response = co_await rest.get(url + "/actuator/prometheus");
                    recovered = response && response->status_code() == 200 &&
                        response->body().contains("otel_exporter_exported_records_total 1\n") &&
                        response->body().contains("otel_exporter_accepted_logs_total 2\n") &&
                        response->body().contains("otel_exporter_failed_batches_total 1\n") &&
                        response->body().contains("otel_exporter_invalid_responses_total 1\n");
                    if (recovered)
                        break;
                    (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{10});
                }
            }
            const auto business = co_await rest.get(std::format(
                "http://127.0.0.1:{}/actuator/prometheus", business_endpoint->port()));
            isolated = business && business->status_code() == 404;
            rest.clear();
            io->stop();
        };
        if (started)
        {
            cnetmod::spawn(*io, inspect());
            io->run();
        }
        host.request_stop();
        runner.join();
        collector.stop();
        collector.abort_connections();
        collector_io->stop();
        collector_runner.join();
        ASSERT_TRUE(started);
        ASSERT_TRUE(scraped);
        ASSERT_TRUE(isolated);
        ASSERT_TRUE(ready);
        ASSERT_TRUE(recovered);
        ASSERT_EQ(collector_requests.load(std::memory_order_relaxed), metrics_enabled ? 2U : 0U);
        ASSERT_TRUE(completed.has_value() && completed->has_value());
    }
}

#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
TEST(mongodb_supervisor_joins_before_service_stop)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io};
    application::task_supervisor supervisor{*io};
    cnetmod::cancel_token cancellation;
    application::service_context context{*io, telemetry, supervisor, cancellation, {}};
    cnetmod::mongodb::connection_pool_options options;
    options.minimum_size = 0;
    options.health_check_interval = std::chrono::hours{1};
    application::mongodb_service service{*io, options, "test",
        application::service_requirement::required, {}};
    bool joined = false;
    auto run = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE((co_await service.start(context)).has_value());
        (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{10});
        supervisor.request_stop();
        joined = (co_await supervisor.join()).has_value();
        ASSERT_TRUE((co_await service.stop(context)).has_value());
        io->stop();
    };
    cnetmod::spawn(*io, run());
    const auto started = std::chrono::steady_clock::now();
    io->run();
    ASSERT_TRUE(joined);
    ASSERT_TRUE(std::chrono::steady_clock::now() - started < std::chrono::seconds{2});
}
#endif

#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
TEST(mysql_health_rejects_allocated_but_unconnected_pool)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    cnetmod::observability::telemetry_hub telemetry{*io};
    application::task_supervisor supervisor{*io};
    cnetmod::mysql::pool_params options;
    options.port = endpoint->port();
    options.ssl = cnetmod::mysql::ssl_mode::disable;
    application::mysql_service service{*io, options, "test", application::service_requirement::required, {}};
    std::optional<cnetmod::socket> peer;
    bool checked = false;
    auto server = [&]() -> cnetmod::task<void>
    {
        auto accepted = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(accepted.has_value());
        peer.emplace(std::move(*accepted));
    };
    auto run = [&]() -> cnetmod::task<void>
    {
        cnetmod::cancel_token startup;
        application::service_context start_context{*io, telemetry, supervisor, startup,
            cnetmod::deadline::after(std::chrono::milliseconds{20})};
        ASSERT_FALSE((co_await service.start(start_context)).has_value());
        (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{20});
        ASSERT_TRUE(peer.has_value());
        ASSERT_EQ(service.pool().size(), 1U);
        cnetmod::cancel_token probe;
        application::service_context probe_context{*io, telemetry, supervisor, probe,
            cnetmod::deadline::after(std::chrono::milliseconds{10})};
        auto health = co_await service.probe(probe_context);
        ASSERT_TRUE(health.status == application::service_health::down);
        ASSERT_TRUE(health.error == std::errc::timed_out);
        supervisor.request_stop();
        ASSERT_TRUE((co_await supervisor.join()).has_value());
        ASSERT_TRUE((co_await service.stop(start_context)).has_value());
        checked = true;
        io->stop();
    };
    cnetmod::spawn(*io, server());
    cnetmod::spawn(*io, run());
    io->run();
    ASSERT_TRUE(checked);
}
#endif

#include "application_mysql_recovery_cases.inc"
#include "application_runtime_health_cases.inc"
#include "application_shutdown_cases.inc"

RUN_TESTS();
