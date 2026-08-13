#include "test_framework.hpp"

import std;
import cnetmod.core.buffer_pool;
import cnetmod.executor.async_op;
import cnetmod.utils;

TEST(concurrent_queue_transfers_all_values)
{
    cnetmod::concurrent_containers::bounded_mpmc_queue<int> queue{1024};
    constexpr int count = 1000;
    std::jthread producer{[&]
        {
            for (int index{}; index < count;)
                if (queue.try_enqueue(index))
                    ++index;
        }};
    int expected{};
    while (expected < count)
    {
        if (auto value = queue.try_dequeue())
            ASSERT_EQ(*value, expected++);
    }
    producer.join();
}

TEST(atomic_hash_map_and_copy_on_write_publish_consistent_values)
{
    cnetmod::concurrent_containers::atomic_hash_map<std::string, int> values{32};
    ASSERT_TRUE(values.try_emplace("one", 1));
    values.insert_or_assign("one", 2);
    ASSERT_EQ(*values.find("one"), 2);
    ASSERT_TRUE(values.erase("one"));
    ASSERT_FALSE(values.contains("one"));

    cnetmod::concurrent_containers::copy_on_write<std::vector<int>> snapshot{{1}};
    snapshot.update([](auto& current)
        {
            current.push_back(2);
        });
    const auto stable = snapshot.read();
    ASSERT_EQ(stable->size(), 2U);
    ASSERT_EQ((*stable)[1], 2);
}

TEST(atomic_hash_map_bounded_insert_and_cas_eviction)
{
    cnetmod::concurrent_containers::atomic_hash_map<int, int> values{1};
    ASSERT_TRUE(values.try_emplace_bounded(1, 10, 1));
    ASSERT_FALSE(values.try_emplace_bounded(2, 20, 1));
    ASSERT_TRUE(values.erase_min_by([](int, int left, int, int right)
        {
            return left < right;
        }));
    ASSERT_EQ(values.size(), std::size_t{0});
    ASSERT_TRUE(values.try_emplace_bounded(2, 20, 1));
    ASSERT_EQ(*values.find(2), 20);
    ASSERT_EQ(values.erase_if([](int, int value)
                  {
                      return value == 20;
                  }),
        std::size_t{1});
    ASSERT_EQ(values.size(), std::size_t{0});
}

TEST(concurrent_vector_skip_list_and_accumulator)
{
    cnetmod::concurrent_containers::concurrent_vector<int> vector;
    vector.push_back(7);
    ASSERT_EQ(*vector.try_get(0), 7);
    cnetmod::concurrent_containers::concurrent_skip_list<int, std::string> list;
    ASSERT_TRUE(list.insert_or_assign(2, "two"));
    ASSERT_EQ(*list.find(2), "two");
    cnetmod::concurrent_containers::striped_accumulator<std::uint64_t> counter{4};
    std::array<std::jthread, 4> workers{std::jthread{[&]
                                            {
                                                for (int i{}; i < 1000; ++i)
                                                    counter.add(1);
                                            }},
        std::jthread{[&]
            {
                for (int i{}; i < 1000; ++i)
                    counter.add(1);
            }},
        std::jthread{[&]
            {
                for (int i{}; i < 1000; ++i)
                    counter.add(1);
            }},
        std::jthread{[&]
            {
                for (int i{}; i < 1000; ++i)
                    counter.add(1);
            }}};
    for (auto& worker : workers)
        worker.join();
    ASSERT_EQ(counter.value(), 4000U);
}

TEST(slot_latch_and_striped_maps_preserve_key_value_semantics)
{
    cnetmod::concurrent_containers::slot_latch_hash_map<int, std::string> slots{32};
    ASSERT_TRUE(slots.try_emplace(1, "one"));
    ASSERT_FALSE(slots.try_emplace(1, "replacement"));
    slots.insert_or_assign(1, "updated");
    ASSERT_EQ(*slots.find(1), "updated");
    ASSERT_TRUE(slots.erase(1));
    ASSERT_FALSE(slots.contains(1));
    ASSERT_TRUE(slots.try_emplace(1, "reused-tombstone"));
    ASSERT_EQ(*slots.find(1), "reused-tombstone");

    cnetmod::concurrent_containers::striped_hash_map<int, int> stripes{16, 4};
    std::array<std::jthread, 4> writers{
        std::jthread{[&]
            {
                for (int key{}; key < 100; ++key)
                    stripes.insert_or_assign(key, key);
            }},
        std::jthread{[&]
            {
                for (int key{100}; key < 200; ++key)
                    stripes.insert_or_assign(key, key);
            }},
        std::jthread{[&]
            {
                for (int key{200}; key < 300; ++key)
                    stripes.insert_or_assign(key, key);
            }},
        std::jthread{[&]
            {
                for (int key{300}; key < 400; ++key)
                    stripes.insert_or_assign(key, key);
            }}};
    for (auto& writer : writers)
        writer.join();
    ASSERT_EQ(stripes.size(), 400U);
    ASSERT_EQ(*stripes.find(357), 357);
    const auto first_update = stripes.update_or_emplace(900, 4,
        [](int& value, bool inserted)
        {
            value += inserted ? 1 : 10;
            return value;
        });
    ASSERT_EQ(first_update, 5);
    const auto second_update = stripes.update_or_emplace(900, 0,
        [](int& value, bool inserted)
        {
            ASSERT_FALSE(inserted);
            value += 10;
            return value;
        });
    ASSERT_EQ(second_update, 15);
    ASSERT_EQ(stripes.erase_if([](int key, int)
                  {
                      return key == 900;
                  }),
        std::size_t{1});
    ASSERT_FALSE(stripes.contains(900));
}

TEST(atomic_latch_containers_survive_parallel_read_write_pressure)
{
    constexpr std::size_t writer_count = 6U;
    constexpr std::size_t values_per_writer = 1500U;
    cnetmod::concurrent_containers::concurrent_vector<std::size_t> values;
    cnetmod::concurrent_containers::striped_hash_map<std::size_t, std::size_t> map{
        writer_count * values_per_writer, writer_count};
    std::atomic<bool> start{};
    std::atomic<bool> writers_done{};
    std::atomic<bool> valid{true};
    std::array<std::jthread, writer_count> writers;
    std::array<std::jthread, writer_count> readers;

    for (std::size_t worker{}; worker < writer_count; ++worker)
    {
        writers[worker] = std::jthread{[&, worker]
            {
                start.wait(false, std::memory_order_acquire);
                for (std::size_t offset{}; offset < values_per_writer; ++offset)
                {
                    const auto key = worker * values_per_writer + offset;
                    values.push_back(key);
                    map.insert_or_assign(key, key ^ 0x5a5aU);
                }
            }};
        readers[worker] = std::jthread{[&, worker]
            {
                start.wait(false, std::memory_order_acquire);
                while (!writers_done.load(std::memory_order_acquire))
                {
                    const auto key = (worker * 131U) % (writer_count * values_per_writer);
                    if (const auto found = map.find(key); found && *found != (key ^ 0x5a5aU))
                        valid.store(false, std::memory_order_release);
                    const auto count = values.size();
                    if (count != 0U && !values.try_get(count - 1U))
                        valid.store(false, std::memory_order_release);
                }
            }};
    }
    start.store(true, std::memory_order_release);
    start.notify_all();
    for (auto& writer : writers)
        writer.join();
    writers_done.store(true, std::memory_order_release);
    for (auto& reader : readers)
        reader.join();

    ASSERT_TRUE(valid.load(std::memory_order_acquire));
    ASSERT_EQ(values.size(), writer_count * values_per_writer);
    ASSERT_EQ(map.size(), writer_count * values_per_writer);
    ASSERT_EQ(*map.find(777U), 777U ^ 0x5a5aU);
}

TEST(buffer_pool_cross_worker_leases_are_aba_safe)
{
    cnetmod::buffer_pool pool{256, 64};
    std::atomic<bool> valid{true};
    std::array<std::jthread, 8> workers;
    for (std::size_t worker{}; worker < workers.size(); ++worker)
    {
        workers[worker] = std::jthread{[&, worker]
            {
                for (std::size_t iteration{}; iteration < 20000U; ++iteration)
                {
                    auto lease = pool.acquire();
                    if (!lease.valid() || lease.size() != 256U)
                    {
                        valid.store(false, std::memory_order_relaxed);
                        return;
                    }
                    auto* data = static_cast<std::byte*>(lease.data());
                    data[0] = static_cast<std::byte>(worker);
                    data[lease.size() - 1U] = static_cast<std::byte>(iteration);
                    if (data[0] != static_cast<std::byte>(worker))
                    {
                        valid.store(false, std::memory_order_relaxed);
                        return;
                    }
                }
            }};
    }
    for (auto& worker : workers)
        worker.join();
    ASSERT_TRUE(valid.load(std::memory_order_relaxed));
    ASSERT_TRUE(pool.pool_size() <= 64U);
}

TEST(udp_datagram_buffer_transfers_pooled_or_fallback_storage)
{
    cnetmod::udp_datagram_buffer pooled{65527U};
    ASSERT_TRUE(pooled.is_pooled());
    ASSERT_EQ(pooled.size(), 65527U);
    pooled.resize(1200U);
    pooled.front() = std::byte{0x42};
    const std::span<const std::byte> pooled_view = pooled;
    ASSERT_EQ(pooled_view.size(), 1200U);
    ASSERT_TRUE(pooled_view.front() == std::byte{0x42});

    cnetmod::udp_datagram_buffer fallback{65537U};
    ASSERT_FALSE(fallback.is_pooled());
    fallback.resize(128U);
    ASSERT_EQ(fallback.size(), 128U);
}

RUN_TESTS()
