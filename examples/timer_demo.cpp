/// cnetmod example — Timer Demo
/// 演示 steady_timer (低精度) 和 high_resolution_timer (高精度)

import std;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.io.io_context;
import cnetmod.coro.timer;

using namespace cnetmod;

auto run_timer(io_context& ctx) -> task<void> {
    steady_timer low(ctx);
    high_resolution_timer high(ctx);

    std::println("  [low] waiting 300ms...");
    (void)co_await low.async_wait(std::chrono::milliseconds{300});
    std::println("  [low] done");

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{150};
    std::println("  [high] waiting until now+150ms...");
    (void)co_await high.async_wait_until(deadline);
    std::println("  [high] done");

    ctx.stop();
}

auto main() -> int {
    std::println("=== cnetmod: Timer Demo ===");

    auto ctx = make_io_context();
    spawn(*ctx, run_timer(*ctx));
    ctx->run();

    std::println("Done.");
    return 0;
}
