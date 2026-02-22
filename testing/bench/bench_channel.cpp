/// cnetmod benchmark — Channel throughput
/// Compare with: Go channels, Rust tokio::mpsc, crossbeam-channel

import std;
import cnetmod.core.log;
import cnetmod.coro.task;
import cnetmod.coro.channel;

#include "bench_framework.hpp"

using namespace cnetmod;
using namespace cnetmod::bench;

// =============================================================================
// Helpers: producer/consumer driven by when_all
// =============================================================================

static auto producer(channel<int>& ch, std::size_t count) -> task<void> {
    for (std::size_t i = 0; i < count; ++i) {
        co_await ch.send(static_cast<int>(i));
    }
    ch.close();
}

static auto consumer(channel<int>& ch) -> task<std::size_t> {
    std::size_t count = 0;
    while (true) {
        auto val = co_await ch.receive();
        if (!val) break;
        ++count;
    }
    co_return count;
}

// =============================================================================
// Benchmarks
// =============================================================================

BENCH_CAT("Channel", buffered_1_pingpong, 500'000) {
    // Simulates synchronous handoff through channel(1)
    for (std::size_t i = 0; i < _bench_iters_; ++i) {
        channel<int> ch(1);

        auto send_one = [&]() -> task<void> {
            co_await ch.send(42);
            ch.close();
        };
        auto recv_one = [&]() -> task<int> {
            auto v = co_await ch.receive();
            co_return v.value_or(0);
        };

        sync_wait(send_one());
        auto r = sync_wait(recv_one());
        do_not_optimize(r);
    }
}

BENCH_CAT("Channel", buffered_64_throughput, 100) {
    // Measure throughput: 100K messages per iteration through channel(64)
    constexpr std::size_t msgs = 100'000;
    for (std::size_t i = 0; i < _bench_iters_; ++i) {
        channel<int> ch(64);
        // when_all(task<void>, task<size_t>) returns task<size_t>
        auto c = sync_wait(when_all(producer(ch, msgs), consumer(ch)));
        do_not_optimize(c);
    }
}

BENCH_CAT("Channel", buffered_1024_throughput, 100) {
    // Measure throughput: 100K messages through channel(1024)
    constexpr std::size_t msgs = 100'000;
    for (std::size_t i = 0; i < _bench_iters_; ++i) {
        channel<int> ch(1024);
        auto c = sync_wait(when_all(producer(ch, msgs), consumer(ch)));
        do_not_optimize(c);
    }
}

RUN_BENCHMARKS()
