#include "test_framework.hpp"
#include <cnetmod/orm.hpp>

import std;
import nlohmann.json;
import cnetmod.orm;
import cnetmod.io.io_context;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.protocol.http.middleware.tracing;
import cnetmod.protocol.mysql;

namespace orm = cnetmod::orm;

static_assert(orm::asynchronous_database_client<cnetmod::mysql::client>);

struct orm_json_user
{
    std::int64_t id{};
    std::string name;
    std::int32_t status{};
};

CNETMOD_MODEL(orm_json_user, "users",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(status, "status", int_))

struct orm_crud_user
{
    std::int64_t id{};
    std::string name;
    std::int32_t status{};
};

CNETMOD_MODEL(orm_crud_user, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(status, "status", int_))

[[maybe_unused]] auto mysql_database_session_compile_probe(
    cnetmod::mysql::client& client) -> cnetmod::task<void>
{
    orm::database_session session{client, orm::sql_dialect::mysql};
    auto result = co_await session.find_by_id<orm_crud_user>(
        orm::param_value::from_int(7));
    static_cast<void>(result);
}

struct orm_json_team
{
    std::int64_t id{};
    std::string name;
};

CNETMOD_MODEL(orm_json_team, "teams",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(name, "name", varchar))

struct orm_json_role
{
    std::int64_t id{};
    std::string name;
};

CNETMOD_MODEL(orm_json_role, "roles",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(name, "name", varchar))

struct orm_json_user_graph
{
    std::int64_t id{};
    std::string name;
    std::optional<orm_json_team> team;
    std::vector<orm_json_role> roles;
};

CNETMOD_MODEL(orm_json_user_graph, "users",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(name, "name", varchar))

struct traced_database_client
{
    orm::sql_format_options format_options{};

    auto query(std::string_view) -> cnetmod::task<orm::query_result>
    {
        co_return orm::query_result{};
    }

    auto execute(std::string_view) -> cnetmod::task<orm::query_result>
    {
        co_return orm::query_result{};
    }

    auto execute(orm::parameterized_query) -> cnetmod::task<orm::query_result>
    {
        co_return orm::query_result{};
    }

    auto current_format_opts() const -> const orm::sql_format_options&
    {
        return format_options;
    }
};

auto orm_crud_user_rows(std::int64_t id = 7, std::string name = "Ada",
    std::int64_t status = 1) -> orm::query_result
{
    orm::query_result result;
    result.columns = {{.name = "id"}, {.name = "name"}, {.name = "status"}};
    result.rows = {{orm::field_value::from_int64(id),
        orm::field_value::from_string(std::move(name)),
        orm::field_value::from_int64(status)}};
    result.affected_rows = 1;
    return result;
}

struct mysql_style_orm_client
{
    orm::sql_format_options format_options{};
    std::string last_sql;

    auto query(std::string_view sql) -> cnetmod::task<orm::query_result>
    {
        last_sql = sql;
        co_return orm_crud_user_rows();
    }

    auto execute(std::string_view sql) -> cnetmod::task<orm::query_result>
    {
        last_sql = sql;
        if (sql.starts_with("SELECT"))
            co_return orm_crud_user_rows();
        orm::query_result result;
        result.affected_rows = 1;
        result.last_insert_id = 73;
        co_return result;
    }

    auto execute(orm::parameterized_query statement) -> cnetmod::task<orm::query_result>
    {
        auto formatted = orm::format_sql(format_options, statement.query, statement.args);
        if (!formatted)
        {
            orm::query_result error;
            error.error_msg = "failed to format parameterized SQL";
            co_return error;
        }
        co_return co_await execute(*formatted);
    }

    auto current_format_opts() const -> const orm::sql_format_options&
    {
        return format_options;
    }
};

struct postgresql_style_orm_client
{
    orm::sql_format_options format_options{};
    std::string last_sql;
    std::vector<orm::param_value> last_parameters;

    auto query(std::string_view sql) -> cnetmod::task<orm::query_result>
    {
        last_sql = sql;
        co_return orm_crud_user_rows();
    }

    auto execute(std::string_view sql) -> cnetmod::task<orm::query_result>
    {
        last_sql = sql;
        orm::query_result result;
        result.affected_rows = 1;
        co_return result;
    }

    auto execute(orm::parameterized_query statement) -> cnetmod::task<orm::query_result>
    {
        last_sql = statement.query;
        last_parameters = std::move(statement.args);
        if (last_sql.starts_with("SELECT") || last_sql.contains("RETURNING"))
            co_return orm_crud_user_rows(17, "Grace", 2);
        orm::query_result result;
        result.affected_rows = 1;
        co_return result;
    }

    auto current_format_opts() const -> const orm::sql_format_options&
    {
        return format_options;
    }
};

namespace cnetmod::orm {

template <> struct xml_object_graph_binder<::orm_json_user_graph>
{
    static void bind(::orm_json_user_graph& target, const mapped_object& source)
    {
        target.team = mapped_association_as<::orm_json_team>(source, "team");
        target.roles = mapped_collection_as<::orm_json_role>(source, "roles");
    }
};

} // namespace cnetmod::orm

TEST(orm_json_uses_model_metadata_without_nlohmann_macros)
{
    const orm_json_user original{42, "Ada", 1};
    const auto encoded = orm::to_json(original);
    ASSERT_EQ(encoded.at("id").get<std::int64_t>(), 42);
    ASSERT_EQ(encoded.at("name").get<std::string>(), "Ada");

    const auto decoded = orm::from_json<orm_json_user>(encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->id, 42);
    ASSERT_EQ(decoded->name, "Ada");
    ASSERT_EQ(decoded->status, 1);
}

TEST(xml_result_map_projects_scalar_object_graph_into_model_dto)
{
    orm::mapped_object object;
    // ResultMap properties commonly use member names, while a raw alias may
    // use the column spelling. Both must use the same CNETMOD_MODEL setter.
    object.values.emplace("id", orm::param_value::from_int(9));
    object.values.emplace("name", orm::param_value::from_string("Lin"));
    object.values.emplace("status", orm::param_value::from_int(3));
    const auto dto = orm::from_mapped_object<orm_json_user>(object);
    ASSERT_EQ(dto.id, 9);
    ASSERT_EQ(dto.name, "Lin");
    ASSERT_EQ(dto.status, 3);

    std::vector<orm::mapped_object> graph;
    graph.push_back(std::move(object));
    const auto rows = orm::from_mapped_objects<orm_json_user>(graph);
    ASSERT_EQ(rows.size(), 1U);
    ASSERT_EQ(rows.front().name, "Lin");
}

TEST(xml_result_map_projects_explicit_typed_associations_and_collections)
{
    orm::mapped_object source;
    source.values.emplace("id", orm::param_value::from_int(9));
    source.values.emplace("name", orm::param_value::from_string("Lin"));
    source.associations["team"].values.emplace("id", orm::param_value::from_int(2));
    source.associations["team"].values.emplace("name",
        orm::param_value::from_string("Core"));
    source.collections["roles"] = {
        {.values = {{"id", orm::param_value::from_int(1)},
             {"name", orm::param_value::from_string("admin")}}},
        {.values = {{"id", orm::param_value::from_int(2)},
             {"name", orm::param_value::from_string("editor")}}},
    };

    const auto dto = orm::from_mapped_object<orm_json_user_graph>(source);
    ASSERT_EQ(dto.id, 9);
    ASSERT_TRUE(dto.team.has_value());
    ASSERT_EQ(dto.team->name, "Core");
    ASSERT_EQ(dto.roles.size(), 2U);
    ASSERT_EQ(dto.roles.at(1).name, "editor");
}

TEST(xml_mapper_registry_loads_namespaced_result_map)
{
    orm::mapper_registry registry;
    const auto loaded = registry.load_xml(R"(
        <mapper namespace="UserMapper">
          <resultMap id="UserMap" type="User" autoMapping="false">
            <id property="id" column="user_id" jdbcType="BIGINT"/>
            <result property="name" column="display_name" jdbcType="VARCHAR"/>
            <association property="team" column="team_id" resultMap="TeamMap"/>
            <collection property="roles" column="user_id" resultMap="RoleMap" ofType="Role"/>
            <collection property="orders" column="user_id" select="findOrdersByUser" resultMap="OrderMap"/>
          </resultMap>
          <resultMap id="TeamMap" type="Team">
            <id property="id" column="team_id"/>
            <result property="name" column="team_name"/>
          </resultMap>
          <resultMap id="RoleMap" type="Role">
            <id property="id" column="role_id"/>
            <result property="name" column="role_name"/>
          </resultMap>
          <resultMap id="OrderMap" type="Order">
            <id property="id" column="order_id"/>
          </resultMap>
          <select id="findById" resultMap="UserMap">SELECT 1</select>
          <select id="findOrdersByUser" resultMap="OrderMap">SELECT 1</select>
        </mapper>)");
    ASSERT_TRUE(loaded.has_value());

    const auto* map = registry.find_result_map("UserMapper.UserMap");
    ASSERT_TRUE(map != nullptr);
    ASSERT_FALSE(map->auto_mapping);
    ASSERT_EQ(map->id_mappings.size(), 1U);
    ASSERT_EQ(map->result_mappings.size(), 1U);
    ASSERT_EQ(map->associations.size(), 1U);
    ASSERT_EQ(map->collections.size(), 2U);
    ASSERT_EQ(map->collections.at(1).select, "findOrdersByUser");
    ASSERT_EQ(map->find_by_column("display_name")->property, "name");

    orm::result_set joined;
    joined.columns = {{.name = "user_id"}, {.name = "display_name"},
        {.name = "team_id"}, {.name = "team_name"},
        {.name = "role_id"}, {.name = "role_name"}};
    joined.rows = {
        {orm::field_value::from_int64(7), orm::field_value::from_string("Ada"),
            orm::field_value::from_int64(3), orm::field_value::from_string("Core"),
            orm::field_value::from_int64(1), orm::field_value::from_string("admin")},
        {orm::field_value::from_int64(7), orm::field_value::from_string("Ada"),
            orm::field_value::from_int64(3), orm::field_value::from_string("Core"),
            orm::field_value::from_int64(2), orm::field_value::from_string("editor")},
    };
    const auto* maps = registry.result_maps("UserMapper");
    ASSERT_TRUE(maps != nullptr);
    const auto graph = orm::result_map_applier::materialize_joined(*map, joined, *maps);
    ASSERT_EQ(graph.size(), 1U);
    ASSERT_EQ(graph.front().associations.at("team").values.at("name").str_val, "Core");
    ASSERT_EQ(graph.front().collections.at("roles").size(), 2U);
    ASSERT_FALSE(graph.front().collections.contains("orders"));
}

TEST(xml_mapper_registry_exposes_standard_statement_type_metadata)
{
    orm::mapper_registry registry;
    const auto loaded = registry.load_xml(R"(
        <mapper namespace="CatalogMapper">
          <select id="find" resultType="catalog::item" parameterType="std::int64_t">
            SELECT 1
          </select>
          <update id="rename" parameterType="catalog::rename_request">
            UPDATE item SET name = #{name}
          </update>
        </mapper>)");
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(registry.statement_result_type("CatalogMapper.find"),
        std::string_view("catalog::item"));
    ASSERT_EQ(registry.statement_parameter_type("CatalogMapper.find"),
        std::string_view("std::int64_t"));
    ASSERT_EQ(registry.statement_parameter_type("rename"),
        std::string_view("catalog::rename_request"));
    ASSERT_TRUE(registry.statement_result_map("CatalogMapper.find").empty());
}

TEST(xml_mapper_registry_rejects_ambiguous_select_result_binding)
{
    orm::mapper_registry registry;
    const auto loaded = registry.load_xml(R"(
        <mapper namespace="BrokenMapper">
          <select id="find" resultMap="Map" resultType="item">SELECT 1</select>
        </mapper>)");
    ASSERT_FALSE(loaded.has_value());
}

TEST(dynamic_sql_foreach_binds_iteration_index_as_a_parameter)
{
    auto statement = orm::parse_xml(R"(
        <select>
          <foreach collection="items" item="item" index="index" separator=",">
            (#{index}, #{item.value})
          </foreach>
        </select>)");
    ASSERT_TRUE(statement.has_value());

    orm::param_context first;
    first.set("value", orm::to_query_parameter(std::int64_t{10}));
    orm::param_context second;
    second.set("value", orm::to_query_parameter(std::int64_t{20}));
    orm::param_context parameters;
    parameters.add_collection("items", {std::move(first), std::move(second)});

    orm::dynamic_sql_processor processor{orm::format_options{}};
    const auto built = processor.process(**statement, parameters, {});
    ASSERT_EQ(built.params.size(), 4U);
    ASSERT_EQ(built.params[0].int_val, 0);
    ASSERT_EQ(built.params[1].int_val, 10);
    ASSERT_EQ(built.params[2].int_val, 1);
    ASSERT_EQ(built.params[3].int_val, 20);
}

TEST(dynamic_sql_binds_property_not_mybatis_placeholder_modifiers)
{
    auto statement = orm::parse_xml(R"(
        <select>
          SELECT * FROM users
          WHERE id = #{ id, jdbcType = BIGINT, javaType=long, numericScale=2 }
            AND name = #{name,typeHandler=example::name_handler,mode=IN}
        </select>)");
    ASSERT_TRUE(statement.has_value());

    const auto parameters = orm::param_context::from_map({
        {"id", orm::param_value::from_int(17)},
        {"name", orm::param_value::from_string("Ada")},
    });
    orm::dynamic_sql_processor processor{orm::format_options{}};
    const auto built = processor.process(**statement, parameters, {});

    ASSERT_TRUE(built.sql.starts_with(
        "SELECT * FROM users WHERE id = {} AND name = {}"));
    ASSERT_TRUE(built.prepared_sql.starts_with(
        "SELECT * FROM users WHERE id = ? AND name = ?"));
    ASSERT_EQ(built.params.size(), 2U);
    ASSERT_EQ(built.params.at(0).int_val, 17);
    ASSERT_EQ(built.params.at(1).str_val, "Ada");
    ASSERT_EQ(built.parameter_mappings.size(), 2U);
    ASSERT_EQ(built.parameter_mappings.at(0).property, "id");
    ASSERT_EQ(built.parameter_mappings.at(0).jdbc_type, "BIGINT");
    ASSERT_EQ(built.parameter_mappings.at(0).java_type, "long");
    ASSERT_EQ(*built.parameter_mappings.at(0).numeric_scale, 2U);
    ASSERT_EQ(built.parameter_mappings.at(1).property, "name");
    ASSERT_EQ(built.parameter_mappings.at(1).type_handler,
        "example::name_handler");
    ASSERT_EQ(built.parameter_mappings.at(1).mode, "IN");
}

TEST(xml_lazy_relation_coalesces_concurrent_first_loads)
{
    auto context = cnetmod::make_io_context();
    std::atomic<std::size_t> loads{};
    orm::lazy_relation<int> relation{
        [raw = context.get(), &loads]() -> cnetmod::task<std::expected<int, std::string>>
        {
            loads.fetch_add(1U, std::memory_order_acq_rel);
            (void)co_await cnetmod::async_timer_wait(*raw,
                std::chrono::milliseconds{1});
            co_return 42;
        }};
    bool passed{};
    cnetmod::spawn(*context,
        [&]() -> cnetmod::task<void>
        {
            auto [first, second] = co_await cnetmod::when_all(relation.get(),
                relation.get());
            passed = first && second && **first == 42 && **second == 42;
            context->stop();
        }());
    context->run();
    ASSERT_TRUE(passed);
    ASSERT_EQ(loads.load(std::memory_order_acquire), std::size_t{1});
}

TEST(orm_database_session_reports_explicit_sql_client_span)
{
    traced_database_client client;
    orm::database_session session{client};
    const auto parent = cnetmod::http::tracing::new_root_context();
    std::optional<cnetmod::http::tracing::completed_span> reported;

    const auto result = cnetmod::sync_wait(session.query(
        "SELECT * FROM users", parent,
        [&reported](const cnetmod::http::tracing::completed_span& span)
        {
            reported = span;
        }));

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(reported.has_value());
    ASSERT_EQ(reported->context.trace_id, parent.trace_id);
    ASSERT_NE(reported->context.span_id, parent.span_id);
    ASSERT_EQ(reported->name, "SQL QUERY");
    ASSERT_EQ(reported->attributes.at(0).first, "db.system");
    ASSERT_EQ(reported->attributes.at(0).second, "sql");
}

TEST(orm_database_session_unifies_mysql_crud_and_model_mapping)
{
    mysql_style_orm_client client;
    orm::database_session session{client, orm::sql_dialect::mysql};

    const auto found = cnetmod::sync_wait(
        session.find_by_id<orm_crud_user>(orm::param_value::from_int(7)));
    ASSERT_TRUE(found.ok());
    ASSERT_EQ(found.first()->name, "Ada");
    ASSERT_TRUE(client.last_sql.contains("`users`"));
    ASSERT_TRUE(client.last_sql.contains("`id` = 7"));

    orm_crud_user created{.name = "Lin", .status = 3};
    const auto inserted = cnetmod::sync_wait(session.insert(created));
    ASSERT_TRUE(inserted.ok());
    ASSERT_EQ(created.id, 73);
    ASSERT_EQ(inserted.first()->id, 73);
    ASSERT_TRUE(client.last_sql.starts_with("INSERT INTO `users`"));
    ASSERT_TRUE(client.last_sql.contains("'Lin'"));

    const auto updated = cnetmod::sync_wait(session.update(created));
    ASSERT_TRUE(updated.ok());
    ASSERT_TRUE(client.last_sql.starts_with("UPDATE `users` SET"));
    ASSERT_TRUE(client.last_sql.contains("WHERE `id` = 73"));

    const auto removed = cnetmod::sync_wait(session.remove(created));
    ASSERT_TRUE(removed.ok());
    ASSERT_TRUE(client.last_sql.starts_with("DELETE FROM `users`"));

    const auto removed_by_id = cnetmod::sync_wait(
        session.remove_by_id<orm_crud_user>(orm::param_value::from_int(73)));
    ASSERT_TRUE(removed_by_id.ok());
    ASSERT_TRUE(client.last_sql.contains("WHERE `id` = 73"));

    orm::query_wrapper<orm_crud_user> select_wrapper;
    select_wrapper.eq("status", 1);
    const auto selected = cnetmod::sync_wait(session.execute(select_wrapper));
    ASSERT_TRUE(selected.ok());
    ASSERT_EQ(selected.first()->name, "Ada");
    ASSERT_TRUE(client.last_sql.starts_with("SELECT"));

    orm::query_wrapper<orm_crud_user> delete_wrapper;
    delete_wrapper.eq("id", 73).as_delete();
    const auto deleted = cnetmod::sync_wait(session.execute(delete_wrapper));
    ASSERT_TRUE(deleted.ok());
    ASSERT_TRUE(client.last_sql.starts_with("DELETE FROM `users`"));

    orm::update_wrapper<orm_crud_user> update_wrapper;
    update_wrapper.set("name", "Ada Lovelace").eq("id", 73);
    const auto conditionally_updated = cnetmod::sync_wait(session.execute(update_wrapper));
    ASSERT_TRUE(conditionally_updated.ok());
    ASSERT_TRUE(client.last_sql.starts_with("UPDATE `users` SET"));
}

TEST(orm_database_session_uses_postgresql_binding_and_returning_mapping)
{
    postgresql_style_orm_client client;
    orm::database_session session{client, orm::sql_dialect::postgresql};

    const auto found = cnetmod::sync_wait(
        session.find_by_id<orm_crud_user>(orm::param_value::from_int(17)));
    ASSERT_TRUE(found.ok());
    ASSERT_EQ(found.first()->id, 17);
    ASSERT_TRUE(client.last_sql.contains("\"users\""));
    ASSERT_TRUE(client.last_sql.contains("\"id\" = $1"));
    ASSERT_EQ(client.last_parameters.size(), 1U);
    ASSERT_EQ(client.last_parameters.front().int_val, 17);

    orm_crud_user created{.name = "Grace", .status = 2};
    const auto inserted = cnetmod::sync_wait(session.insert(created));
    ASSERT_TRUE(inserted.ok());
    ASSERT_TRUE(client.last_sql.contains("RETURNING *"));
    ASSERT_EQ(created.id, 17);
    ASSERT_EQ(created.name, "Grace");
    ASSERT_EQ(client.last_parameters.size(), 2U);
}

TEST(query_wrapper_accepts_optional_condition_values)
{
    std::optional<std::int64_t> active_status{1};
    std::optional<std::string> absent_name;
    orm::query_wrapper<orm_crud_user> query;
    query.eq(&orm_crud_user::status, active_status)
        .eq(&orm_crud_user::name, absent_name)
        .eq("id", std::nullopt);

    const auto [sql, parameters] = query.build_select_sql(orm::sql_dialect::postgresql);
    ASSERT_TRUE(sql.contains("\"status\" = $1"));
    ASSERT_TRUE(sql.contains("\"name\" IS NULL"));
    ASSERT_TRUE(sql.contains("\"id\" IS NULL"));
    ASSERT_EQ(parameters.size(), 1U);
    ASSERT_TRUE(parameters[0].kind == orm::param_value::kind_t::int64_kind);
}

TEST(query_wrapper_preserves_sql_null_and_collection_semantics)
{
    std::optional<std::int64_t> absent;

    orm::query_wrapper<orm_crud_user> null_query;
    null_query.ne("id", absent);
    const auto [null_sql, null_parameters] =
        null_query.build_select_sql(orm::sql_dialect::postgresql);
    ASSERT_TRUE(null_sql.contains("\"id\" IS NOT NULL"));
    ASSERT_TRUE(null_parameters.empty());

    orm::query_wrapper<orm_crud_user> empty_in;
    empty_in.in("id", std::vector<std::int64_t>{});
    const auto [empty_in_sql, empty_in_parameters] =
        empty_in.build_select_sql(orm::sql_dialect::postgresql);
    ASSERT_TRUE(empty_in_sql.contains("1 = 0"));
    ASSERT_TRUE(empty_in_parameters.empty());

    orm::query_wrapper<orm_crud_user> empty_not_in;
    empty_not_in.not_in("id", std::vector<std::int64_t>{});
    const auto [empty_not_in_sql, empty_not_in_parameters] =
        empty_not_in.build_select_sql(orm::sql_dialect::postgresql);
    ASSERT_TRUE(empty_not_in_sql.contains("1 = 1"));
    ASSERT_TRUE(empty_not_in_parameters.empty());

    const std::vector<std::optional<std::int64_t>> mixed_values{
        std::int64_t{7}, std::nullopt, std::int64_t{9}};
    orm::query_wrapper<orm_crud_user> mixed_in;
    mixed_in.in("id", mixed_values);
    const auto [mixed_in_sql, mixed_in_parameters] =
        mixed_in.build_select_sql(orm::sql_dialect::postgresql);
    ASSERT_TRUE(mixed_in_sql.contains("\"id\" IN ($1, $2)"));
    ASSERT_TRUE(mixed_in_sql.contains("OR \"id\" IS NULL"));
    ASSERT_EQ(mixed_in_parameters.size(), 2U);

    orm::query_wrapper<orm_crud_user> mixed_not_in;
    mixed_not_in.not_in("id", mixed_values);
    const auto [mixed_not_in_sql, mixed_not_in_parameters] =
        mixed_not_in.build_select_sql(orm::sql_dialect::postgresql);
    ASSERT_TRUE(mixed_not_in_sql.contains("\"id\" NOT IN ($1, $2)"));
    ASSERT_TRUE(mixed_not_in_sql.contains("AND \"id\" IS NOT NULL"));
    ASSERT_EQ(mixed_not_in_parameters.size(), 2U);

    orm::query_wrapper<orm_crud_user> invalid_between;
    ASSERT_THROWS(invalid_between.between("id", absent, std::int64_t{10}));
}

TEST(update_wrapper_preserves_set_order_and_optional_conditions)
{
    std::optional<std::int64_t> absent_id;
    orm::update_wrapper<orm_crud_user> update;
    update.set("name", "first")
        .set("status", 2)
        .set("name", "replacement")
        .eq("id", absent_id)
        .ne("status", std::nullopt);

    const auto [sql, parameters] = update.build_sql(orm::sql_dialect::postgresql);
    ASSERT_TRUE(sql.contains(
        "SET \"name\" = $1, \"status\" = $2"));
    ASSERT_TRUE(sql.contains("\"id\" IS NULL"));
    ASSERT_TRUE(sql.contains("\"status\" IS NOT NULL"));
    ASSERT_EQ(parameters.size(), 2U);
    ASSERT_TRUE(parameters[0].kind == orm::param_value::kind_t::string_kind);
    ASSERT_EQ(parameters[0].str_val, std::string("replacement"));
    ASSERT_TRUE(parameters[1].kind == orm::param_value::kind_t::int64_kind);
    ASSERT_EQ(parameters[1].int_val, 2);
}

RUN_TESTS()
