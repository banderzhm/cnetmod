/// cnetmod example — Native Task Composition Demo
/// Demonstrates synchronous task entry points without exposing a sender API.

import std;
import cnetmod.coro.task;

using namespace cnetmod;

auto compute(int x, int y) -> task<int>
{
    co_return x* y + 1;
}

auto greet() -> task<std::string>
{
    co_return std::string{"hello from task"};
}

auto main() -> int
{
    std::println("=== cnetmod: Native Task Composition Demo ===");

    auto a = sync_wait(compute(2, 5));
    std::println("  compute(2,5) = {}", a);

    // chain
    auto b = sync_wait(compute(a, 3));
    std::println("  compute(a,3) = {}", b);

    // string
    auto s = sync_wait(greet());
    std::println("  greet() = {}", s);

    std::println("Done.");
    return 0;
}
