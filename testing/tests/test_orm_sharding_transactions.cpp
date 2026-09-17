#include "test_framework.hpp"
#include <cnetmod/orm.hpp>

import std;
import cnetmod.orm;
import cnetmod.coro.task;

namespace orm = cnetmod::orm;

struct transactional_order
{
    std::int64_t id{};
    std::string description;
};

CNETMOD_MODEL(transactional_order, "orders",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(description, "description", varchar))

struct transaction_recording_client
{
    std::vector<std::string> statements;

    auto query(std::string_view sql) -> cnetmod::task<orm::query_result>
    {
        statements.emplace_back(sql);
        co_return orm::query_result{};
    }

    auto execute(std::string_view sql) -> cnetmod::task<orm::query_result>
    {
        statements.emplace_back(sql);
        co_return orm::query_result{};
    }

    auto execute(orm::parameterized_query statement)
        -> cnetmod::task<orm::query_result>
    {
        statements.push_back(std::move(statement.query));
        co_return orm::query_result{};
    }
};

struct transaction_recording_lease
{
    transaction_recording_client* client{};
};

auto make_transaction_catalog() -> std::shared_ptr<orm::shard_catalog>
{
    auto catalog = std::make_shared<orm::shard_catalog>();
    if (!catalog->add_database("orders-0") ||
        !catalog->add_database("orders-1") ||
        !catalog->freeze("orders", 2,
            std::make_shared<orm::hash_shard_strategy>()))
        return {};
    return catalog;
}

auto keys_for_both_database_shards(const orm::shard_catalog& catalog)
    -> std::array<orm::shard_key, 2>
{
    std::array<std::optional<orm::shard_key>, 2> selected;
    for (std::uint64_t value = 1; value < 1000; ++value)
    {
        const auto route = catalog.route(orm::shard_key{value});
        if (route && !selected[route->database_shard])
            selected[route->database_shard] = orm::shard_key{value};
    }
    return {*selected[0], *selected[1]};
}

auto contains_statement(const transaction_recording_client& client,
    std::string_view prefix) -> bool
{
    return std::ranges::any_of(client.statements,
        [prefix](const std::string& sql)
        {
            return sql.starts_with(prefix);
        });
}

TEST(orm_distributed_transaction_uses_mysql_xa_two_phase_commit)
{
    auto catalog = make_transaction_catalog();
    ASSERT_TRUE(catalog != nullptr);
    if (!catalog)
        return;

    std::array<transaction_recording_client, 2> clients;
    orm::sharded_session_gateway<transaction_recording_client,
        transaction_recording_lease>
        gateway{catalog, orm::sql_dialect::mysql,
            [&](std::string_view instance)
                -> cnetmod::task<std::expected<transaction_recording_lease,
                    std::string>>
            {
                co_return transaction_recording_lease{
                    &clients[instance == "orders-0" ? 0U : 1U]};
            },
            [](transaction_recording_lease& lease)
                -> transaction_recording_client&
            {
                return *lease.client;
            }};

    const auto keys = keys_for_both_database_shards(*catalog);
    const auto result = cnetmod::sync_wait(
        gateway.distributed_transaction<int>(keys,
            [](auto& sessions)
                -> cnetmod::task<std::expected<int, std::string>>
            {
                ASSERT_EQ(sessions.entries().size(), 2U);
                for (auto& entry : sessions.entries())
                {
                    transactional_order order{
                        static_cast<std::int64_t>(entry.route.database_shard + 1),
                        "xa"};
                    const auto updated = co_await entry.session.update(order);
                    if (updated.is_err())
                        co_return std::unexpected(updated.error_msg);
                }
                co_return 2;
            }));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, 2);
    for (const auto& client : clients)
    {
        ASSERT_TRUE(contains_statement(client, "XA START"));
        ASSERT_TRUE(contains_statement(client, "XA PREPARE"));
        ASSERT_TRUE(contains_statement(client, "XA COMMIT"));
    }
}

TEST(orm_distributed_transaction_rolls_back_every_started_shard)
{
    auto catalog = make_transaction_catalog();
    ASSERT_TRUE(catalog != nullptr);
    if (!catalog)
        return;

    std::array<transaction_recording_client, 2> clients;
    orm::sharded_session_gateway<transaction_recording_client,
        transaction_recording_lease>
        gateway{catalog, orm::sql_dialect::mysql,
            [&](std::string_view instance)
                -> cnetmod::task<std::expected<transaction_recording_lease,
                    std::string>>
            {
                co_return transaction_recording_lease{
                    &clients[instance == "orders-0" ? 0U : 1U]};
            },
            [](transaction_recording_lease& lease)
                -> transaction_recording_client&
            {
                return *lease.client;
            }};

    const auto keys = keys_for_both_database_shards(*catalog);
    const auto result = cnetmod::sync_wait(
        gateway.distributed_transaction<int>(keys,
            [](auto&) -> cnetmod::task<std::expected<int, std::string>>
            {
                co_return std::unexpected("business rule rejected write");
            }));
    ASSERT_FALSE(result.has_value());
    for (const auto& client : clients)
    {
        ASSERT_TRUE(contains_statement(client, "XA ROLLBACK"));
        ASSERT_FALSE(contains_statement(client, "XA COMMIT"));
    }
}

TEST(orm_distributed_transaction_rolls_back_callback_exceptions)
{
    auto catalog = make_transaction_catalog();
    ASSERT_TRUE(catalog != nullptr);
    if (!catalog)
        return;

    std::array<transaction_recording_client, 2> clients;
    orm::sharded_session_gateway<transaction_recording_client,
        transaction_recording_lease>
        gateway{catalog, orm::sql_dialect::mysql,
            [&](std::string_view instance)
                -> cnetmod::task<std::expected<transaction_recording_lease,
                    std::string>>
            {
                co_return transaction_recording_lease{
                    &clients[instance == "orders-0" ? 0U : 1U]};
            },
            [](transaction_recording_lease& lease)
                -> transaction_recording_client&
            {
                return *lease.client;
            }};

    const auto keys = keys_for_both_database_shards(*catalog);
    const auto result = cnetmod::sync_wait(
        gateway.distributed_transaction<int>(keys,
            [](auto&) -> cnetmod::task<std::expected<int, std::string>>
            {
                throw std::runtime_error{"transaction callback failed"};
                co_return 0;
            }));
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), "transaction callback failed");
    for (const auto& client : clients)
        ASSERT_TRUE(contains_statement(client, "XA ROLLBACK"));
}

RUN_TESTS()
