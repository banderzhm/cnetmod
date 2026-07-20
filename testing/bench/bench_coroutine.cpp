/// cnetmod benchmark — Coroutine primitives overhead
/// Compare with: Boost.Asio coroutine, cppcoro, libunifex, folly::coro

import std;
import cnetmod.core.log;
import cnetmod.coro.task;

#include "bench_framework.hpp"

using namespace cnetmod;
using namespace cnetmod::bench;

// =============================================================================
// Helper coroutines
// =============================================================================

static auto return_int(int v) -> task<int> { co_return v; }
static auto return_void() -> task<void> { co_return; }

template <int Depth>
static auto nested_await() -> task<int> {
    if constexpr (Depth <= 0)
        co_return 1;
    else
        co_return co_await nested_await<Depth - 1>() + 1;
}

// =============================================================================
// Benchmarks
// =============================================================================

BENCH_CAT("Coroutine Primitives", task_int_sync_wait, 2'000'000) {
    for (std::size_t i = 0; i < _bench_iters_; ++i) {
        auto r = sync_wait(return_int(42));
        do_not_optimize(r);
    }
}

BENCH_CAT("Coroutine Primitives", task_void_sync_wait, 2'000'000) {
    for (std::size_t i = 0; i < _bench_iters_; ++i) {
        sync_wait(return_void());
    }
}

BENCH_CAT("Coroutine Primitives", when_all_2, 1'000'000) {
    for (std::size_t i = 0; i < _bench_iters_; ++i) {
        auto [a, b] = sync_wait(when_all(return_int(1), return_int(2)));
        do_not_optimize(a);
        do_not_optimize(b);
    }
}

BENCH_CAT("Coroutine Primitives", when_all_4, 500'000) {
    for (std::size_t i = 0; i < _bench_iters_; ++i) {
        auto [a, b, c, d] = sync_wait(when_all(
            return_int(1), return_int(2),
            return_int(3), return_int(4)));
        do_not_optimize(a);
    }
}

BENCH_CAT("Coroutine Primitives", when_all_8, 200'000) {
    for (std::size_t i = 0; i < _bench_iters_; ++i) {
        auto [a, b, c, d, e, f, g, h] = sync_wait(when_all(
            return_int(1), return_int(2), return_int(3), return_int(4),
            return_int(5), return_int(6), return_int(7), return_int(8)));
        do_not_optimize(a);
    }
}

BENCH_CAT("Coroutine Primitives", nested_co_await_10, 1'000'000) {
    for (std::size_t i = 0; i < _bench_iters_; ++i) {
        auto r = sync_wait(nested_await<10>());
        do_not_optimize(r);
    }
}

BENCH_CAT("Coroutine Primitives", nested_co_await_100, 200'000) {
    for (std::size_t i = 0; i < _bench_iters_; ++i) {
        auto r = sync_wait(nested_await<100>());
        do_not_optimize(r);
    }
}

RUN_BENCHMARKS()
