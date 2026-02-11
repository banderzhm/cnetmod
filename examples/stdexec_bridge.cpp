/// cnetmod example — stdexec Bridge Demo
/// 演示 task<T> -> sender 的桥接与同步等待

import std;
import cnetmod.coro.task;
import cnetmod.executor.scheduler;

using namespace cnetmod;

auto compute(int x, int y) -> task<int> {
    co_return x * y + 1;
}

auto greet() -> task<std::string> {
    co_return std::string{"hello from task"};
}

auto main() -> int {
    std::println("=== cnetmod: stdexec Bridge Demo ===");

    // task<int> -> sender -> sync_wait
    auto a = sync_wait_sender(as_sender(compute(2, 5)));
    std::println("  compute(2,5) = {}", a);

    // chain
    auto b = sync_wait_sender(as_sender(compute(a, 3)));
    std::println("  compute(a,3) = {}", b);

    // string
    auto s = sync_wait_sender(as_sender(greet()));
    std::println("  greet() = {}", s);

    std::println("Done.");
    return 0;
}
