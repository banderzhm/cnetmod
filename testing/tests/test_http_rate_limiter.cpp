#include "test_framework.hpp"

import std;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.rate_limiter;

TEST(rate_limiter_shares_one_process_wide_budget_across_event_loops)
{
    constexpr std::size_t loop_count = 4;
    constexpr std::size_t requests_per_loop = 32;
    constexpr std::size_t burst = 32;

    auto limiter = cnetmod::rate_limiter({
        .rate = 0.000001,
        .burst = static_cast<double>(burst),
        .key_fn = [](cnetmod::http::request_context&)
        {
            return std::string{"shared-client"};
        },
    });
    std::atomic<std::size_t> admitted{};
    std::atomic<std::size_t> rejected{};
    std::barrier start{static_cast<std::ptrdiff_t>(loop_count)};
    std::vector<std::jthread> workers;
    workers.reserve(loop_count);

    for (std::size_t worker = 0; worker < loop_count; ++worker)
    {
        auto worker_limiter = limiter;
        workers.emplace_back([&, worker_limiter = std::move(worker_limiter)]() mutable
            {
                auto io = cnetmod::make_io_context();
                cnetmod::socket peer;
                auto work = [&]() -> cnetmod::task<void>
                {
                    for (std::size_t request = 0; request < requests_per_loop;
                        ++request)
                    {
                        cnetmod::http::response response;
                        cnetmod::http::header_map headers;
                        cnetmod::http::request_context context{*io, peer, "GET",
                            "/limited", headers, {}, response, {}};
                        co_await worker_limiter(context,
                            [&]() -> cnetmod::task<void>
                            {
                                admitted.fetch_add(1, std::memory_order_relaxed);
                                co_return;
                            });
                        if (response.status_code() ==
                            cnetmod::http::status::too_many_requests)
                            rejected.fetch_add(1, std::memory_order_relaxed);
                    }
                    io->stop();
                };
                cnetmod::spawn(*io, work());
                start.arrive_and_wait();
                io->run();
            });
    }
    workers.clear();

    ASSERT_EQ(admitted.load(), burst);
    ASSERT_EQ(rejected.load(),
        loop_count * requests_per_loop - burst);
}

TEST(rate_limiter_ignores_spoofed_forwarding_headers_and_uses_the_envelope)
{
    int limited_calls = 0;
    std::chrono::seconds observed{};
    auto limiter = cnetmod::rate_limiter({
        .rate = 0.000001,
        .burst = 1.0,
        .on_limited = [&](cnetmod::http::request_context& context,
                          std::chrono::seconds retry_after)
        {
            ++limited_calls;
            observed = retry_after;
            context.json(cnetmod::http::status::too_many_requests, R"({"custom":true})");
        },
    });
    auto io = cnetmod::make_io_context();
    cnetmod::socket peer;
    int admitted = 0;
    for (const auto* spoofed : {"1.1.1.1", "2.2.2.2"})
    {
        cnetmod::http::response response;
        cnetmod::http::header_map headers{{"X-Forwarded-For", spoofed}};
        cnetmod::http::request_context context{*io, peer, "GET", "/limited",
            headers, {}, response, {}};
        cnetmod::sync_wait(limiter(context,
            [&]() -> cnetmod::task<void>
            {
                ++admitted;
                co_return;
            }));
        if (response.status_code() == cnetmod::http::status::too_many_requests)
        {
            ASSERT_EQ(std::string{response.body()}, std::string{R"({"custom":true})"});
            ASSERT_FALSE(response.get_header("Retry-After").empty());
        }
    }
    // A different spoofed X-Forwarded-For must not buy a fresh bucket.
    ASSERT_EQ(admitted, 1);
    ASSERT_EQ(limited_calls, 1);
    ASSERT_TRUE(observed >= std::chrono::seconds{1});
}

RUN_TESTS()
