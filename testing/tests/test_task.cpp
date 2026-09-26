/// cnetmod unit tests — task<T>, task<void>, sync_wait, when_all

#include "test_framework.hpp"

import std;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.bridge;
import cnetmod.coro.mutex;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.executor.scheduler;

using namespace cnetmod;

// =============================================================================
// Helper coroutines
// =============================================================================

static auto return_42() -> task<int>
{
    co_return 42;
}

static auto return_string() -> task<std::string>
{
    co_return std::string("hello");
}

static auto return_void() -> task<void>
{
    co_return;
}

static auto add(int a, int b) -> task<int>
{
    co_return a + b;
}

static auto chain_add() -> task<int>
{
    auto x = co_await return_42();
    auto y = co_await add(x, 8);
    co_return y;
}

static auto throwing_task() -> task<int>
{
    throw std::runtime_error("test error");
    co_return 0;
}

static auto void_throwing_task() -> task<void>
{
    throw std::runtime_error("void error");
    co_return;
}

struct delayed_resume_awaitable
{
    auto await_ready() const noexcept -> bool
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> continuation) const
    {
        std::thread([continuation]
            {
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
                continuation.resume();
            })
            .detach();
    }

    void await_resume() const noexcept {}
};

static auto completes_after_external_resume() -> task<int>
{
    co_await delayed_resume_awaitable{};
    co_return 7;
}

static auto records_execution_thread(std::thread::id& execution_thread) -> task<int>
{
    execution_thread = std::this_thread::get_id();
    co_return 9;
}

static auto records_current_context(io_context*& current) -> task<void>
{
    current = io_context::current();
    co_return;
}

static auto completes_on(io_context& context) -> task<int>
{
    co_await post_awaitable{context};
    co_return 17;
}

static auto throws_on(io_context& context) -> task<int>
{
    co_await post_awaitable{context};
    throw std::runtime_error{"resume failure"};
}

static auto schedules_on(io_scheduler scheduler,
    std::thread::id& execution_thread) -> task<int>
{
    co_await scheduler.schedule();
    execution_thread = std::this_thread::get_id();
    co_return 11;
}

static auto catches_blocking_exception_on_io_context(thread_pool& pool,
    io_context& context, std::thread::id& pool_thread,
    std::thread::id& catch_thread, bool& caught) -> task<void>
{
    try
    {
        (void)co_await blocking_invoke(pool, context, [&]() -> int
            {
                pool_thread = std::this_thread::get_id();
                throw std::runtime_error("blocking failure");
            });
    }
    catch (const std::runtime_error&)
    {
        catch_thread = std::this_thread::get_id();
        caught = true;
    }
}

static auto switches_to_pool(thread_pool& pool) -> task<void>
{
    co_await pool_post_awaitable{pool};
}

// =============================================================================
// Tests
// =============================================================================

TEST(task_int_basic)
{
    auto result = sync_wait(return_42());
    ASSERT_EQ(result, 42);
}

TEST(task_string_basic)
{
    auto result = sync_wait(return_string());
    ASSERT_EQ(result, std::string("hello"));
}

TEST(task_void_basic)
{
    // Should not throw
    sync_wait(return_void());
    ASSERT_TRUE(true);
}

TEST(task_chained_await)
{
    auto result = sync_wait(chain_add());
    ASSERT_EQ(result, 50);
}

TEST(task_exception_propagation)
{
    ASSERT_THROWS(sync_wait(throwing_task()));
}

TEST(task_void_exception_propagation)
{
    ASSERT_THROWS(sync_wait(void_throwing_task()));
}

TEST(task_move_semantics)
{
    auto t = return_42();
    auto t2 = std::move(t);
    auto result = sync_wait(std::move(t2));
    ASSERT_EQ(result, 42);
}

TEST(task_empty_await_is_deterministic)
{
    // Default construction and a moved-from task are valid object states.
    // Awaiting either must report a normal logic error instead of
    // dereferencing a null coroutine handle.
    task<int> empty_int;
    ASSERT_THROWS(sync_wait(std::move(empty_int)));
    task<void> empty_void;
    ASSERT_THROWS(sync_wait(std::move(empty_void)));
}

TEST(sync_wait_waits_for_asynchronous_completion)
{
    ASSERT_EQ(sync_wait(completes_after_external_resume()), 7);
}

TEST(starts_on_starts_task_on_target_io_context)
{
    auto context = make_io_context();
    std::thread::id context_thread;
    std::thread runner{[&]
        {
            context_thread = std::this_thread::get_id();
            context->run();
        }};

    std::thread::id execution_thread;
    const auto result = sync_wait(
        starts_on(*context, records_execution_thread(execution_thread)));

    context->stop();
    runner.join();
    ASSERT_EQ(result, 9);
    ASSERT_EQ(execution_thread, context_thread);
}

TEST(io_context_current_tracks_the_dispatching_loop)
{
    ASSERT_TRUE(io_context::current() == nullptr);
    auto context = make_io_context();
    io_context* observed = nullptr;
    std::thread runner{[&] { context->run(); }};

    sync_wait(starts_on(*context, records_current_context(observed)));

    context->stop();
    runner.join();
    ASSERT_TRUE(observed == context.get());
    ASSERT_TRUE(io_context::current() == nullptr);
}

TEST(resume_on_returns_values_and_exceptions_to_target_context)
{
    auto source = make_io_context();
    auto target = make_io_context();
    std::jthread source_thread{[&] { source->run(); }};
    std::jthread target_thread{[&] { target->run(); }};

    int result = 0;
    io_context* value_context = nullptr;
    io_context* error_context = nullptr;
    auto verify = [&]() -> task<void>
    {
        result = co_await resume_on(*target, completes_on(*source));
        value_context = io_context::current();
        try
        {
            (void)co_await resume_on(*target, throws_on(*source));
        }
        catch (const std::runtime_error&)
        {
            error_context = io_context::current();
        }
    };
    sync_wait(verify());

    source->stop();
    target->stop();
    ASSERT_EQ(result, 17);
    ASSERT_TRUE(value_context == target.get());
    ASSERT_TRUE(error_context == target.get());
}

TEST(io_scheduler_schedule_resumes_on_target_io_context)
{
    auto context = make_io_context();
    std::thread::id context_thread;
    std::thread runner{[&]
        {
            context_thread = std::this_thread::get_id();
            context->run();
        }};

    std::thread::id execution_thread;
    const auto result = sync_wait(
        schedules_on(io_scheduler{*context}, execution_thread));

    context->stop();
    runner.join();
    ASSERT_EQ(result, 11);
    ASSERT_EQ(execution_thread, context_thread);
}

TEST(io_context_restart_preserves_work_posted_while_stopped)
{
    auto context = make_io_context();
    context->stop();

    std::atomic<bool> callback_ran{};
    context->post(
        [](void* state) noexcept
        {
            static_cast<std::atomic<bool>*>(state)->store(
                true, std::memory_order_release);
        },
        &callback_ran);
    context->restart();

    std::jthread watchdog{[&](std::stop_token stop_token)
        {
            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::seconds{2};
            while (!stop_token.stop_requested() &&
                !callback_ran.load(std::memory_order_acquire) &&
                std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            context->stop();
        }};

    context->run();
    watchdog.request_stop();
    watchdog.join();
    ASSERT_TRUE(callback_ran.load(std::memory_order_acquire));
}

TEST(blocking_invoke_rethrows_on_requested_io_context)
{
    auto context = make_io_context();
    thread_pool pool{1};
    std::thread::id context_thread;
    std::thread runner{[&]
        {
            context_thread = std::this_thread::get_id();
            context->run();
        }};

    std::thread::id pool_thread;
    std::thread::id catch_thread;
    bool caught = false;
    sync_wait(catches_blocking_exception_on_io_context(
        pool, *context, pool_thread, catch_thread, caught));

    context->stop();
    runner.join();
    ASSERT_TRUE(caught);
    ASSERT_EQ(catch_thread, context_thread);
    ASSERT_TRUE(pool_thread != context_thread);
}

TEST(pool_post_reports_stopped_completion)
{
    thread_pool pool{1};
    pool.request_stop();
    ASSERT_THROWS(sync_wait(switches_to_pool(pool)));
}

TEST(spawn_releases_unstarted_coroutine_when_context_is_destroyed)
{
    std::weak_ptr<int> retained;
    {
        auto ctx = make_io_context();
        auto value = std::make_shared<int>(42);
        retained = value;
        spawn(*ctx, [](std::shared_ptr<int> captured) -> task<void>
            {
                (void)captured;
                co_return;
            }(value));
        value.reset();
    }
    ASSERT_TRUE(retained.expired());
}

TEST(when_all_two_ints)
{
    auto both = when_all(return_42(), add(10, 20));
    auto [a, b] = sync_wait(std::move(both));
    ASSERT_EQ(a, 42);
    ASSERT_EQ(b, 30);
}

TEST(when_all_three_ints)
{
    auto all = when_all(return_42(), add(1, 2), add(10, 10));
    auto [a, b, c] = sync_wait(std::move(all));
    ASSERT_EQ(a, 42);
    ASSERT_EQ(b, 3);
    ASSERT_EQ(c, 20);
}

TEST(when_all_void)
{
    // Should not throw
    sync_wait(when_all(return_void(), return_void()));
    ASSERT_TRUE(true);
}

TEST(when_all_waits_for_external_resumes)
{
    // Both children complete from independent external threads.  This covers
    // the handoff from when_all's startup phase to a genuinely suspended
    // parent, where resuming inline during await_suspend would be unsafe.
    auto [first, second] = sync_wait(when_all(completes_after_external_resume(),
        completes_after_external_resume()));
    ASSERT_EQ(first, 7);
    ASSERT_EQ(second, 7);
}

// =============================================================================
// Extended Tests
// =============================================================================

static auto return_expected_ok() -> task<std::expected<int, std::string>>
{
    co_return 42;
}

static auto return_expected_err() -> task<std::expected<int, std::string>>
{
    co_return std::unexpected(std::string("fail"));
}

TEST(task_expected_ok)
{
    auto result = sync_wait(return_expected_ok());
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, 42);
}

TEST(task_expected_error)
{
    auto result = sync_wait(return_expected_err());
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), std::string("fail"));
}

// 10-level nested co_await
template <int N>
static auto nested() -> task<int>
{
    if constexpr (N <= 0)
        co_return 1;
    else
        co_return co_await nested<N - 1>() + 1;
}

TEST(task_nested_co_await_10)
{
    auto result = sync_wait(nested<10>());
    ASSERT_EQ(result, 11);
}

TEST(task_move_only_type)
{
    auto make_ptr = []() -> task<std::unique_ptr<int>>
    {
        co_return std::make_unique<int>(99);
    };
    auto result = sync_wait(make_ptr());
    ASSERT_TRUE(result != nullptr);
    ASSERT_EQ(*result, 99);
}

TEST(when_all_mixed_types)
{
    auto [i, s] = sync_wait(when_all(return_42(), return_string()));
    ASSERT_EQ(i, 42);
    ASSERT_EQ(s, std::string("hello"));
}

TEST(when_all_exception_propagation)
{
    // Exception in one task should propagate when extracting result
    bool caught = false;
    try
    {
        auto [a, b] = sync_wait(when_all(throwing_task(), return_42()));
        (void)a;
        (void)b;
    }
    catch (const std::runtime_error&)
    {
        caught = true;
    }
    ASSERT_TRUE(caught);
}

TEST(when_all_single_task)
{
    // when_all requires >= 2 tasks; test single task via direct sync_wait
    auto r = sync_wait(return_42());
    ASSERT_EQ(r, 42);
}

TEST(guarded_spawn_reports_task_failure_and_contains_observer_failure)
{
    auto io = make_io_context();
    unsigned failures{};
    bool original{};
    auto failing = []() -> task<void>
    {
        throw std::runtime_error("background failure");
        co_return;
    };
    spawn_guarded(*io, failing(), [&](std::exception_ptr error)
        {
            ++failures;
            try
            {
                std::rethrow_exception(error);
            }
            catch (const std::runtime_error& exception)
            {
                original = std::string_view{exception.what()} == "background failure";
            }
            io->stop();
            throw std::runtime_error("observer failure");
        });
    io->run();
    ASSERT_EQ(failures, 1U);
    ASSERT_TRUE(original);
}

TEST(raw_post_node_can_release_its_storage_during_dispatch)
{
    auto io = make_io_context();
    auto node = std::make_unique<post_node>();
    node->callback_arg = &node;
    node->callback = [](void* value)
    {
        static_cast<std::unique_ptr<post_node>*>(value)->reset();
    };
    io->post_node_raw(node.get());
    io->poll();
    ASSERT_TRUE(node == nullptr);
}

TEST(raw_post_node_ownership_is_captured_before_callback)
{
    auto io = make_io_context();
    post_node node;
    node.callback_arg = &node;
    node.callback = [](void* value)
    {
        static_cast<post_node*>(value)->heap_owned = true;
    };
    io->post_node_raw(&node);
    io->poll();
    ASSERT_TRUE(node.heap_owned);
    node.heap_owned = false;
}

TEST(async_mutex_resumes_waiter_on_its_event_loop)
{
    auto io = make_io_context();
    async_mutex mutex;
    ASSERT_TRUE(mutex.try_lock());
    std::atomic<bool> waiting{false};
    std::atomic<bool> resumed_on_owner{false};

    auto waiter = [&]() -> task<void>
    {
        waiting.store(true, std::memory_order_release);
        co_await mutex.lock();
        resumed_on_owner.store(io_context::current() == io.get(),
            std::memory_order_release);
        mutex.unlock();
        io->stop();
    };
    spawn(*io, waiter());
    std::jthread runner([&] { io->run(); });
    while (!waiting.load(std::memory_order_acquire))
        std::this_thread::yield();
    mutex.unlock();
    runner.join();
    ASSERT_TRUE(resumed_on_owner.load(std::memory_order_acquire));
}

RUN_TESTS()
