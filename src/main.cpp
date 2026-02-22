#include <cnetmod/version.hpp>
#include <cnetmod/config.hpp>

import std;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.io.io_context;
import cnetmod.executor.scheduler;
// import cnetmod.core.log;  // TODO: temporarily disabled to check compilation issues

// =============================================================================
// Demo: coroutine chain + spawn + stdexec bridge (smoke test)
// =============================================================================

auto echo(std::string msg) -> cnetmod::task<std::string> {
    co_return "echo: " + msg;
}

auto demo_chain() -> cnetmod::task<void> {
    auto r = co_await echo("Hello, cnetmod!");
    std::println("  {}", r);
}

auto compute(int x, int y) -> cnetmod::task<int> {
    co_return x * y + 1;
}

auto demo_spawn(cnetmod::io_context& ctx) -> cnetmod::task<void> {
    std::println("  spawn task running on event loop!");
    ctx.stop();
    co_return;
}

auto main() -> int {
    std::println("cnetmod v{}", CNETMOD_VERSION_STRING);

    // 1. coroutine chain
    std::println("\n=== Coroutine Chain ===");
    cnetmod::sync_wait(demo_chain());

    // 2. stdexec sender bridge
    std::println("\n=== stdexec Sender ===");
    auto val = cnetmod::sync_wait_sender(cnetmod::as_sender(compute(6, 7)));
    std::println("  compute(6,7) = {}", val);

    // 3. io_context + spawn
    std::println("\n=== spawn + io_context ===");
    auto ctx = cnetmod::make_io_context();
    cnetmod::spawn(*ctx, demo_spawn(*ctx));
    ctx->run();
    std::println("  io_context + spawn OK");

    std::println("All OK. See examples/ for full demos.");
    return 0;
}
