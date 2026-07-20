/// cnetmod benchmark — Synchronization primitives
/// Compare with: std::mutex (baseline), folly::coro::Mutex, Rust tokio::sync::Mutex

import std;
import cnetmod.core.log;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import cnetmod.coro.semaphore;
import cnetmod.coro.circuit_breaker;

#include "bench_framework.hpp"

using namespace cnetmod;
using namespace cnetmod::bench;

// =============================================================================
// Benchmarks
// =============================================================================

BENCH_CAT("Synchronization", async_mutex_lock_unlock, 2'000'000) {
    async_mutex mtx;
    for (std::size_t i = 0; i < _bench_iters_; ++i) {
        auto t = [&]() -> task<void> {
            co_await mtx.lock();
            mtx.unlock();
        };
        sync_wait(t());
    }
}

BENCH_CAT("Synchronization", async_mutex_try_lock, 5'000'000) {
    async_mutex mtx;
    for (std::size_t i = 0; i < _bench_iters_; ++i) {
        bool ok = mtx.try_lock();
        if (ok) mtx.unlock();
        do_not_optimize(ok);
    }
}

BENCH_CAT("Synchronization", std_mutex_baseline, 5'000'000) {
    std::mutex mtx;
    for (std::size_t i = 0; i < _bench_iters_; ++i) {
        mtx.lock();
        mtx.unlock();
    }
}

BENCH_CAT("Synchronization", semaphore_acquire_release, 2'000'000) {
    async_semaphore sem(1);
    for (std::size_t i = 0; i < _bench_iters_; ++i) {
        auto t = [&]() -> task<void> {
            co_await sem.acquire();
            sem.release();
        };
        sync_wait(t());
    }
}

BENCH_CAT("Synchronization", semaphore_try_acquire, 5'000'000) {
    async_semaphore sem(1);
    for (std::size_t i = 0; i < _bench_iters_; ++i) {
        bool ok = sem.try_acquire();
        if (ok) sem.release();
        do_not_optimize(ok);
    }
}

BENCH_CAT("Synchronization", circuit_breaker_execute, 1'000'000) {
    circuit_breaker cb;
    auto success = []() -> task<std::expected<int, std::error_code>> {
        co_return 42;
    };
    for (std::size_t i = 0; i < _bench_iters_; ++i) {
        auto r = sync_wait(cb.execute_ec<int>(success));
        do_not_optimize(r);
    }
}

RUN_BENCHMARKS()
