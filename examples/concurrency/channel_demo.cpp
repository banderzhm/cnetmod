/// cnetmod example — Channel Demo
/// 演示协程 channel<T> 的 producer/consumer 模型

import std;
import cnetmod.coro.task;
import cnetmod.coro.channel;

using namespace cnetmod;

auto producer(channel<int>& ch, int count) -> task<void> {
    for (int i = 0; i < count; ++i) {
        std::println("  [producer] send {}", i);
        co_await ch.send(i);
    }
    ch.close();
    std::println("  [producer] closed");
}

auto consumer(channel<int>& ch) -> task<void> {
    int sum = 0;
    while (true) {
        auto val = co_await ch.receive();
        if (!val) break;  // channel closed
        std::println("  [consumer] recv {}", *val);
        sum += *val;
    }
    std::println("  [consumer] done, sum = {}", sum);
}

auto run_demo() -> task<void> {
    channel<int> ch(2);  // capacity = 2

    // 直接 co_await 两个子任务（channel 内部互相唤醒，无需 io_context）
    co_await when_all(
        producer(ch, 5),
        consumer(ch)
    );
}

auto main() -> int {
    std::println("=== cnetmod: Channel Demo ===");
    sync_wait(run_demo());
    std::println("Done.");
    return 0;
}
