#include "test_framework.hpp"
#include <cnetmod/orm.hpp>

import std;
import cnetmod.orm;
import cnetmod.coro.task;

namespace orm = cnetmod::orm;

struct scatter_recording_client
{
    auto query(std::string_view) -> cnetmod::task<orm::query_result>
    {
        co_return orm::query_result{};
    }

    auto execute(std::string_view) -> cnetmod::task<orm::query_result>
    {
        co_return orm::query_result{};
    }

    auto execute(orm::parameterized_query)
        -> cnetmod::task<orm::query_result>
    {
        co_return orm::query_result{};
    }
};

struct scatter_recording_lease
{
    scatter_recording_client* client{};
};

auto make_scatter_gateway(bool scatter_enabled = true,
    bool distributed_transactions_enabled = true)
    -> orm::sharded_session_gateway<scatter_recording_client,
        scatter_recording_lease>
{
    auto catalog = std::make_shared<orm::shard_catalog>();
    (void)catalog->add_database("orders-primary");
    (void)catalog->freeze("orders", 3,
        std::make_shared<orm::hash_shard_strategy>());
    static scatter_recording_client client;
    return orm::sharded_session_gateway<scatter_recording_client,
        scatter_recording_lease>{catalog, orm::sql_dialect::mysql,
        [](std::string_view)
            -> cnetmod::task<std::expected<scatter_recording_lease, std::string>>
        {
            co_return scatter_recording_lease{&client};
        },
        [](scatter_recording_lease& lease) -> scatter_recording_client&
        {
            return *lease.client;
        },
        scatter_enabled, distributed_transactions_enabled};
}

TEST(orm_scatter_gather_reports_every_shard_outcome)
{
    auto gateway = make_scatter_gateway();
    auto result = cnetmod::sync_wait(gateway.scatter_read<std::string>(
        [](const orm::shard_route& route, auto&)
            -> cnetmod::task<std::expected<std::string, std::string>>
        {
            if (route.table_shard == 1)
                co_return std::unexpected("shard unavailable");
            co_return route.physical_table;
        }));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 3U);
    ASSERT_TRUE((*result)[0].result.has_value());
    ASSERT_FALSE((*result)[1].result.has_value());
    ASSERT_TRUE((*result)[2].result.has_value());
    const auto merged = cnetmod::sync_wait(
        gateway.scatter_gather<std::size_t, std::size_t>(
            [](const orm::shard_route&, auto&)
                -> cnetmod::task<std::expected<std::size_t, std::string>>
            {
                co_return 1U;
            },
            [](std::vector<orm::shard_read_result<std::size_t>> values)
                -> std::expected<std::size_t, std::string>
            {
                std::size_t total{};
                for (const auto& value : values)
                {
                    if (!value.result)
                        return std::unexpected(value.result.error());
                    total += *value.result;
                }
                return total;
            }));
    ASSERT_TRUE(merged.has_value());
    ASSERT_EQ(*merged, 3U);
}

TEST(orm_scatter_read_converts_callback_exceptions_to_shard_failures)
{
    auto gateway = make_scatter_gateway();
    const auto result = cnetmod::sync_wait(gateway.scatter_read<int>(
        [](const orm::shard_route&, auto&)
            -> cnetmod::task<std::expected<int, std::string>>
        {
            throw std::runtime_error{"scatter callback failed"};
            co_return 0;
        }));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 3U);
    for (const auto& shard : *result)
    {
        ASSERT_FALSE(shard.result.has_value());
        ASSERT_EQ(shard.result.error(), "scatter callback failed");
    }
}

TEST(orm_sharding_capabilities_can_be_disabled_independently)
{
    auto gateway = make_scatter_gateway(false, false);
    const auto scattered = cnetmod::sync_wait(gateway.scatter_read<int>(
        [](const orm::shard_route&, auto&)
            -> cnetmod::task<std::expected<int, std::string>>
        {
            co_return 1;
        }));
    ASSERT_FALSE(scattered.has_value());

    const std::array keys{orm::shard_key{1ULL}};
    const auto transaction = cnetmod::sync_wait(
        gateway.distributed_transaction<int>(keys,
            [](auto&) -> cnetmod::task<std::expected<int, std::string>>
            {
                co_return 1;
            }));
    ASSERT_FALSE(transaction.has_value());
}

RUN_TESTS()
