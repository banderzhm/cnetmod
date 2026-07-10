/// cnetmod example — Async Mutex Demo
/// Demonstrates async_mutex protectshared

import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;

using namespace cnetmod;

struct shared_state {
    async_mutex mtx;
    int counter = 0;
};

auto increment(shared_state& state, int id, int times) -> task<void> {
    for (int i = 0; i < times; ++i) {
        co_await state.mtx.lock();
        async_lock_guard guard(state.mtx, std::adopt_lock);

        int prev = state.counter;
        state.counter += 1;
        std::println("  [coro-{}] {} -> {}", id, prev, state.counter);
    }
}

auto run_demo() -> task<void> {
    shared_state state;

    constexpr int N = 3;   // Coroutine flow.
    constexpr int M = 3;   // Coroutine flow.

    // Mutex , does not require io_context
    co_await increment(state, 0, M);
    co_await increment(state, 1, M);
    co_await increment(state, 2, M);

    std::println("  Final counter = {} (expected {})", state.counter, N * M);
}

auto main() -> int {
    std::println("=== cnetmod: Async Mutex Demo ===");
    sync_wait(run_demo());
    std::println("Done.");
    return 0;
}
