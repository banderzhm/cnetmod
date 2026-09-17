#include "test_framework.hpp"
#include <cnetmod/orm.hpp>

import std;
import cnetmod.orm;
import cnetmod.coro.task;

namespace orm = cnetmod::orm;

struct sharded_order
{
    std::int64_t id{};
    std::string description;
};

CNETMOD_MODEL(sharded_order, "orders",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(description, "description", varchar))

struct recording_database_client
{
    std::string last_sql;
    std::vector<std::string> statements;

    auto query(std::string_view sql) -> cnetmod::task<orm::query_result>
    {
        last_sql = sql;
        statements.emplace_back(sql);
        co_return orm::query_result{};
    }

    auto execute(std::string_view sql) -> cnetmod::task<orm::query_result>
    {
        last_sql = sql;
        statements.emplace_back(sql);
        co_return orm::query_result{};
    }

    auto execute(orm::parameterized_query statement)
        -> cnetmod::task<orm::query_result>
    {
        last_sql = statement.query;
        statements.push_back(statement.query);
        co_return orm::query_result{};
    }
};

struct recording_database_lease
{
    recording_database_client* client{};
};

TEST(orm_shard_catalog_rejects_invalid_topology)
{
    orm::shard_catalog catalog;
    ASSERT_FALSE(catalog.route(orm::shard_key{1ULL}).has_value());
    ASSERT_TRUE(catalog.add_database("orders-0").has_value());
    ASSERT_FALSE(catalog.add_database("orders-0").has_value());
    ASSERT_FALSE(catalog.freeze("orders;drop", 4,
                            std::make_shared<orm::hash_shard_strategy>())
            .has_value());

    orm::shard_catalog oversized;
    ASSERT_TRUE(oversized.add_database("orders-0").has_value());
    ASSERT_FALSE(oversized.freeze(std::string(61, 'x'), 100,
                              std::make_shared<orm::hash_shard_strategy>())
            .has_value());
}

TEST(orm_hash_sharding_is_stable_and_bounded)
{
    orm::shard_catalog catalog;
    ASSERT_TRUE(catalog.add_database("orders-0").has_value());
    ASSERT_TRUE(catalog.add_database("orders-1").has_value());
    ASSERT_TRUE(catalog.freeze("orders", 64,
                           std::make_shared<orm::hash_shard_strategy>())
            .has_value());

    const auto first = catalog.route(orm::shard_key{"tenant-42"});
    const auto second = catalog.route(orm::shard_key{"tenant-42"});
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    if (!first || !second)
        return;
    ASSERT_EQ(first->database_shard, second->database_shard);
    ASSERT_EQ(first->table_shard, second->table_shard);
    ASSERT_EQ(first->instance, second->instance);
    ASSERT_EQ(first->physical_table, second->physical_table);
    ASSERT_TRUE(first->database_shard < 2U);
    ASSERT_TRUE(first->table_shard < 64U);
    ASSERT_TRUE(orm::valid_shard_identifier(first->physical_table));
}

TEST(orm_database_and_table_shards_are_independently_distributed)
{
    orm::shard_catalog catalog;
    ASSERT_TRUE(catalog.add_database("orders-0").has_value());
    ASSERT_TRUE(catalog.add_database("orders-1").has_value());
    ASSERT_TRUE(catalog.freeze("orders", 4,
                           std::make_shared<orm::hash_shard_strategy>())
            .has_value());

    std::set<std::pair<std::size_t, std::size_t>> destinations;
    for (std::uint64_t key = 0; key < 2048; ++key)
    {
        const auto route = catalog.route(orm::shard_key{key});
        ASSERT_TRUE(route.has_value());
        if (route)
            destinations.emplace(route->database_shard, route->table_shard);
    }
    ASSERT_EQ(destinations.size(), 8U);
}

TEST(orm_shard_catalog_enumerates_every_physical_destination)
{
    orm::shard_catalog catalog;
    ASSERT_TRUE(catalog.add_database("orders-0").has_value());
    ASSERT_TRUE(catalog.add_database("orders-1").has_value());
    ASSERT_TRUE(catalog.freeze("orders", 3,
                           std::make_shared<orm::hash_shard_strategy>())
            .has_value());

    const auto routes = catalog.routes();
    ASSERT_TRUE(routes.has_value());
    ASSERT_EQ(routes->size(), 6U);
    ASSERT_EQ(routes->front().instance, "orders-0");
    ASSERT_EQ(routes->front().physical_table, "orders_00");
    ASSERT_EQ(routes->back().instance, "orders-1");
    ASSERT_EQ(routes->back().physical_table, "orders_02");
}

TEST(orm_empty_text_shard_key_is_rejected)
{
    orm::shard_catalog catalog;
    ASSERT_TRUE(catalog.add_database("orders-0").has_value());
    ASSERT_TRUE(catalog.freeze("orders", 8,
                           std::make_shared<orm::hash_shard_strategy>())
            .has_value());
    const auto result = catalog.route(orm::shard_key{std::string_view{}});
    ASSERT_FALSE(result.has_value());
    if (!result)
        ASSERT_EQ(result.error(),
            orm::make_error_code(orm::sharding_errc::invalid_shard_key));
}

TEST(orm_routed_session_uses_physical_table_for_every_typed_operation)
{
    recording_database_client client;
    orm::database_session session{client, std::string{"orders_07"},
        orm::sql_dialect::mysql};

    orm::query_wrapper<sharded_order> query;
    query.eq("id", 42);
    (void)cnetmod::sync_wait(session.find(query));
    ASSERT_TRUE(client.last_sql.starts_with("SELECT * FROM `orders_07`"));

    (void)cnetmod::sync_wait(session.count(query));
    ASSERT_TRUE(client.last_sql.starts_with("SELECT COUNT(*) FROM `orders_07`"));

    sharded_order order{42, "routed"};
    (void)cnetmod::sync_wait(session.insert(order));
    ASSERT_TRUE(client.last_sql.starts_with("INSERT INTO `orders_07`"));

    (void)cnetmod::sync_wait(session.update(order));
    ASSERT_TRUE(client.last_sql.starts_with("UPDATE `orders_07`"));

    (void)cnetmod::sync_wait(session.remove(query));
    ASSERT_TRUE(client.last_sql.starts_with("DELETE FROM `orders_07`"));

    orm::update_wrapper<sharded_order> update;
    update.set("description", "updated").eq("id", 42);
    (void)cnetmod::sync_wait(session.update(update));
    ASSERT_TRUE(client.last_sql.starts_with("UPDATE `orders_07`"));
}

TEST(orm_sharded_gateway_pins_named_instance_and_table)
{
    auto catalog = std::make_shared<orm::shard_catalog>();
    ASSERT_TRUE(catalog->add_database("orders-primary").has_value());
    ASSERT_TRUE(catalog->freeze("orders", 16,
                           std::make_shared<orm::hash_shard_strategy>())
            .has_value());

    recording_database_client client;
    std::string acquired_instance;
    orm::sharded_session_gateway<recording_database_client,
        recording_database_lease>
        gateway{
            catalog,
            orm::sql_dialect::mysql,
            [&](std::string_view instance)
                -> cnetmod::task<std::expected<recording_database_lease, std::string>>
            {
                acquired_instance = instance;
                co_return recording_database_lease{&client};
            },
            [](recording_database_lease& lease) -> recording_database_client&
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
            orm::query_wrapper<sharded_order> query;
            query.eq("id", 7);
            auto result = co_await session.find(query);
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
            sharded_order order{7, "transaction"};
            auto result = co_await session.update(order);
            if (result.is_err())
                co_return std::unexpected(result.error_msg);
            co_return 1;
        }));
    ASSERT_TRUE(write.has_value());
    ASSERT_EQ(client.last_sql, "COMMIT");
}

TEST(orm_scatter_gather_reports_every_shard_outcome)
{
    auto catalog = std::make_shared<orm::shard_catalog>();
    ASSERT_TRUE(catalog->add_database("orders-primary").has_value());
    ASSERT_TRUE(catalog->freeze("orders", 3,
                           std::make_shared<orm::hash_shard_strategy>())
            .has_value());
    recording_database_client client;
    orm::sharded_session_gateway<recording_database_client,
        recording_database_lease>
        gateway{
            catalog, orm::sql_dialect::mysql,
            [&](std::string_view)
                -> cnetmod::task<std::expected<recording_database_lease, std::string>>
            {
                co_return recording_database_lease{&client};
            },
            [](recording_database_lease& lease) -> recording_database_client&
            {
                return *lease.client;
            }};

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

TEST(orm_distributed_transaction_uses_mysql_xa_two_phase_commit)
{
    auto catalog = std::make_shared<orm::shard_catalog>();
    ASSERT_TRUE(catalog->add_database("orders-0").has_value());
    ASSERT_TRUE(catalog->add_database("orders-1").has_value());
    ASSERT_TRUE(catalog->freeze("orders", 2,
                           std::make_shared<orm::hash_shard_strategy>())
            .has_value());

    std::array<recording_database_client, 2> clients;
    orm::sharded_session_gateway<recording_database_client,
        recording_database_lease>
        gateway{
            catalog, orm::sql_dialect::mysql,
            [&](std::string_view instance)
                -> cnetmod::task<std::expected<recording_database_lease, std::string>>
            {
                co_return recording_database_lease{
                    &clients[instance == "orders-0" ? 0U : 1U]};
            },
            [](recording_database_lease& lease) -> recording_database_client&
            {
                return *lease.client;
            }};

    std::array<std::optional<orm::shard_key>, 2> selected;
    for (std::uint64_t value = 1; value < 1000; ++value)
    {
        const auto route = catalog->route(orm::shard_key{value});
        if (route && !selected[route->database_shard])
            selected[route->database_shard] = orm::shard_key{value};
    }
    ASSERT_TRUE(selected[0].has_value());
    ASSERT_TRUE(selected[1].has_value());
    const std::array keys{*selected[0], *selected[1]};

    const auto result = cnetmod::sync_wait(
        gateway.distributed_transaction<int>(keys,
            [](auto& sessions)
                -> cnetmod::task<std::expected<int, std::string>>
            {
                ASSERT_EQ(sessions.entries().size(), 2U);
                for (auto& entry : sessions.entries())
                {
                    sharded_order order{
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
        ASSERT_TRUE(std::ranges::any_of(client.statements,
            [](const std::string& sql)
            {
                return sql.starts_with("XA START");
            }));
        ASSERT_TRUE(std::ranges::any_of(client.statements,
            [](const std::string& sql)
            {
                return sql.starts_with("XA PREPARE");
            }));
        ASSERT_TRUE(std::ranges::any_of(client.statements,
            [](const std::string& sql)
            {
                return sql.starts_with("XA COMMIT");
            }));
    }
}

TEST(orm_distributed_transaction_rolls_back_every_started_shard)
{
    auto catalog = std::make_shared<orm::shard_catalog>();
    ASSERT_TRUE(catalog->add_database("orders-0").has_value());
    ASSERT_TRUE(catalog->add_database("orders-1").has_value());
    ASSERT_TRUE(catalog->freeze("orders", 1,
                           std::make_shared<orm::hash_shard_strategy>())
            .has_value());
    std::array<recording_database_client, 2> clients;
    orm::sharded_session_gateway<recording_database_client,
        recording_database_lease>
        gateway{
            catalog, orm::sql_dialect::mysql,
            [&](std::string_view instance)
                -> cnetmod::task<std::expected<recording_database_lease, std::string>>
            {
                co_return recording_database_lease{
                    &clients[instance == "orders-0" ? 0U : 1U]};
            },
            [](recording_database_lease& lease) -> recording_database_client&
            {
                return *lease.client;
            }};
    std::array<std::optional<orm::shard_key>, 2> selected;
    for (std::uint64_t value = 1; value < 1000; ++value)
    {
        const auto route = catalog->route(orm::shard_key{value});
        if (route && !selected[route->database_shard])
            selected[route->database_shard] = orm::shard_key{value};
    }
    ASSERT_TRUE(selected[0].has_value());
    ASSERT_TRUE(selected[1].has_value());
    const std::array keys{*selected[0], *selected[1]};
    const auto result = cnetmod::sync_wait(
        gateway.distributed_transaction<int>(keys,
            [](auto&) -> cnetmod::task<std::expected<int, std::string>>
            {
                co_return std::unexpected("business rule rejected write");
            }));
    ASSERT_FALSE(result.has_value());
    for (const auto& client : clients)
    {
        ASSERT_TRUE(std::ranges::any_of(client.statements,
            [](const std::string& sql)
            {
                return sql.starts_with("XA ROLLBACK");
            }));
        ASSERT_FALSE(std::ranges::any_of(client.statements,
            [](const std::string& sql)
            {
                return sql.starts_with("XA COMMIT");
            }));
    }
}

TEST(orm_sharding_capabilities_can_be_disabled_independently)
{
    auto catalog = std::make_shared<orm::shard_catalog>();
    ASSERT_TRUE(catalog->add_database("orders-primary").has_value());
    ASSERT_TRUE(catalog->freeze("orders", 1,
                           std::make_shared<orm::hash_shard_strategy>())
            .has_value());
    recording_database_client client;
    orm::sharded_session_gateway<recording_database_client,
        recording_database_lease>
        gateway{
            catalog, orm::sql_dialect::mysql,
            [&](std::string_view)
                -> cnetmod::task<std::expected<recording_database_lease, std::string>>
            {
                co_return recording_database_lease{&client};
            },
            [](recording_database_lease& lease) -> recording_database_client&
            {
                return *lease.client;
            },
            false, false};

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
