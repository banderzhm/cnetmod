#include "test_framework.hpp"
#include <cnetmod/orm.hpp>

import std;
import cnetmod.orm;
import cnetmod.protocol.mysql;
import cnetmod.coro.task;
import cnetmod.coro.cancel;

namespace orm = cnetmod::orm;
namespace mysql = cnetmod::mysql;

struct routed_order
{
    std::int64_t id{};
    std::string description;
};

CNETMOD_MODEL(routed_order, "orders",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(description, "description", varchar))

struct policy_order
{
    std::int64_t id{};
    std::int64_t tenant_id{};
    std::int64_t deleted{};
};

CNETMOD_MODEL(policy_order, "policy_orders",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(tenant_id, "tenant_id", bigint, TENANT_ID),
    CNETMOD_FIELD(deleted, "deleted", tinyint, LOGIC_DELETE))

struct versioned_order
{
    std::int64_t id{};
    std::int64_t version{};
    std::string description;
};

CNETMOD_MODEL(versioned_order, "versioned_orders",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(version, "version", bigint, VERSION),
    CNETMOD_FIELD(description, "description", varchar))

struct filled_order
{
    std::int64_t id{};
    orm::calendar_datetime created_at{};
};

CNETMOD_MODEL(filled_order, "filled_orders",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(created_at, "created_at", datetime, FILL_INSERT))

struct recording_session_client
{
    std::string last_sql;
    std::vector<std::string> statements;
    orm::query_result response;
    std::deque<orm::query_result> responses;

    auto next_response() -> orm::query_result
    {
        if (responses.empty())
            return response;
        auto next = std::move(responses.front());
        responses.pop_front();
        return next;
    }

    auto query(std::string_view sql) -> cnetmod::task<orm::query_result>
    {
        last_sql = sql;
        statements.push_back(last_sql);
        co_return next_response();
    }

    auto execute(std::string_view sql) -> cnetmod::task<orm::query_result>
    {
        last_sql = sql;
        statements.push_back(last_sql);
        co_return next_response();
    }

    auto execute(orm::parameterized_query statement)
        -> cnetmod::task<orm::query_result>
    {
        last_sql = std::move(statement.query);
        statements.push_back(last_sql);
        co_return next_response();
    }
};

struct streaming_session_client : recording_session_client
{
    std::deque<std::vector<mysql::row>> stream_batches;
    std::size_t stream_starts{};
    std::size_t stream_reads{};
    bool open = true;

    auto start_execution(orm::parameterized_query statement,
        mysql::execution_state& state) -> cnetmod::task<void>
    {
        ++stream_starts;
        last_sql = std::move(statement.query);
        statements.push_back(last_sql);
        state.set_columns({
            mysql::column_meta{.name = "id", .type = mysql::field_type::longlong},
            mysql::column_meta{.name = "description",
                .type = mysql::field_type::var_string}});
        state.set_state(mysql::execution_state::state_t::reading_rows);
        co_return;
    }

    auto read_some_rows(mysql::execution_state& state, std::size_t max_rows)
        -> cnetmod::task<std::vector<mysql::row>>
    {
        ++stream_reads;
        if (stream_batches.empty())
        {
            state.set_state(mysql::execution_state::state_t::complete);
            co_return std::vector<mysql::row>{};
        }

        auto rows = std::move(stream_batches.front());
        stream_batches.pop_front();
        if (rows.size() > max_rows)
        {
            state.set_error(0, "fake stream batch exceeds requested bound");
            co_return std::vector<mysql::row>{};
        }
        if (rows.size() < max_rows && stream_batches.empty())
            state.set_state(mysql::execution_state::state_t::complete);
        co_return rows;
    }

    auto read_resultset_head(mysql::execution_state&) -> cnetmod::task<void>
    {
        co_return;
    }

    void close() noexcept
    {
        open = false;
    }

    [[nodiscard]] auto is_open() const noexcept -> bool
    {
        return open;
    }
};

auto mysql_order_row(std::int64_t id, std::string description) -> mysql::row
{
    return {mysql::field_value::from_int64(id),
        mysql::field_value::from_string(std::move(description))};
}

[[maybe_unused]] auto base_mapper_complete_surface_compile_probe(
    mysql::client& client) -> cnetmod::task<void>
{
    orm::mysql_base_mapper<routed_order> mapper{client};
    std::array models{routed_order{1, "one"}, routed_order{2, "two"}};
    const std::span<const routed_order> immutable{models};
    const std::vector<std::pair<std::string, orm::param_value>> filters{
        {"id", orm::param_value::from_int(1)}};
    orm::query_wrapper<routed_order> query;
    query.select("id", "description");
    (void)co_await mapper.update_batch_by_id(immutable, 1);
    (void)co_await mapper.save_or_update(models[0]);
    (void)co_await mapper.save_or_update_batch(std::span{models}, 1);
    (void)co_await mapper.upsert_batch(std::span{models}, 1);
    (void)co_await mapper.select_by_map(filters);
    (void)co_await mapper.select_maps(query);
    (void)co_await mapper.select_objects(query);
    (void)co_await mapper.select_maps_page(1, 20, query);
}

[[maybe_unused]] auto repository_complete_surface_compile_probe(
    recording_session_client& client) -> cnetmod::task<void>
{
    orm::database_session session{client, orm::sql_dialect::mysql};
    orm::session_repository<routed_order, decltype(session)> orders{session};
    std::array ids{std::int64_t{1}, std::int64_t{2}};
    orm::query_wrapper<routed_order> query;
    query.select("id", "description");
    std::array models{routed_order{1, "one"}, routed_order{2, "two"}};
    (void)co_await orders.exists(query);
    (void)co_await orders.list_by_ids(std::span<const std::int64_t>{ids});
    (void)co_await orders.select_maps(query);
    (void)co_await orders.select_objects(query);
    (void)co_await orders.page_maps(1, 20, query);
    (void)co_await orders.update_by_wrapper(orm::update_wrapper<routed_order>{});
    (void)co_await orders.remove(query);
    (void)co_await orders.save_batch(std::span<routed_order>{models}, 1);
}

[[maybe_unused]] auto service_complete_surface_compile_probe(
    recording_session_client& client) -> cnetmod::task<void>
{
    using gateway_type = orm::session_gateway<recording_session_client,
        std::monostate>;
    gateway_type gateway{
        orm::sql_dialect::mysql,
        []() -> cnetmod::task<std::expected<void, std::string>>
        {
            co_return std::expected<void, std::string>{};
        },
        []() -> cnetmod::task<std::expected<std::monostate, std::string>>
        {
            co_return std::monostate{};
        },
        [&client](std::monostate&) -> recording_session_client&
        {
            return client;
        }};
    orm::repository<routed_order, gateway_type> orders{gateway};
    std::array ids{std::int64_t{1}, std::int64_t{2}};
    orm::query_wrapper<routed_order> query;
    query.select("id", "description");
    (void)co_await orders.list_by_ids(std::span<const std::int64_t>{ids});
    (void)co_await orders.exists(query);
    (void)co_await orders.select_maps(query);
    (void)co_await orders.select_objects(query);
    (void)co_await orders.page_maps(1, 20, query);
    (void)co_await orders.for_each_map(query,
        [](const orm::projection_row&) -> cnetmod::task<
            std::expected<void, std::string>>
        {
            co_return std::expected<void, std::string>{};
        });
}

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

auto routed_order_result(std::initializer_list<std::pair<std::int64_t, std::string>> rows)
    -> orm::query_result
{
    orm::query_result result;
    result.columns = {{.name = "id"}, {.name = "description"}};
    for (const auto& [id, description] : rows)
    {
        result.rows.push_back({orm::field_value::from_int64(id),
            orm::field_value::from_string(description)});
    }
    return result;
}

TEST(orm_find_one_enforces_unique_cardinality_without_losing_diagnostics)
{
    recording_session_client client;
    client.response = routed_order_result({{1, "first"}, {2, "second"}});
    orm::database_session session{client, orm::sql_dialect::mysql};
    orm::query_wrapper<routed_order> query;
    query.eq("description", "duplicate");

    auto strict = cnetmod::sync_wait(session.find_one(query));
    ASSERT_TRUE(strict.is_err());
    ASSERT_EQ(strict.framework_error,
        std::make_error_code(std::errc::result_out_of_range));
    ASSERT_TRUE(strict.data.empty());
    ASSERT_TRUE(client.last_sql.contains("LIMIT 2"));

    client.response.error_msg = "database unavailable";
    client.response.sql_state = "08006";
    client.response.error_code = 2013;
    auto failed = cnetmod::sync_wait(session.find_one(query));
    ASSERT_TRUE(failed.is_err());
    ASSERT_EQ(failed.error_msg, "database unavailable");
    ASSERT_EQ(failed.sql_state, "08006");
    ASSERT_EQ(failed.error_code, 2013U);
    ASSERT_FALSE(static_cast<bool>(failed.framework_error));
}

TEST(orm_find_first_exists_and_find_by_ids_are_explicit_and_bounded)
{
    recording_session_client client;
    client.response = routed_order_result({{7, "selected"}});
    orm::database_session session{client, orm::sql_dialect::mysql};
    orm::query_wrapper<routed_order> query;
    query.eq("description", "selected");

    auto first = cnetmod::sync_wait(session.find_first(query));
    ASSERT_TRUE(first.ok());
    ASSERT_EQ(first.data.size(), 1U);
    ASSERT_TRUE(client.last_sql.contains("LIMIT 1"));

    auto exists = cnetmod::sync_wait(session.exists(query));
    ASSERT_TRUE(exists.ok());
    ASSERT_EQ(exists.data.size(), 1U);
    ASSERT_TRUE(exists.data.front());
    ASSERT_TRUE(client.last_sql.starts_with("SELECT *"));
    ASSERT_TRUE(client.last_sql.contains("LIMIT 1"));

    client.response = {};
    exists = cnetmod::sync_wait(session.exists(query));
    ASSERT_TRUE(exists.ok());
    ASSERT_FALSE(exists.data.front());

    client.response.error_msg = "connection lost";
    client.response.sql_state = "08006";
    client.response.error_code = 2013;
    exists = cnetmod::sync_wait(session.exists(query));
    ASSERT_TRUE(exists.is_err());
    ASSERT_TRUE(exists.data.empty());
    ASSERT_EQ(exists.sql_state, "08006");
    ASSERT_EQ(exists.error_code, 2013U);

    client.response = routed_order_result({{7, "selected"}});
    const std::array ids{1LL, 7LL, 9LL};
    auto selected = cnetmod::sync_wait(
        session.find_by_ids<routed_order>(std::span<const long long>{ids}));
    ASSERT_TRUE(selected.ok());
    ASSERT_TRUE(client.last_sql.contains(" IN ("));

    const std::span<const long long> empty;
    selected = cnetmod::sync_wait(session.find_by_ids<routed_order>(empty));
    ASSERT_TRUE(selected.ok());
    ASSERT_TRUE(selected.data.empty());
}

TEST(orm_map_filters_and_projection_queries_preserve_shape_and_types)
{
    recording_session_client client;
    orm::database_session session{client, orm::sql_dialect::mysql};

    const std::vector<std::pair<std::string, orm::param_value>> filters{
        {"description", orm::param_value::from_string("selected")},
        {"id", orm::param_value::null()}};
    client.response = routed_order_result({{7, "selected"}});
    auto found = cnetmod::sync_wait(
        session.find_by_map<routed_order>(filters));
    ASSERT_TRUE(found.ok());
    ASSERT_TRUE(client.last_sql.contains("`description` = {}"));
    ASSERT_TRUE(client.last_sql.contains("`id` IS NULL"));

    const std::vector<std::pair<std::string, orm::param_value>> invalid{
        {"not_a_column", orm::param_value::from_int(1)}};
    found = cnetmod::sync_wait(session.find_by_map<routed_order>(invalid));
    ASSERT_TRUE(found.is_err());
    ASSERT_EQ(found.framework_error,
        std::make_error_code(std::errc::invalid_argument));

    orm::query_wrapper<routed_order> projection;
    projection.select("id", "description").order_by_asc("id");
    client.response = routed_order_result({{7, "selected"}, {9, "next"}});
    auto maps = cnetmod::sync_wait(session.select_maps(projection));
    ASSERT_TRUE(maps.ok());
    ASSERT_EQ(maps.data.size(), 2U);
    ASSERT_EQ(maps.data[0].at("id").get_int64(), 7);
    ASSERT_EQ(maps.data[1].at("description").get_string(), "next");

    orm::query_wrapper<routed_order> ids;
    ids.select("id").order_by_asc("id");
    auto objects = cnetmod::sync_wait(session.select_objects(ids));
    ASSERT_TRUE(objects.ok());
    ASSERT_EQ(objects.data.size(), 2U);
    ASSERT_EQ(objects.data[0].get_int64(), 7);
}

TEST(orm_projection_page_keeps_count_and_page_query_diagnostics)
{
    recording_session_client client;
    orm::database_session session{client, orm::sql_dialect::mysql};
    orm::query_result count;
    count.columns = {{.name = "count"}};
    count.rows = {{orm::field_value::from_int64(3)}};
    client.responses.push_back(std::move(count));
    client.responses.push_back(
        routed_order_result({{3, "third"}}));

    orm::query_wrapper<routed_order> query;
    query.select("id", "description").order_by_asc("id");
    auto page = cnetmod::sync_wait(
        session.page_maps<routed_order>(2, 2, query));
    ASSERT_TRUE(page.ok());
    ASSERT_EQ(page.total, 3U);
    ASSERT_EQ(page.total_pages, 2U);
    ASSERT_TRUE(page.has_previous());
    ASSERT_FALSE(page.has_next());
    ASSERT_EQ(page.records.data.size(), 1U);
    ASSERT_TRUE(client.last_sql.contains("LIMIT 2 OFFSET 2"));

    client.responses.push_back(routed_order_result({{3, "unused"}}));
    client.responses.back().rows = {{orm::field_value::from_int64(3)}};
    client.responses.back().columns = {{.name = "count"}};
    client.responses.push_back(routed_order_result({{3, "third"}}));
    auto models = cnetmod::sync_wait(
        session.page<routed_order>(2, 2));
    ASSERT_TRUE(models.ok());
    ASSERT_EQ(models.total, 3U);
    ASSERT_EQ(models.records.data.size(), 1U);
    ASSERT_EQ(models.records.data.front().description, "third");
}

TEST(orm_batch_mutations_are_transactional_and_full_delete_is_explicit)
{
    recording_session_client client;
    orm::database_session session{client, orm::sql_dialect::mysql};
    orm::query_result updated;
    updated.affected_rows = 1;
    client.responses.push_back({});
    client.responses.push_back(updated);
    client.responses.push_back(updated);
    client.responses.push_back({});

    const std::array orders{
        routed_order{1, "first"}, routed_order{2, "second"}};
    auto batch = cnetmod::sync_wait(
        session.update_batch_by_id<routed_order>(orders, 1));
    ASSERT_TRUE(batch.ok());
    ASSERT_EQ(batch.affected_rows, 2U);
    ASSERT_EQ(client.statements.front(), "START TRANSACTION");
    ASSERT_EQ(client.statements.back(), "COMMIT");

    client.statements.clear();
    orm::query_wrapper<routed_order> empty;
    auto rejected = cnetmod::sync_wait(session.remove(empty));
    ASSERT_TRUE(rejected.is_err());
    ASSERT_EQ(rejected.framework_error,
        std::make_error_code(std::errc::operation_not_permitted));
    ASSERT_TRUE(client.statements.empty());

    client.response = {};
    auto allowed = cnetmod::sync_wait(
        session.remove(empty, orm::allow_full_table));
    ASSERT_TRUE(allowed.ok());
    ASSERT_EQ(client.last_sql, "DELETE FROM `orders`");

    client.statements.clear();
    orm::update_wrapper<routed_order> full_update;
    full_update.set("description", "rewritten");
    auto update_rejected = cnetmod::sync_wait(session.update(full_update));
    ASSERT_TRUE(update_rejected.is_err());
    ASSERT_EQ(update_rejected.framework_error,
        std::make_error_code(std::errc::operation_not_permitted));
    ASSERT_TRUE(client.statements.empty());

    auto update_allowed = cnetmod::sync_wait(
        session.update(full_update, orm::allow_full_table));
    ASSERT_TRUE(update_allowed.ok());
    ASSERT_TRUE(client.last_sql.starts_with("UPDATE `orders` SET"));
}

TEST(orm_bulk_delete_helpers_validate_input_and_preserve_empty_id_semantics)
{
    recording_session_client client;
    orm::database_session session{client, orm::sql_dialect::mysql};
    const std::array ids{1LL, 2LL};
    auto removed = cnetmod::sync_wait(
        session.remove_by_ids<routed_order>(std::span<const long long>{ids}));
    ASSERT_TRUE(removed.ok());
    ASSERT_TRUE(client.last_sql.contains("`id` IN ("));

    client.statements.clear();
    const std::span<const long long> empty_ids;
    removed = cnetmod::sync_wait(
        session.remove_by_ids<routed_order>(empty_ids));
    ASSERT_TRUE(removed.ok());
    ASSERT_TRUE(client.statements.empty());

    const std::vector<std::pair<std::string, orm::param_value>> filters{
        {"description", orm::param_value::from_string("obsolete")}};
    removed = cnetmod::sync_wait(
        session.remove_by_map<routed_order>(filters));
    ASSERT_TRUE(removed.ok());
    ASSERT_TRUE(client.last_sql.contains("`description` = {}"));

    const std::vector<std::pair<std::string, orm::param_value>> no_filters;
    removed = cnetmod::sync_wait(
        session.remove_by_map<routed_order>(no_filters));
    ASSERT_TRUE(removed.is_err());
    ASSERT_EQ(removed.framework_error,
        std::make_error_code(std::errc::operation_not_permitted));
}

TEST(orm_save_or_update_batch_rolls_back_and_preserves_the_primary_failure)
{
    recording_session_client client;
    orm::database_session session{client, orm::sql_dialect::mysql};
    orm::query_result updated;
    updated.affected_rows = 1;
    orm::query_result failed;
    failed.error_msg = "write failed";
    failed.sql_state = "40001";
    failed.error_code = 1213;

    client.responses.push_back({});
    client.responses.push_back(routed_order_result({{1, "old"}}));
    client.responses.push_back(updated);
    client.responses.push_back(routed_order_result({{2, "old"}}));
    client.responses.push_back(failed);
    client.responses.push_back({});

    std::array orders{
        routed_order{1, "first"}, routed_order{2, "second"}};
    auto result = cnetmod::sync_wait(
        session.save_or_update_batch<routed_order>(orders, 1));
    ASSERT_TRUE(result.is_err());
    ASSERT_EQ(result.error_msg, "write failed");
    ASSERT_EQ(result.sql_state, "40001");
    ASSERT_EQ(result.error_code, 1213U);
    ASSERT_EQ(client.statements.front(), "START TRANSACTION");
    ASSERT_EQ(client.statements.back(), "ROLLBACK");
}

TEST(orm_interceptor_chain_orders_and_freezes_registration)
{
    orm::interceptor_chain chain;
    std::vector<std::string> order;
    ASSERT_TRUE(chain.add("late", 20,
        [&order](orm::sql_operation,
            orm::intercepted_statement statement)
            -> std::expected<orm::intercepted_statement, std::string>
        {
            order.push_back("late");
            return statement;
        }));
    ASSERT_TRUE(chain.add("early", 10,
        [&order](orm::sql_operation,
            orm::intercepted_statement statement)
            -> std::expected<orm::intercepted_statement, std::string>
        {
            order.push_back("early");
            return statement;
        }));
    ASSERT_TRUE(chain.add("duplicate", 30,
        [](orm::sql_operation, orm::intercepted_statement statement)
            -> std::expected<orm::intercepted_statement, std::string>
        {
            return statement;
        }));
    auto duplicate = chain.add("duplicate", 40,
        [](orm::sql_operation, orm::intercepted_statement statement)
            -> std::expected<orm::intercepted_statement, std::string>
        {
            return statement;
        });
    ASSERT_FALSE(duplicate);
    ASSERT_TRUE(chain.freeze());
    ASSERT_TRUE(chain.frozen());
    auto late_registration = chain.add("too-late", 1,
        [](orm::sql_operation, orm::intercepted_statement statement)
            -> std::expected<orm::intercepted_statement, std::string>
        {
            return statement;
        });
    ASSERT_FALSE(late_registration);

    auto result = chain.apply(orm::sql_operation::query,
        {"SELECT 1", {}});
    ASSERT_TRUE(result);
    ASSERT_EQ(order.size(), 2U);
    ASSERT_EQ(order[0], "early");
    ASSERT_EQ(order[1], "late");
}

TEST(orm_interceptor_chain_rewrites_typed_sql_before_protocol_client)
{
    recording_session_client client;
    auto chain = std::make_shared<orm::interceptor_chain>();
    ASSERT_TRUE(chain->add("tenant", 10,
        [](orm::sql_operation,
            orm::intercepted_statement statement)
            -> std::expected<orm::intercepted_statement, std::string>
        {
            statement.sql = "SELECT * FROM `tenant_orders` WHERE `id` = {}";
            return statement;
        }));
    ASSERT_TRUE(chain->freeze());
    orm::database_session session{client, orm::sql_dialect::mysql, chain};

    orm::query_wrapper<routed_order> query;
    query.eq("id", 42);
    auto result = cnetmod::sync_wait(session.find(query));
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(client.last_sql,
        "SELECT * FROM `tenant_orders` WHERE `id` = {}");
}

TEST(orm_interceptor_chain_rejection_short_circuits_protocol_client)
{
    recording_session_client client;
    auto chain = std::make_shared<orm::interceptor_chain>();
    ASSERT_TRUE(chain->add("deny", 1,
        [](orm::sql_operation,
            orm::intercepted_statement)
            -> std::expected<orm::intercepted_statement, std::string>
        {
            return std::unexpected("tenant context is missing");
        }));
    ASSERT_TRUE(chain->freeze());
    orm::database_session session{client, orm::sql_dialect::mysql, chain};

    orm::query_wrapper<routed_order> query;
    query.eq("id", 42);
    auto result = cnetmod::sync_wait(session.find(query));
    ASSERT_TRUE(result.is_err());
    ASSERT_TRUE(result.error_msg.contains("deny"));
    ASSERT_TRUE(client.statements.empty());
}

TEST(orm_repository_delegates_to_database_session_contract)
{
    recording_session_client client;
    orm::database_session session{client, orm::sql_dialect::mysql};
    orm::session_repository<routed_order, decltype(session)> orders{session};

    client.responses.push_back(routed_order_result({{7, "stored"}}));
    auto loaded = cnetmod::sync_wait(
        orders.get_by_id(orm::param_value::from_int(7)));
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.data.front().id, 7);

    routed_order value{7, "updated"};
    client.responses.push_back({});
    auto updated = cnetmod::sync_wait(orders.update_by_id(value));
    ASSERT_TRUE(updated.ok());
    ASSERT_TRUE(client.last_sql.starts_with("UPDATE `orders` SET"));
}

TEST(orm_repository_batches_are_transactional_and_bounded)
{
    recording_session_client client;
    orm::database_session session{client, orm::sql_dialect::mysql};
    orm::session_repository<routed_order, decltype(session)> orders{session};
    client.responses.push_back({});
    client.responses.push_back({.affected_rows = 1});
    client.responses.push_back({.affected_rows = 1});
    client.responses.push_back({});
    client.responses.push_back({});
    client.responses.push_back({.affected_rows = 1});
    client.responses.push_back({});

    std::array models{
        routed_order{1, "one"}, routed_order{2, "two"},
        routed_order{3, "three"}};
    auto result = cnetmod::sync_wait(
        orders.save_batch(std::span<routed_order>{models}, 2));
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.affected_rows, 3U);
    ASSERT_EQ(client.statements.front(), "START TRANSACTION");
    ASSERT_EQ(client.statements[3], "COMMIT");
    ASSERT_EQ(client.statements.back(), "COMMIT");
}

TEST(orm_repository_batch_failure_preserves_exact_location)
{
    recording_session_client client;
    orm::database_session session{client, orm::sql_dialect::mysql};
    orm::session_repository<routed_order, decltype(session)> orders{session};
    orm::query_result failure;
    failure.error_msg = "duplicate key";
    failure.sql_state = "23000";
    failure.error_code = 1062;
    client.responses.push_back({});
    client.responses.push_back({});
    client.responses.push_back(failure);
    client.responses.push_back({});

    std::array models{
        routed_order{1, "one"}, routed_order{2, "two"},
        routed_order{3, "three"}};
    auto result = cnetmod::sync_wait(
        orders.save_batch(std::span<routed_order>{models}, 2));
    ASSERT_TRUE(result.is_err());
    ASSERT_TRUE(result.batch_index.has_value());
    ASSERT_TRUE(result.item_index.has_value());
    ASSERT_EQ(*result.batch_index, 0U);
    ASSERT_EQ(*result.item_index, 1U);
    ASSERT_EQ(result.operation, "insert_batch");
    ASSERT_EQ(result.sql_state, "23000");
    ASSERT_EQ(result.error_code, 1062U);
    ASSERT_EQ(client.statements.back(), "ROLLBACK");
}

TEST(orm_service_owns_leases_transactions_and_automatic_model_policies)
{
    recording_session_client client;
    using gateway_type = orm::session_gateway<recording_session_client,
        std::monostate>;
    gateway_type gateway{
        orm::sql_dialect::mysql,
        []() -> cnetmod::task<std::expected<void, std::string>>
        {
            co_return std::expected<void, std::string>{};
        },
        []() -> cnetmod::task<std::expected<std::monostate, std::string>>
        {
            co_return std::monostate{};
        },
        [&client](std::monostate&) -> recording_session_client&
        {
            return client;
        }};

    orm::tenant_guard tenant{73};
    orm::repository<policy_order, gateway_type> orders{gateway};
    auto listed = cnetmod::sync_wait(orders.list());
    ASSERT_TRUE(listed.ok());
    ASSERT_TRUE(client.last_sql.contains("tenant_id"));
    ASSERT_TRUE(client.last_sql.contains("deleted"));

    client.responses.push_back({});
    orm::query_result duplicate;
    duplicate.error_msg = "duplicate";
    duplicate.sql_state = "23000";
    duplicate.error_code = 1062;
    client.responses.push_back(duplicate);
    client.responses.push_back({});
    policy_order model{1, 73, 0};
    auto saved = cnetmod::sync_wait(orders.save(model));
    ASSERT_TRUE(saved.is_err());
    ASSERT_EQ(saved.sql_state, "23000");
    ASSERT_EQ(saved.error_code, 1062U);
    ASSERT_EQ(client.statements[client.statements.size() - 3],
        "START TRANSACTION");
    ASSERT_EQ(client.statements.back(), "ROLLBACK");
}

TEST(orm_update_wrapper_supports_parameterized_increment_and_decrement)
{
    recording_session_client client;
    orm::database_session session{client, orm::sql_dialect::mysql};
    orm::update_wrapper<routed_order> update;
    update.set_increment("id", 2).set_decrement("id", 1).eq("id", 7);
    auto result = cnetmod::sync_wait(session.update(update));
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(client.last_sql.contains("`id` = `id` - {}"));
}

TEST(orm_upsert_uses_native_mysql_and_postgresql_conflict_forms)
{
    routed_order mysql_value{9, "mysql"};
    recording_session_client mysql_client;
    orm::database_session mysql_session{mysql_client, orm::sql_dialect::mysql};
    auto mysql_result = cnetmod::sync_wait(mysql_session.upsert(mysql_value));
    ASSERT_TRUE(mysql_result.ok());
    ASSERT_TRUE(mysql_client.last_sql.contains("ON DUPLICATE KEY UPDATE"));
    ASSERT_TRUE(mysql_client.last_sql.contains("VALUES(`description`)") ||
        mysql_client.last_sql.contains("VALUES (`description`)"));

    routed_order postgres_value{10, "postgres"};
    recording_session_client postgres_client;
    orm::database_session postgres_session{
        postgres_client, orm::sql_dialect::postgresql};
    auto postgres_result = cnetmod::sync_wait(
        postgres_session.upsert(postgres_value));
    ASSERT_TRUE(postgres_result.ok());
    ASSERT_TRUE(postgres_client.last_sql.contains("ON CONFLICT (\"id\") DO UPDATE SET"));
    ASSERT_TRUE(postgres_client.last_sql.contains("EXCLUDED.\"description\""));
}

TEST(orm_for_each_streams_bounded_pages_and_honors_cancellation)
{
    recording_session_client client;
    orm::database_session session{client, orm::sql_dialect::mysql};
    client.responses.push_back(routed_order_result({{1, "one"}, {2, "two"}}));
    client.responses.push_back(routed_order_result({}));
    std::size_t seen = 0;
    auto handler = [&seen](const routed_order&) -> cnetmod::task<
                                                    std::expected<void, std::string>>
    {
        ++seen;
        co_return std::expected<void, std::string>{};
    };
    auto streamed = cnetmod::sync_wait(session.for_each<routed_order>({}, handler,
        orm::stream_options{.batch_size = 2, .max_rows = 10}));
    ASSERT_TRUE(streamed.has_value());
    ASSERT_EQ(seen, 2U);

    cnetmod::cancel_token cancellation;
    cancellation.cancel();
    auto cancelled = cnetmod::sync_wait(session.for_each<routed_order>({}, handler,
        orm::stream_options{}, cancellation));
    ASSERT_FALSE(cancelled.has_value());
}

TEST(orm_batch_failure_reports_batch_and_item_location)
{
    recording_session_client client;
    client.responses.push_back({});
    orm::query_result failure;
    failure.error_msg = "duplicate key";
    failure.sql_state = "23000";
    failure.error_code = 1062;
    client.responses.push_back(failure);

    orm::database_session session{client, orm::sql_dialect::mysql};
    std::array values{routed_order{1, "one"}, routed_order{2, "two"}};
    auto result = cnetmod::sync_wait(
        session.upsert_batch<routed_order>(std::span<routed_order>{values}, 1));
    ASSERT_TRUE(result.is_err());
    ASSERT_TRUE(result.batch_index.has_value());
    ASSERT_TRUE(result.item_index.has_value());
    ASSERT_EQ(*result.batch_index, 0U);
    ASSERT_EQ(*result.item_index, 0U);
    ASSERT_EQ(result.operation, "upsert");
    ASSERT_EQ(result.sql_state, "23000");
    ASSERT_EQ(result.error_code, 1062U);
}

TEST(orm_bounded_stream_honors_deadline_before_submitting_io)
{
    recording_session_client client;
    orm::database_session session{client, orm::sql_dialect::mysql};
    std::size_t seen = 0;
    auto handler = [&seen](const routed_order&) -> cnetmod::task<
                                                    std::expected<void, std::string>>
    {
        ++seen;
        co_return std::expected<void, std::string>{};
    };
    auto options = orm::stream_options{
        .batch_size = 2,
        .max_rows = 10,
        .deadline = std::chrono::steady_clock::now() - std::chrono::seconds{1}};
    auto result = cnetmod::sync_wait(
        session.for_each<routed_order>({}, handler, options));
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(seen, 0U);
    ASSERT_TRUE(client.statements.empty());
}

TEST(orm_projection_stream_honors_backpressure_cancellation_and_deadline)
{
    recording_session_client client;
    client.responses.push_back(routed_order_result(
        {{1, "one"}, {2, "two"}}));
    orm::database_session session{client, orm::sql_dialect::mysql};
    orm::query_wrapper<routed_order> query;
    query.select("id", "description").order_by_asc("id");
    cnetmod::cancel_token cancellation;
    std::size_t seen{};
    auto result = cnetmod::sync_wait(session.for_each_map<routed_order>(query,
        [&seen, &cancellation](const orm::projection_row&)
            -> cnetmod::task<std::expected<void, std::string>>
        {
            ++seen;
            cancellation.cancel();
            co_return std::expected<void, std::string>{};
        },
        orm::stream_options{.batch_size = 2, .max_rows = 10}, cancellation));
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(seen, 1U);
    ASSERT_EQ(client.statements.size(), 1U);

    recording_session_client deadline_client;
    orm::database_session deadline_session{
        deadline_client, orm::sql_dialect::mysql};
    auto expired = cnetmod::sync_wait(
        deadline_session.for_each_map<routed_order>(query,
            [](const orm::projection_row&)
                -> cnetmod::task<std::expected<void, std::string>>
            {
                co_return std::expected<void, std::string>{};
            },
            orm::stream_options{
                .batch_size = 2,
                .max_rows = 10,
                .deadline = std::chrono::steady_clock::now() -
                    std::chrono::milliseconds{1}}));
    ASSERT_FALSE(expired.has_value());
    ASSERT_TRUE(deadline_client.statements.empty());
}

TEST(orm_cursor_is_stateful_bounded_and_cancellable)
{
    recording_session_client client;
    client.responses.push_back(routed_order_result({{1, "one"}}));
    client.responses.push_back(routed_order_result({}));
    orm::database_session session{client, orm::sql_dialect::mysql};
    auto cursor = session.open_cursor<routed_order>({},
        orm::cursor_options{.batch_size = 1, .max_rows = 3});
    auto first = cnetmod::sync_wait(cursor.next());
    ASSERT_TRUE(first.ok());
    ASSERT_EQ(first.data.size(), 1U);
    ASSERT_FALSE(cursor.done());
    auto second = cnetmod::sync_wait(cursor.next());
    ASSERT_TRUE(second.ok());
    ASSERT_TRUE(second.data.empty());
    ASSERT_TRUE(cursor.done());

    cnetmod::cancel_token cancellation;
    cancellation.cancel();
    auto cancelled = cnetmod::sync_wait(cursor.next(cancellation));
    ASSERT_TRUE(cancelled.is_err());
    ASSERT_EQ(cancelled.framework_error,
        std::make_error_code(std::errc::operation_canceled));
}

TEST(mysql_orm_cursor_submits_once_and_reads_with_backpressure)
{
    streaming_session_client client;
    client.stream_batches.push_back({mysql_order_row(1, "one")});
    client.stream_batches.push_back({mysql_order_row(2, "two")});
    orm::database_session session{client, orm::sql_dialect::mysql};
    orm::query_wrapper<routed_order> query;
    query.ge("id", 1);
    auto cursor = orm::open_mysql_stream_cursor<routed_order>(session, query,
        orm::cursor_options{.batch_size = 1, .max_rows = 10});

    auto first = cnetmod::sync_wait(cursor.next());
    ASSERT_TRUE(first.ok());
    ASSERT_EQ(first.data.size(), 1U);
    ASSERT_EQ(first.data.front().id, 1);
    ASSERT_EQ(client.stream_starts, 1U);
    ASSERT_EQ(client.stream_reads, 1U);
    ASSERT_FALSE(client.last_sql.contains("OFFSET"));

    auto second = cnetmod::sync_wait(cursor.next());
    ASSERT_TRUE(second.ok());
    ASSERT_EQ(second.error_msg, std::string{});
    ASSERT_EQ(second.data.size(), 1U);
    if (!second.data.empty())
        ASSERT_EQ(second.data.front().id, 2);
    ASSERT_EQ(client.stream_starts, 1U);
    ASSERT_EQ(client.stream_reads, 2U);

    auto eof = cnetmod::sync_wait(cursor.next());
    ASSERT_TRUE(eof.ok());
    ASSERT_TRUE(eof.data.empty());
    ASSERT_TRUE(cursor.done());
    ASSERT_EQ(client.stream_reads, 3U);
    ASSERT_TRUE(client.open);
}

TEST(mysql_orm_cursor_invalidates_unfinished_connection_on_cancellation)
{
    streaming_session_client client;
    client.stream_batches.push_back({mysql_order_row(1, "one")});
    client.stream_batches.push_back({mysql_order_row(2, "two")});
    orm::database_session session{client, orm::sql_dialect::mysql};
    auto cursor = orm::open_mysql_stream_cursor<routed_order>(session, {},
        orm::cursor_options{.batch_size = 1, .max_rows = 10});

    auto first = cnetmod::sync_wait(cursor.next());
    ASSERT_TRUE(first.ok());
    cnetmod::cancel_token cancellation;
    cancellation.cancel();
    auto cancelled = cnetmod::sync_wait(cursor.next(cancellation));
    ASSERT_TRUE(cancelled.is_err());
    ASSERT_EQ(cancelled.framework_error,
        std::make_error_code(std::errc::operation_canceled));
    ASSERT_FALSE(client.open);
    ASSERT_TRUE(cursor.done());
}

TEST(mysql_orm_cursor_honors_deadline_before_protocol_submission)
{
    streaming_session_client client;
    orm::database_session session{client, orm::sql_dialect::mysql};
    auto cursor = orm::open_mysql_stream_cursor<routed_order>(session, {},
        orm::cursor_options{
            .batch_size = 1,
            .max_rows = 10,
            .deadline = std::chrono::steady_clock::now() -
                std::chrono::milliseconds{1}});

    auto expired = cnetmod::sync_wait(cursor.next());
    ASSERT_TRUE(expired.is_err());
    ASSERT_EQ(expired.framework_error,
        std::make_error_code(std::errc::timed_out));
    ASSERT_EQ(client.stream_starts, 0U);
    ASSERT_TRUE(client.open);
}

TEST(mysql_repository_streams_through_one_managed_lease)
{
    streaming_session_client client;
    client.stream_batches.push_back({mysql_order_row(1, "one")});
    client.stream_batches.push_back({mysql_order_row(2, "two")});
    using session_type = orm::database_session<streaming_session_client>;
    using gateway_type = orm::session_gateway<streaming_session_client,
        std::monostate, session_type>;
    gateway_type gateway{
        orm::sql_dialect::mysql,
        []() -> cnetmod::task<std::expected<void, std::string>>
        {
            co_return std::expected<void, std::string>{};
        },
        []() -> cnetmod::task<std::expected<std::monostate, std::string>>
        {
            co_return std::monostate{};
        },
        [&client](std::monostate&) -> streaming_session_client&
        {
            return client;
        }};
    orm::repository<routed_order, gateway_type, orm::mysql_stream_strategy>
        orders{gateway};
    std::vector<std::int64_t> seen;
    auto result = cnetmod::sync_wait(orders.for_each({},
        [&seen](const routed_order& value)
            -> cnetmod::task<std::expected<void, std::string>>
        {
            seen.push_back(value.id);
            co_return std::expected<void, std::string>{};
        },
        orm::stream_options{.batch_size = 1, .max_rows = 10}));

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(seen.size(), 2U);
    ASSERT_EQ(client.stream_starts, 1U);
    ASSERT_FALSE(client.last_sql.contains("OFFSET"));
    ASSERT_TRUE(client.open);
}

TEST(orm_wrapper_builds_parameterized_subquery_predicates)
{
    orm::query_wrapper<routed_order> query;
    query.in_subquery("id", orm::subquery{"SELECT `order_id` FROM `order_members` WHERE `member_id` = {}", {orm::param_value::from_int(7)}});
    query.exists(orm::subquery{
        "SELECT 1 FROM `audit_log` WHERE `audit_log`.`order_id` = `orders`.`id`",
        {}});
    auto [sql, params] = query.build_select_sql(orm::sql_dialect::mysql);
    ASSERT_TRUE(sql.contains("`id` IN (SELECT `order_id`"));
    ASSERT_TRUE(sql.contains("EXISTS (SELECT 1 FROM `audit_log`"));
    ASSERT_EQ(params.size(), 1U);
}

TEST(orm_wrapper_rebases_postgresql_subquery_parameters)
{
    orm::query_wrapper<routed_order> query;
    query.eq("description", "active");
    query.in_subquery("id", orm::subquery{
        "SELECT order_id FROM members WHERE member_id = {}",
        {orm::param_value::from_int(7)}});
    query.gt_subquery("id", orm::subquery{
        "SELECT MIN(order_id) FROM archived WHERE tenant_id = {}",
        {orm::param_value::from_int(9)}});
    query.ne("id", 11);
    auto [sql, params] = query.build_select_sql(orm::sql_dialect::postgresql);
    ASSERT_TRUE(sql.contains("member_id = $2"));
    ASSERT_TRUE(sql.contains("tenant_id = $3"));
    ASSERT_TRUE(sql.contains("!= $4"));
    ASSERT_EQ(params.size(), 4U);
}

TEST(orm_automatic_interceptor_chain_orders_tenant_and_logical_delete)
{
    orm::tenant_guard tenant{42};
    auto chain = orm::make_automatic_interceptor_chain<policy_order>();
    ASSERT_TRUE(chain);
    auto query = (*chain)->apply(orm::sql_operation::query,
        {"SELECT * FROM `policy_orders` WHERE `id` = {}",
            {orm::param_value::from_int(9)}});
    ASSERT_TRUE(query);
    ASSERT_TRUE(query->sql.contains("tenant_id"));
    ASSERT_TRUE(query->sql.contains("deleted"));
    ASSERT_EQ(query->parameters.size(), 2U);

    auto remove = (*chain)->apply(orm::sql_operation::remove,
        {"DELETE FROM `policy_orders` WHERE `id` = {}",
            {orm::param_value::from_int(9)}});
    ASSERT_TRUE(remove);
    ASSERT_TRUE(remove->sql.starts_with("UPDATE `policy_orders` SET"));
}

TEST(orm_session_can_install_automatic_interceptors_for_all_paths)
{
    recording_session_client client;
    orm::tenant_guard tenant{7};
    orm::database_session session{client, orm::sql_dialect::mysql};
    auto enabled = session.enable_automatic_interceptors<policy_order>();
    ASSERT_TRUE(enabled.has_value());

    auto rejected = cnetmod::sync_wait(session.execute(
        "UPDATE `policy_orders` SET `deleted` = 1"));
    ASSERT_TRUE(rejected.ok());
    ASSERT_TRUE(client.last_sql.contains("tenant_id"));
    ASSERT_TRUE(client.last_sql.contains("WHERE"));

    orm::database_session unsafe_session{client, orm::sql_dialect::mysql};
    auto safety_only = unsafe_session.enable_automatic_interceptors<policy_order>(
        orm::automatic_interceptor_options{
            .logical_delete = false, .multi_tenant = false, .sql_safety = true});
    ASSERT_TRUE(safety_only.has_value());
    auto unsafe = cnetmod::sync_wait(unsafe_session.execute(
        "UPDATE `policy_orders` SET `deleted` = 1"));
    ASSERT_TRUE(unsafe.is_err());
    ASSERT_TRUE(unsafe.error_msg.contains("unbounded UPDATE/DELETE"));

    client.response = routed_order_result({{1, "ok"}});
    orm::query_wrapper<policy_order> query;
    query.eq("id", 1);
    auto selected = cnetmod::sync_wait(session.find(query));
    ASSERT_TRUE(selected.ok());
    ASSERT_TRUE(client.last_sql.contains("tenant_id"));
    ASSERT_TRUE(client.last_sql.contains("deleted"));
}

TEST(orm_update_applies_optimistic_version_predicate_and_increment)
{
    recording_session_client client;
    client.response.affected_rows = 1;
    orm::database_session session{client, orm::sql_dialect::mysql};
    versioned_order model{7, 3, "changed"};
    auto result = cnetmod::sync_wait(session.update(model));
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(client.last_sql.contains("`version` = `version` + 1"));
    ASSERT_TRUE(client.last_sql.contains("AND `version` ="));
    ASSERT_EQ(model.version, 4);

    client.response = {};
    auto conflict = cnetmod::sync_wait(session.update(model));
    ASSERT_TRUE(conflict.is_err());
    ASSERT_EQ(conflict.framework_error,
        std::make_error_code(std::errc::state_not_recoverable));
}

TEST(orm_automatic_pipeline_controls_model_stage_policies)
{
    recording_session_client client;
    client.response.affected_rows = 1;

    orm::database_session disabled{client, orm::sql_dialect::mysql};
    auto disabled_result = disabled.enable_automatic_interceptors<filled_order>(
        orm::automatic_interceptor_options{
            .logical_delete = false,
            .multi_tenant = false,
            .sql_safety = true,
            .field_fill = false,
            .optimistic_lock = false});
    ASSERT_TRUE(disabled_result.has_value());
    filled_order without_fill{.id = 1};
    auto inserted_without_fill = cnetmod::sync_wait(disabled.insert(without_fill));
    ASSERT_TRUE(inserted_without_fill.ok());
    ASSERT_EQ(without_fill.created_at.year, 0U);

    orm::database_session enabled{client, orm::sql_dialect::mysql};
    auto enabled_result = enabled.enable_automatic_interceptors<filled_order>();
    ASSERT_TRUE(enabled_result.has_value());
    filled_order with_fill{.id = 2};
    auto inserted_with_fill = cnetmod::sync_wait(enabled.insert(with_fill));
    ASSERT_TRUE(inserted_with_fill.ok());
    ASSERT_TRUE(with_fill.created_at.year > 2000U);

    orm::database_session unlocked{client, orm::sql_dialect::mysql};
    auto unlocked_result = unlocked.enable_automatic_interceptors<versioned_order>(
        orm::automatic_interceptor_options{
            .logical_delete = false,
            .multi_tenant = false,
            .sql_safety = true,
            .field_fill = false,
            .optimistic_lock = false});
    ASSERT_TRUE(unlocked_result.has_value());
    versioned_order model{7, 3, "changed"};
    auto updated = cnetmod::sync_wait(unlocked.update(model));
    ASSERT_TRUE(updated.ok());
    ASSERT_FALSE(client.last_sql.contains("`version` = `version` + 1"));
    ASSERT_FALSE(client.last_sql.contains("AND `version` ="));
    ASSERT_EQ(model.version, 3);

    client.response = {};
    auto zero_rows = cnetmod::sync_wait(unlocked.update(model));
    ASSERT_TRUE(zero_rows.ok());
}

RUN_TESTS()
