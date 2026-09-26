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

RUN_TESTS()
