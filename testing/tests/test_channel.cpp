/// cnetmod unit tests — channel<T> send/receive, bounded capacity, close

#include "test_framework.hpp"

import cnetmod.coro.task;
import cnetmod.coro.channel;

using namespace cnetmod;

// =============================================================================
// Helper: run two coroutines that communicate via channel using when_all
// =============================================================================

static auto send_values(channel<int>& ch, int count) -> task<void> {
    for (int i = 0; i < count; ++i) {
        co_await ch.send(i);
    }
    ch.close();
}

static auto recv_sum(channel<int>& ch) -> task<int> {
    int sum = 0;
    while (true) {
        auto val = co_await ch.receive();
        if (!val) break;
        sum += *val;
    }
    co_return sum;
}

// =============================================================================
// Tests
// =============================================================================

TEST(channel_single_send_recv) {
    // Test basic send then receive with capacity 1
    channel<int> ch(1);

    auto producer = [&]() -> task<void> {
        co_await ch.send(42);
        ch.close();
    };

    auto consumer = [&]() -> task<int> {
        auto val = co_await ch.receive();
        ASSERT_TRUE(val.has_value());
        co_return *val;
    };

    // Producer first (buffered, won't block), then consumer
    sync_wait(producer());
    auto result = sync_wait(consumer());
    ASSERT_EQ(result, 42);
}

TEST(channel_buffered_multiple) {
    channel<int> ch(4);

    // Fill buffer (won't block because capacity >= count)
    auto fill = [&]() -> task<void> {
        co_await ch.send(1);
        co_await ch.send(2);
        co_await ch.send(3);
    };
    sync_wait(fill());

    // Drain
    auto drain = [&]() -> task<void> {
        auto v1 = co_await ch.receive();
        ASSERT_TRUE(v1.has_value());
        ASSERT_EQ(*v1, 1);

        auto v2 = co_await ch.receive();
        ASSERT_TRUE(v2.has_value());
        ASSERT_EQ(*v2, 2);

        auto v3 = co_await ch.receive();
        ASSERT_TRUE(v3.has_value());
        ASSERT_EQ(*v3, 3);
    };
    sync_wait(drain());
}

TEST(channel_close_returns_nullopt) {
    channel<int> ch(1);
    ch.close();

    auto recv = [&]() -> task<void> {
        auto val = co_await ch.receive();
        ASSERT_FALSE(val.has_value());
    };
    sync_wait(recv());
}

TEST(channel_is_closed) {
    channel<int> ch(1);
    ASSERT_FALSE(ch.is_closed());
    ch.close();
    ASSERT_TRUE(ch.is_closed());
}

TEST(channel_close_idempotent) {
    channel<int> ch(1);
    ch.close();
    ch.close();  // Should not crash
    ASSERT_TRUE(ch.is_closed());
}

TEST(channel_string_type) {
    channel<std::string> ch(2);

    auto send = [&]() -> task<void> {
        co_await ch.send(std::string("hello"));
        co_await ch.send(std::string("world"));
        ch.close();
    };
    sync_wait(send());

    auto recv = [&]() -> task<void> {
        auto v1 = co_await ch.receive();
        ASSERT_TRUE(v1.has_value());
        ASSERT_EQ(*v1, std::string("hello"));

        auto v2 = co_await ch.receive();
        ASSERT_TRUE(v2.has_value());
        ASSERT_EQ(*v2, std::string("world"));

        auto v3 = co_await ch.receive();
        ASSERT_FALSE(v3.has_value());
    };
    sync_wait(recv());
}

// =============================================================================
// Extended Tests
// =============================================================================

TEST(channel_close_with_buffered_data) {
    // Data in buffer should still be readable after close
    channel<int> ch(4);

    auto fill = [&]() -> task<void> {
        co_await ch.send(10);
        co_await ch.send(20);
        co_await ch.send(30);
        ch.close();
    };
    sync_wait(fill());

    auto drain = [&]() -> task<void> {
        auto v1 = co_await ch.receive();
        ASSERT_TRUE(v1.has_value());
        ASSERT_EQ(*v1, 10);

        auto v2 = co_await ch.receive();
        ASSERT_TRUE(v2.has_value());
        ASSERT_EQ(*v2, 20);

        auto v3 = co_await ch.receive();
        ASSERT_TRUE(v3.has_value());
        ASSERT_EQ(*v3, 30);

        // Now empty and closed
        auto v4 = co_await ch.receive();
        ASSERT_FALSE(v4.has_value());
    };
    sync_wait(drain());
}

TEST(channel_large_capacity) {
    channel<int> ch(1024);

    auto fill = [&]() -> task<void> {
        for (int i = 0; i < 1024; ++i)
            co_await ch.send(i);
    };
    sync_wait(fill());

    auto drain = [&]() -> task<int> {
        int sum = 0;
        for (int i = 0; i < 1024; ++i) {
            auto v = co_await ch.receive();
            ASSERT_TRUE(v.has_value());
            sum += *v;
        }
        co_return sum;
    };
    auto sum = sync_wait(drain());
    // Sum of 0..1023 = 1023*1024/2 = 523776
    ASSERT_EQ(sum, 523776);
}

TEST(channel_concurrent_producer_consumer) {
    channel<int> ch(8);
    constexpr int COUNT = 100;

    auto prod = [&]() -> task<void> {
        for (int i = 0; i < COUNT; ++i)
            co_await ch.send(i);
        ch.close();
    };

    auto cons = [&]() -> task<int> {
        int sum = 0;
        while (true) {
            auto v = co_await ch.receive();
            if (!v) break;
            sum += *v;
        }
        co_return sum;
    };

    auto [p, sum] = sync_wait(when_all(prod(), cons()));
    // Sum of 0..99 = 4950
    ASSERT_EQ(sum, 4950);
}

TEST(channel_move_only_type) {
    channel<std::unique_ptr<int>> ch(2);

    auto send = [&]() -> task<void> {
        co_await ch.send(std::make_unique<int>(42));
        ch.close();
    };
    sync_wait(send());

    auto recv = [&]() -> task<void> {
        auto val = co_await ch.receive();
        ASSERT_TRUE(val.has_value());
        ASSERT_TRUE(*val != nullptr);
        ASSERT_EQ(**val, 42);
    };
    sync_wait(recv());
}

RUN_TESTS()
