#include "test_framework.hpp"

import std;
import cnetmod.coro.cancel;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.io.io_context;
import cnetmod.protocol.http.client.pool;

namespace {

using cnetmod::async_sleep;
using cnetmod::cancel_token;
using cnetmod::make_io_context;
using cnetmod::spawn;
using cnetmod::task;
using cnetmod::http::bounded_client_pool;
using cnetmod::http::bounded_client_pool_options;
using cnetmod::http::pooled_client;

TEST(bounded_client_pool_hands_leases_to_waiters_in_fifo_order)
{
    auto context = make_io_context();
    bounded_client_pool pool{*context, bounded_client_pool_options{
                                           .maximum_clients = 1,
                                           .maximum_waiters = 2,
                                           .acquire_timeout = std::chrono::seconds{1},
                                       }};
    std::vector<int> order;
    std::size_t completed = 0;
    bool queued_metrics = false;
    bool failed = false;

    auto worker = [&](int id) -> task<void>
    {
        {
            auto lease = co_await pool.acquire();
            if (!lease)
            {
                failed = true;
                co_return;
            }
            order.push_back(id);
            co_await async_sleep(*context, std::chrono::milliseconds{1});
        }
        ++completed;
        if (completed == 2)
        {
            co_await pool.close();
            context->stop();
        }
    };

    auto scenario = [&]() -> task<void>
    {
        auto initial = co_await pool.acquire();
        if (!initial)
        {
            failed = true;
            context->stop();
            co_return;
        }
        pooled_client held = std::move(*initial);
        spawn(*context, worker(1));
        spawn(*context, worker(2));
        co_await async_sleep(*context, std::chrono::milliseconds{2});
        queued_metrics = pool.size() == 1 &&
            pool.checked_out_count() == 1 &&
            pool.idle_count() == 0 &&
            pool.waiter_count() == 2;
        held = pooled_client{};
    };

    auto watchdog = [&]() -> task<void>
    {
        co_await async_sleep(*context, std::chrono::seconds{2});
        failed = true;
        context->stop();
    };

    spawn(*context, scenario());
    spawn(*context, watchdog());
    context->run();

    ASSERT_FALSE(failed);
    ASSERT_TRUE(queued_metrics);
    ASSERT_EQ(order.size(), std::size_t{2});
    if (order.size() == 2)
    {
        ASSERT_EQ(order[0], 1);
        ASSERT_EQ(order[1], 2);
    }
    ASSERT_EQ(pool.size(), std::size_t{0});
}

TEST(bounded_client_pool_times_out_without_leaking_a_waiter)
{
    auto context = make_io_context();
    bounded_client_pool pool{*context, bounded_client_pool_options{
                                           .maximum_clients = 1,
                                           .maximum_waiters = 1,
                                           .acquire_timeout = std::chrono::milliseconds{10},
                                       }};
    bool timed_out = false;

    auto scenario = [&]() -> task<void>
    {
        auto initial = co_await pool.acquire();
        if (!initial)
        {
            context->stop();
            co_return;
        }
        pooled_client held = std::move(*initial);
        auto waiting = co_await pool.acquire();
        timed_out = !waiting &&
            waiting.error() ==
                std::make_error_code(std::errc::timed_out) &&
            pool.waiter_count() == 0;
        held = pooled_client{};
        co_await pool.close();
        context->stop();
    };

    spawn(*context, scenario());
    context->run();

    ASSERT_TRUE(timed_out);
    ASSERT_EQ(pool.size(), std::size_t{0});
}

TEST(bounded_client_pool_cancels_a_queued_acquisition)
{
    auto context = make_io_context();
    bounded_client_pool pool{*context, bounded_client_pool_options{
                                           .maximum_clients = 1,
                                           .maximum_waiters = 1,
                                           .acquire_timeout = std::chrono::milliseconds{0},
                                       }};
    cancel_token cancellation;
    bool cancelled = false;

    auto cancel_waiter = [&]() -> task<void>
    {
        co_await async_sleep(*context, std::chrono::milliseconds{2});
        cancellation.cancel();
    };

    auto scenario = [&]() -> task<void>
    {
        auto initial = co_await pool.acquire();
        if (!initial)
        {
            context->stop();
            co_return;
        }
        pooled_client held = std::move(*initial);
        spawn(*context, cancel_waiter());
        auto waiting = co_await pool.acquire(cancellation);
        cancelled = !waiting &&
            waiting.error() == std::make_error_code(std::errc::operation_canceled) &&
            pool.waiter_count() == 0;
        held = pooled_client{};
        co_await pool.close();
        context->stop();
    };

    spawn(*context, scenario());
    context->run();

    ASSERT_TRUE(cancelled);
    ASSERT_EQ(pool.size(), std::size_t{0});
}

TEST(bounded_client_pool_rejects_excess_waiters)
{
    auto context = make_io_context();
    bounded_client_pool pool{*context, bounded_client_pool_options{
                                           .maximum_clients = 1,
                                           .maximum_waiters = 0,
                                           .acquire_timeout = std::chrono::milliseconds{0},
                                       }};
    bool rejected = false;

    auto scenario = [&]() -> task<void>
    {
        auto initial = co_await pool.acquire();
        if (!initial)
        {
            context->stop();
            co_return;
        }
        pooled_client held = std::move(*initial);
        cancel_token cancellation;
        auto excess = co_await pool.acquire(cancellation);
        rejected = !excess &&
            excess.error() == std::make_error_code(std::errc::resource_unavailable_try_again);
        held = pooled_client{};
        co_await pool.close();
        context->stop();
    };

    spawn(*context, scenario());
    context->run();

    ASSERT_TRUE(rejected);
}

TEST(bounded_client_pool_close_wakes_waiters_and_settles_leases)
{
    auto context = make_io_context();
    bounded_client_pool pool{*context, bounded_client_pool_options{
                                           .maximum_clients = 1,
                                           .maximum_waiters = 1,
                                           .acquire_timeout = std::chrono::milliseconds{0},
                                       }};
    bool waiter_cancelled = false;
    bool close_completed = false;
    bool failed = false;

    auto waiting_request = [&]() -> task<void>
    {
        cancel_token cancellation;
        auto result = co_await pool.acquire(cancellation);
        waiter_cancelled = !result &&
            result.error() == std::make_error_code(std::errc::operation_canceled);
    };

    auto close_pool = [&]() -> task<void>
    {
        co_await pool.close();
        close_completed = true;
        context->stop();
    };

    auto scenario = [&]() -> task<void>
    {
        auto initial = co_await pool.acquire();
        if (!initial)
        {
            failed = true;
            context->stop();
            co_return;
        }
        pooled_client held = std::move(*initial);
        spawn(*context, waiting_request());
        co_await async_sleep(*context, std::chrono::milliseconds{2});
        if (pool.waiter_count() != 1)
        {
            failed = true;
            context->stop();
            co_return;
        }
        spawn(*context, close_pool());
        co_await async_sleep(*context, std::chrono::milliseconds{2});
        if (!waiter_cancelled || close_completed)
        {
            failed = true;
            context->stop();
            co_return;
        }
        held = pooled_client{};
    };

    auto watchdog = [&]() -> task<void>
    {
        co_await async_sleep(*context, std::chrono::seconds{2});
        failed = true;
        context->stop();
    };

    spawn(*context, scenario());
    spawn(*context, watchdog());
    context->run();

    ASSERT_FALSE(failed);
    ASSERT_TRUE(waiter_cancelled);
    ASSERT_TRUE(close_completed);
    ASSERT_EQ(pool.size(), std::size_t{0});
    ASSERT_EQ(pool.waiter_count(), std::size_t{0});
}

} // namespace

auto main() -> int
{
    return cnetmod::test::run_all();
}
