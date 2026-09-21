#include "test_framework.hpp"
#include <cnetmod/orm.hpp>

import std;
import cnetmod.orm;
import cnetmod.coro.task;

namespace orm = cnetmod::orm;

struct gateway_order
{
    std::int64_t id{};
    std::string description;
};

CNETMOD_MODEL(gateway_order, "orders",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(description, "description", varchar))

struct gateway_recording_client
{
    std::string last_sql;

    auto query(std::string_view sql) -> cnetmod::task<orm::query_result>
    {
        last_sql = sql;
        co_return orm::query_result{};
    }

    auto execute(std::string_view sql) -> cnetmod::task<orm::query_result>
    {
        last_sql = sql;
        co_return orm::query_result{};
    }

    auto execute(orm::parameterized_query statement)
        -> cnetmod::task<orm::query_result>
    {
        last_sql = std::move(statement.query);
        co_return orm::query_result{};
    }
};

struct gateway_recording_lease
{
    gateway_recording_client* client{};
};

TEST(orm_sharded_gateway_pins_named_instance_and_table)
{
    auto catalog = std::make_shared<orm::shard_catalog>();
    ASSERT_TRUE(catalog->add_database("orders-primary").has_value());
    ASSERT_TRUE(catalog->freeze("orders", 16,
                           std::make_shared<orm::hash_shard_strategy>())
            .has_value());

    gateway_recording_client client;
    std::string acquired_instance;
    orm::sharded_session_gateway<gateway_recording_client,
        gateway_recording_lease>
        gateway{catalog, orm::sql_dialect::mysql,
            [&](std::string_view instance)
                -> cnetmod::task<std::expected<gateway_recording_lease,
                    std::string>>
            {
                acquired_instance = instance;
                co_return gateway_recording_lease{&client};
            },
            [](gateway_recording_lease& lease) -> gateway_recording_client&
            {
                return *lease.client;
            }};

    const auto destination = catalog->route(orm::shard_key{"tenant-7"});
    ASSERT_TRUE(destination.has_value());
    if (!destination)
        return;

    const auto read = cnetmod::sync_wait(gateway.read<int>(
        orm::shard_key{"tenant-7"},
        [](auto& session) -> cnetmod::task<std::expected<int, std::string>>
        {
            using session_type = std::remove_reference_t<decltype(session)>;
            orm::mapper<gateway_order, session_type> orders{session};
            orm::query_wrapper<gateway_order> query;
            query.eq("id", 7);
            auto result = co_await orders.select_list(query);
            if (result.is_err())
                co_return std::unexpected(result.error_msg);
            co_return 7;
        }));
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(acquired_instance, destination->instance);
    ASSERT_TRUE(client.last_sql.contains(
        std::format("`{}`", destination->physical_table)));

    const auto write = cnetmod::sync_wait(gateway.write<int>(
        orm::shard_key{"tenant-7"},
        [](auto& session) -> cnetmod::task<std::expected<int, std::string>>
        {
            using session_type = std::remove_reference_t<decltype(session)>;
            orm::mapper<gateway_order, session_type> orders{session};
            gateway_order order{7, "transaction"};
            auto result = co_await orders.update_by_id(order);
            if (result.is_err())
                co_return std::unexpected(result.error_msg);
            co_return 1;
        }));
    ASSERT_TRUE(write.has_value());
    ASSERT_EQ(client.last_sql, "COMMIT");
}

RUN_TESTS()
