#include "test_framework.hpp"
#include <cnetmod/orm.hpp>

import std;
import cnetmod.orm;
import cnetmod.coro.task;

namespace orm = cnetmod::orm;

struct routed_order
{
    std::int64_t id{};
    std::string description;
};

CNETMOD_MODEL(routed_order, "orders",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(description, "description", varchar))

struct recording_session_client
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

TEST(orm_routed_session_uses_physical_table_for_every_typed_operation)
{
    recording_session_client client;
    orm::database_session session{client, std::string{"orders_07"},
        orm::sql_dialect::mysql};

    orm::query_wrapper<routed_order> query;
    query.eq("id", 42);
    (void)cnetmod::sync_wait(session.find(query));
    ASSERT_TRUE(client.last_sql.starts_with("SELECT * FROM `orders_07`"));

    (void)cnetmod::sync_wait(session.count(query));
    ASSERT_TRUE(client.last_sql.starts_with("SELECT COUNT(*) FROM `orders_07`"));

    routed_order order{42, "routed"};
    (void)cnetmod::sync_wait(session.insert(order));
    ASSERT_TRUE(client.last_sql.starts_with("INSERT INTO `orders_07`"));
    (void)cnetmod::sync_wait(session.update(order));
    ASSERT_TRUE(client.last_sql.starts_with("UPDATE `orders_07`"));
    (void)cnetmod::sync_wait(session.remove(query));
    ASSERT_TRUE(client.last_sql.starts_with("DELETE FROM `orders_07`"));

    orm::update_wrapper<routed_order> update;
    update.set("description", "updated").eq("id", 42);
    (void)cnetmod::sync_wait(session.update(update));
    ASSERT_TRUE(client.last_sql.starts_with("UPDATE `orders_07`"));
}

RUN_TESTS()
