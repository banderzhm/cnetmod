#include "test_framework.hpp"
#include <cnetmod/orm.hpp>

import std;
import cnetmod.core.net_init;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.orm;
import cnetmod.orm.database_session;
import cnetmod.protocol.mysql;

namespace orm = cnetmod::orm;

struct live_scoped_order
{
    std::int64_t id{};
    std::int64_t tenant_id{};
    std::int64_t department_id{};
    std::string description;
};

CNETMOD_MODEL(live_scoped_order, "saas_scoped_orders",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(tenant_id, "tenant_id", bigint, TENANT_ID),
    CNETMOD_FIELD(department_id, "department_id", bigint, DATA_PARTITION),
    CNETMOD_FIELD(description, "description", varchar))

struct live_flag_delete
{
    std::int64_t id{};
    std::int64_t deleted{};
};

CNETMOD_MODEL(live_flag_delete, "saas_flag_delete_verify",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(deleted, "deleted", tinyint, LOGIC_DELETE))

struct live_time_delete
{
    std::int64_t id{};
    orm::calendar_datetime created_at{};
    orm::calendar_datetime updated_at{};
    std::optional<orm::calendar_datetime> deleted_at;
};

CNETMOD_MODEL(live_time_delete, "saas_time_delete_verify",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(created_at, "created_at", datetime, FILL_INSERT),
    CNETMOD_FIELD(updated_at, "updated_at", datetime, FILL_INSERT_UPDATE),
    CNETMOD_FIELD(deleted_at, "deleted_at", datetime, NULLABLE | LOGIC_DELETE))

struct live_system_timestamp
{
    std::int64_t id{};
    std::string name;
    orm::calendar_datetime created_at{};
    orm::calendar_datetime updated_at{};
};

CNETMOD_MODEL(live_system_timestamp, "fill_system_user_verify",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(created_at, "created_at", datetime, FILL_INSERT),
    CNETMOD_FIELD(updated_at, "updated_at", datetime, FILL_INSERT_UPDATE))

struct live_chart_timestamp
{
    std::int64_t id{};
    std::string title;
    orm::calendar_datetime created_at{};
    orm::calendar_datetime updated_at{};
};

CNETMOD_MODEL(live_chart_timestamp, "fill_chart_case_verify",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(title, "title", varchar),
    CNETMOD_FIELD(created_at, "created_at", datetime, FILL_INSERT),
    CNETMOD_FIELD(updated_at, "updated_at", datetime, FILL_INSERT_UPDATE))

struct live_chat_timestamp
{
    std::int64_t id{};
    std::string title;
    orm::calendar_datetime last_message_at{};
    orm::calendar_datetime created_at{};
    orm::calendar_datetime updated_at{};
};

CNETMOD_MODEL(live_chat_timestamp, "fill_chat_session_verify",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(title, "title", varchar),
    CNETMOD_FIELD(last_message_at, "last_message_at", datetime),
    CNETMOD_FIELD(created_at, "created_at", datetime, FILL_INSERT),
    CNETMOD_FIELD(updated_at, "updated_at", datetime, FILL_INSERT_UPDATE))

TEST(mysql_live_system_chart_chat_timestamp_fill)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::mysql::client client{*io};
    auto run = [&]() -> cnetmod::task<void>
    {
        cnetmod::mysql::connect_options connection;
        connection.host = "127.0.0.1";
        connection.username = std::getenv("CNETMOD_MYSQL_TENANT_TEST_USER");
        connection.password = std::getenv("CNETMOD_MYSQL_TENANT_TEST_PASSWORD")
            ? std::getenv("CNETMOD_MYSQL_TENANT_TEST_PASSWORD") : "";
        connection.database = std::getenv("CNETMOD_MYSQL_TENANT_TEST_DATABASE");
        ASSERT_FALSE((co_await client.connect(std::move(connection))).is_err());
        for (const auto sql : {
                 "CREATE TABLE IF NOT EXISTS fill_system_user_verify ("
                 "id BIGINT PRIMARY KEY, name VARCHAR(64) NOT NULL, "
                 "created_at DATETIME NOT NULL, updated_at DATETIME NOT NULL)",
                 "CREATE TABLE IF NOT EXISTS fill_chart_case_verify ("
                 "id BIGINT PRIMARY KEY, title VARCHAR(64) NOT NULL, "
                 "created_at DATETIME NOT NULL, updated_at DATETIME NOT NULL)",
                 "CREATE TABLE IF NOT EXISTS fill_chat_session_verify ("
                 "id BIGINT PRIMARY KEY, title VARCHAR(64) NOT NULL, "
                 "last_message_at DATETIME NOT NULL, created_at DATETIME NOT NULL, "
                 "updated_at DATETIME NOT NULL)",
                 "DELETE FROM fill_system_user_verify",
                 "DELETE FROM fill_chart_case_verify",
                 "DELETE FROM fill_chat_session_verify"})
            ASSERT_FALSE((co_await client.query(sql)).is_err());

        orm::database_session session{client, orm::sql_dialect::mysql};
        orm::mapper<live_system_timestamp, decltype(session)> users{session};
        orm::mapper<live_chart_timestamp, decltype(session)> charts{session};
        orm::mapper<live_chat_timestamp, decltype(session)> chats{session};
        ASSERT_TRUE(users.configure().has_value());
        ASSERT_TRUE(charts.configure().has_value());
        ASSERT_TRUE(chats.configure().has_value());

        live_system_timestamp user{.id = 1, .name = "created"};
        ASSERT_TRUE((co_await users.insert(user)).ok());
        ASSERT_TRUE(user.created_at.year > 2000);
        ASSERT_EQ(user.created_at.to_string(), user.updated_at.to_string());
        const auto original_created = user.created_at.to_string();
        user.name = "updated";
        user.updated_at = {2000, 1, 1, 0, 0, 0, 0};
        ASSERT_TRUE((co_await users.update_by_id(user)).ok());
        auto stored_user = co_await users.select_by_id(orm::param_value::from_int(1));
        ASSERT_TRUE(stored_user.ok());
        ASSERT_EQ(stored_user.data.front().created_at.to_string(), original_created);
        ASSERT_TRUE(stored_user.data.front().updated_at.year > 2000);
        const live_system_timestamp const_user{.id = 1, .name = "const update",
            .created_at = {1999, 1, 1, 0, 0, 0, 0},
            .updated_at = {2000, 1, 1, 0, 0, 0, 0}};
        ASSERT_TRUE((co_await users.update_by_id(const_user)).ok());
        stored_user = co_await users.select_by_id(orm::param_value::from_int(1));
        ASSERT_TRUE(stored_user.ok());
        ASSERT_EQ(stored_user.data.front().name, "const update");
        ASSERT_EQ(stored_user.data.front().created_at.to_string(), original_created);
        ASSERT_TRUE(stored_user.data.front().updated_at.year > 2000);
        ASSERT_EQ(const_user.updated_at.year, 2000);

        live_chart_timestamp chart{.id = 1, .title = "first",
            .created_at = {2010, 1, 2, 3, 4, 5, 0}};
        ASSERT_TRUE((co_await charts.upsert(chart)).ok());
        ASSERT_EQ(chart.created_at.year, 2010);
        live_chart_timestamp changed{.id = 1, .title = "second",
            .created_at = {2020, 1, 2, 3, 4, 5, 0}};
        ASSERT_TRUE((co_await charts.upsert(changed)).ok());
        auto stored_chart = co_await charts.select_by_id(orm::param_value::from_int(1));
        ASSERT_TRUE(stored_chart.ok());
        ASSERT_EQ(stored_chart.data.front().title, "second");
        ASSERT_EQ(stored_chart.data.front().created_at.year, 2010);

        orm::mapper_registry archive_xml;
        ASSERT_TRUE(archive_xml.load_xml(R"xml(
            <mapper namespace="FillArchiveLive">
              <insert id="upsert">
                INSERT INTO fill_chart_case_verify (id, title, created_at, updated_at)
                VALUES (#{id}, #{title}, #{created_at}, #{updated_at})
                ON DUPLICATE KEY UPDATE title = VALUES(title),
                  updated_at = VALUES(updated_at)
              </insert>
            </mapper>)xml").has_value());
        orm::param_context archive_values;
        archive_values.set("id", std::int64_t{2});
        archive_values.set("title", std::string{"original"});
        archive_values.set("created_at", orm::calendar_datetime{2001, 1, 2, 3, 4, 5, 0});
        archive_values.set("updated_at", orm::calendar_datetime{2001, 1, 2, 3, 4, 5, 0});
        ASSERT_TRUE((co_await charts.execute_xml(
            archive_xml, "FillArchiveLive.upsert", archive_values)).ok());
        archive_values.set("title", std::string{"replaced"});
        archive_values.set("created_at", orm::calendar_datetime{2022, 1, 2, 3, 4, 5, 0});
        archive_values.set("updated_at", orm::calendar_datetime{2022, 1, 2, 3, 4, 5, 0});
        ASSERT_TRUE((co_await charts.execute_xml(
            archive_xml, "FillArchiveLive.upsert", archive_values)).ok());
        auto stored_archive = co_await charts.select_by_id(orm::param_value::from_int(2));
        ASSERT_TRUE(stored_archive.ok());
        ASSERT_EQ(stored_archive.data.front().title, "replaced");
        ASSERT_EQ(stored_archive.data.front().created_at.year, 2001);
        ASSERT_EQ(stored_archive.data.front().updated_at.year, 2022);

        live_chat_timestamp chat{.id = 1, .title = "first",
            .last_message_at = {2020, 1, 2, 3, 4, 5, 0}};
        ASSERT_TRUE((co_await chats.insert(chat)).ok());
        orm::update_wrapper<live_chat_timestamp> rename;
        rename.set(&live_chat_timestamp::title, "second")
            .eq(&live_chat_timestamp::id, 1);
        ASSERT_TRUE((co_await chats.update(rename)).ok());
        auto stored_chat = co_await chats.select_by_id(orm::param_value::from_int(1));
        ASSERT_TRUE(stored_chat.ok());
        ASSERT_TRUE(stored_chat.data.front().updated_at.year > 2000);

        const orm::calendar_datetime event_time{2021, 2, 3, 4, 5, 6, 0};
        orm::update_wrapper<live_chat_timestamp> touch;
        touch.set(&live_chat_timestamp::last_message_at, event_time)
            .set(&live_chat_timestamp::updated_at, event_time)
            .eq(&live_chat_timestamp::id, 1);
        ASSERT_TRUE((co_await chats.update(touch)).ok());
        stored_chat = co_await chats.select_by_id(orm::param_value::from_int(1));
        ASSERT_TRUE(stored_chat.ok());
        ASSERT_EQ(stored_chat.data.front().last_message_at.to_string(),
            stored_chat.data.front().updated_at.to_string());
        ASSERT_EQ(stored_chat.data.front().updated_at.year, 2021);

        co_await client.quit();
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(operation.handle().done());
    operation.handle().promise().result();
}

TEST(mysql_live_mixed_logical_delete_and_timestamp_fill)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::mysql::client client{*io};
    auto run = [&]() -> cnetmod::task<void>
    {
        cnetmod::mysql::connect_options connection;
        connection.host = "127.0.0.1";
        connection.username = std::getenv("CNETMOD_MYSQL_TENANT_TEST_USER");
        connection.password = std::getenv("CNETMOD_MYSQL_TENANT_TEST_PASSWORD")
            ? std::getenv("CNETMOD_MYSQL_TENANT_TEST_PASSWORD") : "";
        connection.database = std::getenv("CNETMOD_MYSQL_TENANT_TEST_DATABASE");
        ASSERT_FALSE((co_await client.connect(std::move(connection))).is_err());
        ASSERT_FALSE((co_await client.query(
            "CREATE TABLE IF NOT EXISTS saas_flag_delete_verify ("
            "id BIGINT PRIMARY KEY, deleted TINYINT NOT NULL DEFAULT 0)"))
            .is_err());
        ASSERT_FALSE((co_await client.query(
            "CREATE TABLE IF NOT EXISTS saas_time_delete_verify ("
            "id BIGINT PRIMARY KEY, created_at DATETIME NOT NULL, "
            "updated_at DATETIME NOT NULL, deleted_at DATETIME NULL)"))
            .is_err());
        ASSERT_FALSE((co_await client.query("DELETE FROM saas_flag_delete_verify"))
            .is_err());
        ASSERT_FALSE((co_await client.query("DELETE FROM saas_time_delete_verify"))
            .is_err());
        ASSERT_FALSE((co_await client.query(
            "INSERT INTO saas_flag_delete_verify VALUES (1,0),(2,1)"))
            .is_err());

        orm::database_session flag_session{client, orm::sql_dialect::mysql};
        orm::mapper<live_flag_delete, decltype(flag_session)> flags{flag_session};
        ASSERT_TRUE(flags.configure().has_value());
        auto visible_flags = co_await flags.select_list();
        ASSERT_TRUE(visible_flags.ok());
        ASSERT_EQ(visible_flags.data.size(), 1U);
        auto flag_removed = co_await flags.remove_by_id(orm::param_value::from_int(1));
        ASSERT_TRUE(flag_removed.ok());
        auto flag_storage = co_await client.query(
            "SELECT deleted FROM saas_flag_delete_verify WHERE id = 1");
        ASSERT_FALSE(flag_storage.is_err());
        ASSERT_EQ(flag_storage.rows[0][0].to_string(), "1");

        orm::logical_delete_config time_policy;
        time_policy.field_name = "deleted_at";
        time_policy.mode = orm::logical_delete_mode::nullable_datetime;
        time_policy.touch_fields = {{"updated_at",
            orm::logical_delete_touch_value::current_timestamp}};
        orm::automatic_interceptor_options options;
        options.logical_delete_policy = time_policy;
        orm::database_session time_session{client, orm::sql_dialect::mysql};
        orm::mapper<live_time_delete, decltype(time_session)> times{time_session};
        ASSERT_TRUE(times.configure(options).has_value());
        live_time_delete inserted{.id = 1};
        auto saved = co_await times.insert(inserted);
        ASSERT_TRUE(saved.ok());
        ASSERT_TRUE(inserted.created_at.year > 2000);
        ASSERT_EQ(inserted.created_at.to_string(), inserted.updated_at.to_string());
        auto visible_time = co_await times.select_list();
        ASSERT_TRUE(visible_time.ok());
        ASSERT_EQ(visible_time.data.size(), 1U);
        orm::mapper_registry xml;
        ASSERT_TRUE(xml.load_xml(R"xml(
            <mapper namespace="MixedDeleteLive">
              <select id="times" resultType="live_time_delete">
                SELECT s.id, s.created_at, s.updated_at, s.deleted_at
                FROM saas_time_delete_verify s
                WHERE s.id &gt;= #{minimum}
              </select>
            </mapper>)xml").has_value());
        orm::param_context xml_parameters;
        xml_parameters.set("minimum", std::int64_t{1});
        auto xml_visible = co_await times.select_xml(
            xml, "MixedDeleteLive.times", xml_parameters);
        ASSERT_TRUE(xml_visible.ok());
        ASSERT_EQ(xml_visible.data.size(), 1U);
        auto time_removed = co_await times.remove_by_id(orm::param_value::from_int(1));
        ASSERT_TRUE(time_removed.ok());
        auto hidden = co_await times.select_list();
        ASSERT_TRUE(hidden.ok());
        ASSERT_TRUE(hidden.data.empty());
        auto xml_hidden = co_await times.select_xml(
            xml, "MixedDeleteLive.times", xml_parameters);
        ASSERT_TRUE(xml_hidden.ok());
        ASSERT_TRUE(xml_hidden.data.empty());
        auto time_storage = co_await client.query(
            "SELECT deleted_at IS NOT NULL FROM saas_time_delete_verify WHERE id = 1");
        ASSERT_FALSE(time_storage.is_err());
        ASSERT_EQ(time_storage.rows[0][0].to_string(), "1");

        co_await client.quit();
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(operation.handle().done());
    operation.handle().promise().result();
}

TEST(mysql_live_tenant_and_organization_scopes_cover_typed_and_xml)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::mysql::client client{*io};
    auto run = [&]() -> cnetmod::task<void>
    {
        cnetmod::mysql::connect_options connection;
        connection.host = "127.0.0.1";
        connection.username = std::getenv("CNETMOD_MYSQL_TENANT_TEST_USER");
        connection.password = std::getenv("CNETMOD_MYSQL_TENANT_TEST_PASSWORD")
            ? std::getenv("CNETMOD_MYSQL_TENANT_TEST_PASSWORD") : "";
        connection.database = std::getenv("CNETMOD_MYSQL_TENANT_TEST_DATABASE");
        auto connected = co_await client.connect(std::move(connection));
        ASSERT_FALSE(connected.is_err());
        if (connected.is_err())
        {
            io->stop();
            co_return;
        }

        auto schema = co_await client.query(
            "CREATE TABLE IF NOT EXISTS saas_scoped_orders ("
            "id BIGINT PRIMARY KEY, tenant_id BIGINT NOT NULL, "
            "department_id BIGINT NOT NULL, description VARCHAR(100) NOT NULL)");
        ASSERT_FALSE(schema.is_err());
        auto cleared = co_await client.query("DELETE FROM saas_scoped_orders");
        ASSERT_FALSE(cleared.is_err());
        auto fixture = co_await client.query(
            "INSERT INTO saas_scoped_orders "
            "(id, tenant_id, department_id, description) VALUES "
            "(1,10,100,'match'),(2,11,101,'match'),"
            "(3,12,102,'match'),(4,20,101,'match'),"
            "(5,11,999,'match'),(6,10,100,'other')");
        ASSERT_FALSE(fixture.is_err());

        orm::mapper_registry xml;
        auto loaded = xml.load_xml(R"xml(
            <mapper namespace="TenantLive">
              <select id="search" resultType="live_scoped_order">
                SELECT id, tenant_id, department_id, description
                FROM saas_scoped_orders
                WHERE description = #{first} OR description = #{second}
                ORDER BY id LIMIT #{limit}
              </select>
              <select id="searchIds" resultType="live_scoped_order">
                select s.id, s.tenant_id, s.department_id, s.description
                FROM saas_scoped_orders AS s
                <where>
                  <if test="description != null">
                    s.description = #{description} OR
                  </if>
                  s.id IN
                  <foreach collection="ids" item="entry"
                           open="(" close=")" separator=",">
                    #{entry.value}
                  </foreach>
                </where>
                ORDER BY s.id
              </select>
              <update id="rename">
                UPDATE saas_scoped_orders SET description = #{value}
                WHERE id = #{first} OR id = #{second}
              </update>
              <update id="move">
                UPDATE saas_scoped_orders SET department_id = #{department}
                WHERE id = #{id}
              </update>
              <insert id="add">
                INSERT INTO saas_scoped_orders
                  (id, tenant_id, department_id, description)
                VALUES (#{id}, #{tenant}, #{department}, #{value})
              </insert>
              <delete id="remove">
                DELETE FROM saas_scoped_orders WHERE id = #{first} OR id = #{second}
              </delete>
            </mapper>)xml");
        ASSERT_TRUE(loaded.has_value());

        auto tenant = std::make_shared<const orm::tenant_scope>(
            orm::tenant_scope{10, {10, 11, 12}, {10, 11}});
        auto departments = std::make_shared<const orm::data_permission_scope>(
            orm::data_permission_scope{.partition_ids = {100, 101}});
        orm::automatic_interceptor_options policy;
        policy.tenant_scope_required = true;
        policy.tenant = tenant;
        policy.data_permission = departments;
        orm::database_session session{client, orm::sql_dialect::mysql};
        orm::mapper<live_scoped_order, decltype(session)> orders{session};
        ASSERT_TRUE(orders.configure(policy).has_value());

        auto typed = co_await orders.select_list();
        ASSERT_TRUE(typed.ok());
        ASSERT_EQ(typed.data.size(), 3U);
        auto counted = co_await orders.count();
        ASSERT_TRUE(counted.ok());
        ASSERT_EQ(counted.data.size(), 1U);
        if (!counted.data.empty())
            ASSERT_EQ(counted.data.front(), 3);
        auto page = co_await orders.select_page(1, 2);
        ASSERT_TRUE(page.ok());
        ASSERT_EQ(page.total, 3U);
        ASSERT_EQ(page.records.data.size(), 2U);
        auto hidden = co_await orders.select_by_id(orm::param_value::from_int(4));
        ASSERT_TRUE(hidden.ok());
        ASSERT_TRUE(hidden.data.empty());

        orm::param_context search;
        search.set("first", std::string{"match"});
        search.set("second", std::string{"other"});
        search.set("limit", std::int64_t{20});
        auto found = co_await orders.select_xml(xml, "TenantLive.search", search);
        ASSERT_TRUE(found.ok());
        ASSERT_EQ(found.data.size(), 3U);
        if (found.data.size() == 3)
        {
            ASSERT_EQ(found.data[0].id, 1);
            ASSERT_EQ(found.data[1].id, 2);
            ASSERT_EQ(found.data[2].id, 6);
        }
        orm::param_context dynamic;
        dynamic.set("description", std::string{"other"});
        std::vector<orm::param_context> ids;
        for (const auto id : {1, 4, 5})
        {
            orm::param_context item;
            item.set("value", static_cast<std::int64_t>(id));
            ids.push_back(std::move(item));
        }
        dynamic.add_collection("ids", std::move(ids));
        auto dynamic_result = co_await orders.select_xml(
            xml, "TenantLive.searchIds", dynamic);
        ASSERT_TRUE(dynamic_result.ok());
        ASSERT_EQ(dynamic_result.data.size(), 2U);
        if (dynamic_result.data.size() == 2)
        {
            ASSERT_EQ(dynamic_result.data[0].id, 1);
            ASSERT_EQ(dynamic_result.data[1].id, 6);
        }

        orm::param_context rename;
        rename.set("value", std::string{"renamed"});
        rename.set("first", std::int64_t{1});
        rename.set("second", std::int64_t{4});
        auto renamed = co_await orders.execute_xml(xml, "TenantLive.rename", rename);
        ASSERT_TRUE(renamed.ok());
        ASSERT_EQ(renamed.error_msg, "");
        ASSERT_EQ(renamed.affected_rows, 1U);
        orm::param_context move;
        move.set("department", std::int64_t{999});
        move.set("id", std::int64_t{1});
        auto blocked_move = co_await orders.execute_xml(
            xml, "TenantLive.move", move);
        ASSERT_TRUE(blocked_move.is_err());

        live_scoped_order typed_update{6, 10, 100, "typed"};
        auto updated = co_await orders.update_by_id(typed_update);
        ASSERT_TRUE(updated.ok());

        live_scoped_order foreign{7, 20, 100, "forbidden"};
        auto rejected = co_await orders.insert(foreign);
        ASSERT_TRUE(rejected.is_err());
        live_scoped_order wrong_department{7, 10, 999, "forbidden"};
        auto rejected_department = co_await orders.insert(wrong_department);
        ASSERT_TRUE(rejected_department.is_err());
        live_scoped_order permitted{8, 11, 101, "new"};
        auto inserted = co_await orders.insert(permitted);
        ASSERT_TRUE(inserted.ok());

        orm::param_context xml_insert;
        xml_insert.set("id", std::int64_t{9});
        xml_insert.set("tenant", std::int64_t{20});
        xml_insert.set("department", std::int64_t{100});
        xml_insert.set("value", std::string{"forbidden"});
        auto bad_xml_insert = co_await orders.execute_xml(
            xml, "TenantLive.add", xml_insert);
        ASSERT_TRUE(bad_xml_insert.is_err());
        xml_insert.set("tenant", std::int64_t{10});
        xml_insert.set("department", std::int64_t{999});
        auto bad_xml_department = co_await orders.execute_xml(
            xml, "TenantLive.add", xml_insert);
        ASSERT_TRUE(bad_xml_department.is_err());
        xml_insert.set("department", std::int64_t{100});
        auto good_xml_insert = co_await orders.execute_xml(
            xml, "TenantLive.add", xml_insert);
        ASSERT_TRUE(good_xml_insert.ok());

        orm::data_permission_scope read_subtree_write_root;
        read_subtree_write_root.partition_ids = {100, 101};
        read_subtree_write_root.writable_partition_ids =
            std::vector<std::int64_t>{100};
        orm::automatic_interceptor_options narrower = policy;
        narrower.data_permission =
            std::make_shared<const orm::data_permission_scope>(
                read_subtree_write_root);
        orm::database_session narrow_session{client, orm::sql_dialect::mysql};
        orm::mapper<live_scoped_order, decltype(narrow_session)> narrow{
            narrow_session};
        ASSERT_TRUE(narrow.configure(narrower).has_value());
        auto readable_child = co_await narrow.select_by_id(
            orm::param_value::from_int(2));
        ASSERT_TRUE(readable_child.ok());
        ASSERT_EQ(readable_child.data.size(), 1U);
        live_scoped_order unwritable_child{2, 11, 101, "blocked"};
        auto blocked_update = co_await narrow.update_by_id(unwritable_child);
        ASSERT_TRUE(blocked_update.ok());
        ASSERT_EQ(blocked_update.affected_rows, 0U);

        orm::param_context remove;
        remove.set("first", std::int64_t{2});
        remove.set("second", std::int64_t{4});
        auto deleted = co_await orders.execute_xml(
            xml, "TenantLive.remove", remove);
        ASSERT_TRUE(deleted.ok());
        ASSERT_EQ(deleted.affected_rows, 1U);

        auto transaction = co_await session.begin_transaction();
        ASSERT_TRUE(transaction.has_value());
        live_scoped_order transient{10, 10, 100, "transaction"};
        auto transient_insert = co_await orders.insert(transient);
        ASSERT_TRUE(transient_insert.ok());
        auto rolled_back = co_await session.rollback_transaction();
        ASSERT_TRUE(rolled_back.has_value());
        auto absent = co_await orders.select_by_id(
            orm::param_value::from_int(10));
        ASSERT_TRUE(absent.ok());
        ASSERT_TRUE(absent.data.empty());

        auto direct = co_await client.query(
            "SELECT id, description FROM saas_scoped_orders WHERE id IN (1,2,4,7,8,9) ORDER BY id");
        ASSERT_FALSE(direct.is_err());
        ASSERT_EQ(direct.rows.size(), 4U);
        if (direct.rows.size() == 4)
        {
            ASSERT_EQ(direct.rows[0][1].to_string(), "renamed");
            ASSERT_EQ(direct.rows[1][0].to_string(), "4");
            ASSERT_EQ(direct.rows[2][0].to_string(), "8");
            ASSERT_EQ(direct.rows[3][0].to_string(), "9");
        }

        orm::database_session missing_session{client, orm::sql_dialect::mysql};
        orm::mapper<live_scoped_order, decltype(missing_session)> missing{
            missing_session};
        orm::automatic_interceptor_options required;
        required.tenant_scope_required = true;
        ASSERT_TRUE(missing.configure(required).has_value());
        auto no_context = co_await missing.select_list();
        ASSERT_TRUE(no_context.is_err());

        auto sibling_tenant = std::make_shared<const orm::tenant_scope>(
            orm::tenant_scope::self(20));
        auto sibling_departments =
            std::make_shared<const orm::data_permission_scope>(
                orm::data_permission_scope{.partition_ids = {101}});
        orm::database_session sibling_session{client, orm::sql_dialect::mysql};
        orm::mapper<live_scoped_order, decltype(sibling_session)> sibling{
            sibling_session};
        orm::automatic_interceptor_options sibling_policy;
        sibling_policy.tenant_scope_required = true;
        sibling_policy.tenant = sibling_tenant;
        sibling_policy.data_permission = sibling_departments;
        ASSERT_TRUE(sibling.configure(sibling_policy).has_value());
        auto sibling_rows = co_await sibling.select_list();
        ASSERT_TRUE(sibling_rows.ok());
        ASSERT_EQ(sibling_rows.data.size(), 1U);
        if (sibling_rows.data.size() == 1)
            ASSERT_EQ(sibling_rows.data[0].id, 4);

        orm::database_session standalone_session{client, orm::sql_dialect::mysql};
        orm::mapper<live_scoped_order, decltype(standalone_session)> standalone{
            standalone_session};
        orm::automatic_interceptor_options disabled;
        disabled.multi_tenant = false;
        ASSERT_TRUE(standalone.configure(disabled).has_value());
        auto full = co_await standalone.select_by_id(
            orm::param_value::from_int(4));
        ASSERT_TRUE(full.ok());
        ASSERT_EQ(full.data.size(), 1U);

        co_await client.quit();
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(operation.handle().done());
    operation.handle().promise().result();
}

int main()
{
    const auto* enabled = std::getenv("CNETMOD_MYSQL_TENANT_INTEGRATION");
    if (!enabled || std::string_view{enabled} != "1")
        return 77;
    if (!std::getenv("CNETMOD_MYSQL_TENANT_TEST_USER") ||
        !std::getenv("CNETMOD_MYSQL_TENANT_TEST_DATABASE"))
        return EXIT_FAILURE;
    return cnetmod::test::run_all();
}
