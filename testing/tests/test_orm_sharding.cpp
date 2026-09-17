#include "test_framework.hpp"
#include <cnetmod/orm.hpp>

import std;
import cnetmod.orm;

namespace orm = cnetmod::orm;

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

RUN_TESTS()
