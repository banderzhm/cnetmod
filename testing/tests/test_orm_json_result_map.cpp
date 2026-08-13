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

namespace orm = cnetmod::orm;

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

RUN_TESTS()
