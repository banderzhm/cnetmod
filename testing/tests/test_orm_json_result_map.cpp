#include "test_framework.hpp"
#include <cnetmod/orm.hpp>

import std;
import nlohmann.json;
import cnetmod.orm;
import cnetmod.io.io_context;
import cnetmod.core.net_init;
import cnetmod.core.socket;
import cnetmod.core.buffer;
import cnetmod.core.address;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.instrumentation.tracing;
import cnetmod.protocol.mysql;
import cnetmod.instrumentation.operation_result;

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

struct orm_soft_deleted_record
{
    std::int64_t id{};
    std::optional<orm::calendar_datetime> deleted_at;
};

CNETMOD_MODEL(orm_soft_deleted_record, "soft_deleted_records",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(deleted_at, "deleted_at", datetime, NULLABLE | LOGIC_DELETE))

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
    std::error_code failure;
    orm::query_result response;
    unsigned calls = 0;

    auto query(std::string_view) -> cnetmod::task<orm::query_result>
    {
        ++calls;
        if (failure)
            throw std::system_error(failure);
        co_return response;
    }

    auto execute(std::string_view) -> cnetmod::task<orm::query_result>
    {
        ++calls;
        if (failure)
            throw std::system_error(failure);
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

struct diagnostic_database_client
{
    orm::query_result response;

    auto query(std::string_view) -> cnetmod::task<orm::query_result>
    {
        co_return response;
    }

    auto execute(std::string_view) -> cnetmod::task<orm::query_result>
    {
        co_return response;
    }

    auto execute(orm::parameterized_query) -> cnetmod::task<orm::query_result>
    {
        co_return response;
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

struct transaction_orm_client
{
    std::vector<std::string> statements;
    std::map<std::string, std::string> failures;

    auto query(std::string_view sql) -> cnetmod::task<orm::query_result>
    {
        co_return co_await execute(sql);
    }

    auto execute(std::string_view sql) -> cnetmod::task<orm::query_result>
    {
        statements.emplace_back(sql);
        orm::query_result result;
        if (const auto failure = failures.find(std::string{sql});
            failure != failures.end())
            result.error_msg = failure->second;
        co_return result;
    }

    auto execute(orm::parameterized_query statement)
        -> cnetmod::task<orm::query_result>
    {
        co_return co_await execute(statement.query);
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
    auto run = [&]() -> cnetmod::task<void>
    {
        auto [first, second] = co_await cnetmod::when_all(relation.get(),
            relation.get());
        passed = first && second && **first == 42 && **second == 42;
        context->stop();
    };
    auto operation = run();
    context->post(operation.handle());
    context->run();
    ASSERT_TRUE(operation.handle().done());
    operation.handle().promise().result();
    ASSERT_TRUE(passed);
    ASSERT_EQ(loads.load(std::memory_order_acquire), std::size_t{1});
}

TEST(orm_database_session_reports_explicit_sql_client_span)
{
    traced_database_client client;
    orm::database_session session{client};
    const auto parent = cnetmod::instrumentation::new_root_context();
    std::optional<cnetmod::instrumentation::completed_span> reported;

    const auto result = cnetmod::sync_wait(session.query(
        "SELECT * FROM users", parent,
        [&reported](const cnetmod::instrumentation::completed_span& span)
        {
            reported = span;
        }));

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(reported.has_value());
    ASSERT_EQ(reported->context.trace_id, parent.trace_id);
    ASSERT_NE(reported->context.span_id, parent.span_id);
    ASSERT_EQ(reported->name, "SQL QUERY");
    ASSERT_EQ(reported->attributes.at(0).first, "db.system.name");
    ASSERT_EQ(reported->attributes.at(0).second, "mysql");
}

TEST(orm_model_result_preserves_native_database_diagnostics)
{
    diagnostic_database_client client;
    client.response.error_msg = "duplicate entry";
    client.response.sql_state = "23000";
    client.response.error_code = 1062;
    orm::database_session session{client, orm::sql_dialect::mysql};

    const auto result = cnetmod::sync_wait(session.find_all<orm_crud_user>());

    ASSERT_TRUE(result.is_err());
    ASSERT_EQ(result.error_code, 1062U);
    ASSERT_EQ(result.sql_state, "23000");
    ASSERT_EQ(result.error_msg, "duplicate entry");
}

TEST(orm_datetime_model_mapping_supports_nullable_and_utc_epoch_values)
{
    const orm::calendar_datetime datetime{
        2026, 9, 17, 8, 31, 57, 123456};
    const auto field = orm::field_value::from_datetime(datetime);

    std::optional<orm::calendar_datetime> nullable;
    orm::detail::set_member(nullable, field);
    ASSERT_TRUE(nullable.has_value());
    ASSERT_EQ(nullable->to_string(), "2026-09-17 08:31:57.123456");
    const auto parameter = orm::detail::get_member(nullable);
    ASSERT_TRUE(parameter.kind == orm::param_value::kind_t::datetime_kind);
    ASSERT_EQ(parameter.datetime_val.to_string(), datetime.to_string());

    orm::detail::set_member(nullable, orm::field_value::null());
    ASSERT_FALSE(nullable.has_value());
    ASSERT_TRUE(orm::detail::get_member(nullable).kind ==
        orm::param_value::kind_t::null_kind);

    std::int64_t unix_seconds{};
    orm::detail::set_member(unix_seconds, field);
    ASSERT_EQ(unix_seconds, std::int64_t{1789633917});
}

TEST(orm_logical_delete_supports_nullable_datetime_markers)
{
    orm::logical_delete_config config;
    config.field_name = "deleted_at";
    config.mode = orm::logical_delete_mode::nullable_datetime;
    config.touch_fields = {
        {"updated_at", orm::logical_delete_touch_value::current_timestamp},
        {"archive_date", orm::logical_delete_touch_value::current_date}};
    orm::logical_delete_interceptor interceptor{std::move(config)};

    const auto selected = interceptor.inject_select_condition<orm_soft_deleted_record>(
        "SELECT * FROM `soft_deleted_records` WHERE `id` = 7 ORDER BY `id`");
    ASSERT_EQ(selected,
        "SELECT * FROM `soft_deleted_records` WHERE `deleted_at` IS NULL AND `id` = 7 ORDER BY `id`");

    const auto removed = interceptor.transform_delete_to_update<orm_soft_deleted_record>(
        "DELETE FROM `soft_deleted_records` WHERE `id` = 7");
    ASSERT_EQ(removed,
        "UPDATE `soft_deleted_records` SET `deleted_at` = CURRENT_TIMESTAMP, `updated_at` = CURRENT_TIMESTAMP, `archive_date` = CURRENT_DATE WHERE `id` = 7");

    config.field_name = "deleted_at` = NULL WHERE 1=1 --";
    bool rejected = false;
    try
    {
        orm::logical_delete_interceptor invalid{std::move(config)};
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    ASSERT_TRUE(rejected);
}

TEST(orm_error_spans_report_only_valid_database_codes)
{
    for (const auto dialect : {orm::sql_dialect::mysql, orm::sql_dialect::postgresql})
    {
        traced_database_client client;
        client.response.error_msg = "private database diagnostic";
        client.response.error_code = 1054;
        client.response.sql_state = "42S22";
        orm::database_session session{client, dialect};
        const auto parent = cnetmod::instrumentation::new_root_context();
        std::optional<cnetmod::instrumentation::completed_span> reported;
        cnetmod::instrumentation::span_exporter sink = [&](const auto& span)
        {
            reported = span;
        };
        for (const bool malformed : {false, true})
        {
            if (malformed)
            {
                client.response.error_code = 0;
                client.response.sql_state = "private-secret";
            }
            const auto result = cnetmod::sync_wait(session.query("private query", parent, sink));
            ASSERT_EQ(result.error_msg, client.response.error_msg);
            ASSERT_EQ(result.sql_state, client.response.sql_state);
            ASSERT_TRUE(reported.has_value());
            if (!reported)
                continue;
            ASSERT_TRUE(reported->failed);
            unsigned codes = 0;
            for (const auto& [key, value] : reported->attributes)
            {
                ASSERT_FALSE(value.contains("private"));
                if (key == "db.response.status_code" || key == "error.type")
                {
                    ++codes;
                    ASSERT_EQ(value, dialect == orm::sql_dialect::mysql ? "1054" : "42S22");
                }
            }
            ASSERT_EQ(codes, malformed ? 0U : 2U);
        }
    }
}

TEST(orm_observation_preserves_exceptions_and_parent_sampling)
{
    traced_database_client client;
    orm::database_session session{client};
    auto parent = cnetmod::instrumentation::new_root_context();
    unsigned exports = 0;
    cnetmod::instrumentation::span_exporter sink = [&](const auto& span)
    {
        ++exports;
        ASSERT_TRUE(span.result.status == cnetmod::instrumentation::operation_status::timeout);
        ASSERT_TRUE(span.result.error == std::errc::timed_out);
        throw std::runtime_error("export failure");
    };
    client.failure = std::make_error_code(std::errc::timed_out);
    for (bool execution : {false, true})
    {
        bool caught = false;
        try
        {
            (void)cnetmod::sync_wait(execution
                    ? session.execute("private SQL", parent, sink)
                    : session.query("private SQL", parent, sink));
        }
        catch (const std::system_error& error)
        {
            caught = error.code() == client.failure;
        }
        ASSERT_TRUE(caught);
    }
    ASSERT_EQ(exports, 2U);
    client.failure.clear();
    parent.flags = 0;
    ASSERT_TRUE(cnetmod::sync_wait(session.query("private SQL", parent, sink,
                                       {.capture_query_text = true}))
            .ok());
    ASSERT_EQ(exports, 2U);
    ASSERT_TRUE(cnetmod::sync_wait(session.execute("private SQL", parent, {})).ok());
    ASSERT_EQ(client.calls, 4U);
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

TEST(orm_database_session_expected_transaction_commits_typed_value)
{
    transaction_orm_client client;
    orm::database_session session{client, orm::sql_dialect::mysql};

    const auto result = cnetmod::sync_wait(session.transaction<int>(
        []() -> cnetmod::task<std::expected<int, std::string>>
        {
            co_return 42;
        }));

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, 42);
    ASSERT_EQ(client.statements.size(), 2U);
    ASSERT_EQ(client.statements[0], "START TRANSACTION");
    ASSERT_EQ(client.statements[1], "COMMIT");
}

TEST(orm_database_session_expected_transaction_rolls_back_void_failure)
{
    transaction_orm_client client;
    orm::database_session session{client, orm::sql_dialect::mysql};

    const auto result = cnetmod::sync_wait(session.transaction<void>(
        []() -> cnetmod::task<std::expected<void, std::string>>
        {
            co_return std::unexpected("validation failed");
        }));

    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), "validation failed");
    ASSERT_EQ(client.statements.size(), 2U);
    ASSERT_EQ(client.statements[0], "START TRANSACTION");
    ASSERT_EQ(client.statements[1], "ROLLBACK");
}

TEST(orm_database_session_expected_transaction_rolls_back_commit_failure)
{
    transaction_orm_client client;
    client.failures.emplace("COMMIT", "connection lost");
    orm::database_session session{client, orm::sql_dialect::mysql};

    const auto result = cnetmod::sync_wait(session.transaction<int>(
        []() -> cnetmod::task<std::expected<int, std::string>>
        {
            co_return 7;
        }));

    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(result.error().contains(
        "transaction commit failed: connection lost"));
    ASSERT_EQ(client.statements.size(), 3U);
    ASSERT_EQ(client.statements[2], "ROLLBACK");
}

TEST(orm_database_session_expected_transaction_reports_rollback_failure)
{
    transaction_orm_client client;
    client.failures.emplace("ROLLBACK", "server closed transaction");
    orm::database_session session{client, orm::sql_dialect::mysql};

    const auto result = cnetmod::sync_wait(session.transaction<void>(
        []() -> cnetmod::task<std::expected<void, std::string>>
        {
            co_return std::unexpected("write rejected");
        }));

    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(result.error().contains("write rejected"));
    ASSERT_TRUE(result.error().contains(
        "transaction rollback failed: server closed transaction"));
}

TEST(orm_database_session_expected_transaction_supports_isolation_level)
{
    transaction_orm_client client;
    orm::database_session session{client, orm::sql_dialect::postgresql};

    const auto result = cnetmod::sync_wait(session.transaction<void>(
        []() -> cnetmod::task<std::expected<void, std::string>>
        {
            co_return std::expected<void, std::string>{};
        },
        orm::isolation_level::serializable));

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(client.statements.size(), 2U);
    ASSERT_EQ(client.statements[0], "BEGIN ISOLATION LEVEL SERIALIZABLE");
    ASSERT_EQ(client.statements[1], "COMMIT");
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

TEST(query_wrapper_builds_parameterless_conditions_without_overload_ambiguity)
{
    orm::query_wrapper<orm_crud_user> query;
    query.is_null("name")
        .is_not_null("id")
        .is_true("status")
        .is_false("id")
        .raw("1 = 1");

    const auto [sql, parameters] =
        query.build_select_sql(orm::sql_dialect::postgresql);
    ASSERT_TRUE(sql.contains("\"name\" IS NULL"));
    ASSERT_TRUE(sql.contains("\"id\" IS NOT NULL"));
    ASSERT_TRUE(sql.contains("\"status\" = TRUE"));
    ASSERT_TRUE(sql.contains("\"id\" = FALSE"));
    ASSERT_TRUE(sql.contains("1 = 1"));
    ASSERT_TRUE(parameters.empty());
}

TEST(parameterized_query_accepts_string_literals_without_overload_ambiguity)
{
    auto query = cnetmod::database::with_params(
        "SELECT $1", {orm::param_value::from_int(7)});
    ASSERT_EQ(query.query, std::string("SELECT $1"));
    ASSERT_EQ(query.args.size(), 1U);
    ASSERT_EQ(query.args.front().int_val, 7);
}

TEST(observed_bound_execution_preserves_bindings_without_exporting_values)
{
    postgresql_style_orm_client client;
    orm::database_session session{client, orm::sql_dialect::postgresql};
    const auto parent = cnetmod::instrumentation::new_root_context();
    std::optional<cnetmod::instrumentation::completed_span> reported;
    auto pending = session.execute(cnetmod::database::with_params(
                                       "SELECT $1", {orm::param_value::from_string("private-binding")}),
        parent,
        [&](const auto& span)
        {
            reported = span;
        },
        {.capture_query_text = true});
    ASSERT_TRUE(client.last_sql.empty());
    const auto result = cnetmod::sync_wait(std::move(pending));
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(client.last_sql, "SELECT $1");
    ASSERT_EQ(client.last_parameters.size(), 1U);
    ASSERT_EQ(client.last_parameters.front().str_val, "private-binding");
    ASSERT_TRUE(reported.has_value());
    ASSERT_EQ(reported->name, "SQL EXECUTE");
    ASSERT_EQ(reported->attributes.at(0).first, "db.system.name");
    ASSERT_EQ(reported->attributes.at(0).second, "postgresql");
    ASSERT_EQ(reported->parent_span_id, parent.span_id);
    bool query_present = false;
    for (const auto& [key, value] : reported->attributes)
    {
        ASSERT_FALSE(value.contains("private-binding"));
        if (key == "db.query.text")
            query_present = value == "SELECT $1";
    }
    ASSERT_TRUE(query_present);
}

TEST(orm_observation_starts_only_when_the_database_task_runs)
{
    traced_database_client client;
    orm::database_session session{client};
    unsigned samples = 0;
    unsigned exports = 0;
    bool accept = false;
    auto resumed_at = std::chrono::system_clock::time_point{};
    cnetmod::instrumentation::span_exporter sink{
        [&](const auto& span)
        {
            ++exports;
            ASSERT_TRUE(span.started_at >= resumed_at);
        },
        [&](const auto&)
        {
            ++samples;
            return accept;
        }};
    {
        auto discarded = session.query("SELECT 1", {}, sink);
    }
    ASSERT_EQ(samples, 0U);
    ASSERT_EQ(exports, 0U);
    ASSERT_EQ(client.calls, 0U);
    auto pending = session.execute("SELECT 1", {}, sink);
    ASSERT_EQ(samples, 0U);
    accept = true;
    resumed_at = std::chrono::system_clock::now();
    ASSERT_TRUE(cnetmod::sync_wait(std::move(pending)).ok());
    ASSERT_EQ(samples, 1U);
    ASSERT_EQ(exports, 1U);
    ASSERT_EQ(client.calls, 1U);
}

TEST(orm_observation_copy_failure_does_not_prevent_execution)
{
    struct throwing_sink
    {
        bool* fail;

        explicit throwing_sink(bool& value) : fail(&value) {}

        throwing_sink(const throwing_sink& other) : fail(other.fail)
        {
            if (*fail)
                throw std::bad_alloc{};
        }

        void operator()(const cnetmod::instrumentation::completed_span&) const {}
    };

    bool fail = false;
    cnetmod::instrumentation::span_exporter sink{throwing_sink{fail}};
    traced_database_client client;
    orm::database_session session{client};
    fail = true;
    ASSERT_TRUE(cnetmod::sync_wait(session.query("SELECT 1", {}, sink)).ok());
    ASSERT_TRUE(cnetmod::sync_wait(session.execute("SELECT 1", {}, sink)).ok());
    ASSERT_EQ(client.calls, 2U);
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

TEST(mysql_pool_stop_wakes_idle_maintenance_and_preserves_prestart_stop)
{
    for (bool early : {false, true})
    {
        auto io = cnetmod::make_io_context();
        cnetmod::mysql::pool_params options;
        options.initial_size = 0;
        options.ping_interval = std::chrono::hours{1};
        cnetmod::mysql::connection_pool pool{*io, options};
        if (early)
            pool.request_stop();
        bool finished = false;
        auto run = [&]() -> cnetmod::task<void>
        {
            co_await pool.async_run();
            co_await pool.cancel();
            finished = true;
            io->stop();
        };
        auto stop = [&]() -> cnetmod::task<void>
        {
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{10});
            pool.request_stop();
        };
        cnetmod::spawn(*io, run());
        if (!early)
            cnetmod::spawn(*io, stop());
        const auto started = std::chrono::steady_clock::now();
        io->run();
        ASSERT_TRUE(finished);
        ASSERT_TRUE(std::chrono::steady_clock::now() - started < std::chrono::seconds{2});
    }
}

TEST(mysql_pool_stop_releases_queued_acquisition)
{
    auto io = cnetmod::make_io_context();
    cnetmod::mysql::pool_params options;
    options.initial_size = 0;
    cnetmod::mysql::connection_pool pool{*io, options};
    cnetmod::cancel_token token;
    bool cancelled = false;
    auto acquire = [&]() -> cnetmod::task<void>
    {
        auto result = co_await pool.async_get_connection(token);
        cancelled = !result && result.error() == std::errc::operation_canceled;
        io->stop();
    };
    auto stop = [&]() -> cnetmod::task<void>
    {
        (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{10});
        ASSERT_EQ(pool.waiter_count(), 1U);
        co_await pool.cancel();
    };
    cnetmod::spawn(*io, acquire());
    cnetmod::spawn(*io, stop());
    io->run();
    ASSERT_TRUE(cancelled);
    ASSERT_EQ(pool.waiter_count(), 0U);
    auto immediate = pool.try_get_connection();
    ASSERT_FALSE(immediate.has_value());
    ASSERT_TRUE(immediate.error() == std::errc::operation_canceled);
    auto after_stop = cnetmod::sync_wait(pool.async_get_connection(token));
    ASSERT_FALSE(after_stop.has_value());
    ASSERT_TRUE(after_stop.error() == std::errc::operation_canceled);
}

TEST(mysql_pool_pre_cancelled_acquisition_never_enters_queue)
{
    auto io = cnetmod::make_io_context();
    cnetmod::mysql::pool_params options;
    options.initial_size = 0;
    cnetmod::mysql::connection_pool pool{*io, options};
    for (bool expired : {false, true})
    {
        cnetmod::cancel_token token;
        if (expired)
            token.cancel_due_to_deadline();
        else
            token.cancel();
        auto pending = pool.async_get_connection(token);
        pending.handle().resume();
        ASSERT_TRUE(pending.handle().done());
        auto result = pending.handle().promise().result();
        ASSERT_FALSE(result.has_value());
        ASSERT_TRUE(result.error() == (expired ? std::errc::timed_out : std::errc::operation_canceled));
        ASSERT_EQ(pool.waiter_count(), 0U);
        ASSERT_EQ(pool.size(), 0U);
    }
}

TEST(mysql_connect_cancellation_interrupts_silent_server_greeting)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    std::optional<cnetmod::socket> peer;
    cnetmod::mysql::client client{*io};
    cnetmod::cancel_token token;
    bool completed = false;
    auto server = [&]() -> cnetmod::task<void>
    {
        auto accepted = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(accepted.has_value());
        peer.emplace(std::move(*accepted));
        (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{10});
        token.cancel();
    };
    auto connect = [&]() -> cnetmod::task<void>
    {
        cnetmod::mysql::connect_options options;
        options.host = "127.0.0.1";
        options.port = endpoint->port();
        options.ssl = cnetmod::mysql::ssl_mode::disable;
        auto result = co_await client.connect(options, token);
        ASSERT_TRUE(result.is_err());
        ASSERT_TRUE(token.is_cancelled());
        ASSERT_TRUE(static_cast<bool>(client.last_error()));
        ASSERT_FALSE(client.is_open());
        completed = true;
        io->stop();
    };
    cnetmod::spawn(*io, server());
    cnetmod::spawn(*io, connect());
    io->run();
    ASSERT_TRUE(completed);
    ASSERT_TRUE(peer.has_value());
    ASSERT_TRUE(peer->is_open());
}

TEST(mysql_pool_run_joins_worker_waiting_for_server_greeting)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    cnetmod::mysql::pool_params options;
    options.port = endpoint->port();
    options.ssl = cnetmod::mysql::ssl_mode::disable;
    auto pool = std::make_unique<cnetmod::mysql::connection_pool>(*io, options);
    std::optional<cnetmod::socket> peer;
    bool completed = false;
    auto server = [&]() -> cnetmod::task<void>
    {
        auto accepted = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(accepted.has_value());
        peer.emplace(std::move(*accepted));
        (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{10});
        pool->request_stop();
    };
    auto run = [&]() -> cnetmod::task<void>
    {
        co_await pool->async_run();
        ASSERT_TRUE(peer.has_value());
        ASSERT_TRUE(peer->is_open());
        pool.reset();
        completed = true;
        io->stop();
    };
    cnetmod::spawn(*io, server());
    cnetmod::spawn(*io, run());
    io->run();
    ASSERT_TRUE(completed);
    // Drain any remaining queued completion after destroying the pool.
    io->restart();
    io->poll();
}

TEST(mysql_sharded_run_remains_pending_until_all_shards_stop)
{
    auto io = cnetmod::make_io_context();
    cnetmod::mysql::pool_params options;
    options.initial_size = 0;
    auto pool = std::make_unique<cnetmod::mysql::sharded_connection_pool>(*io, options, 3);
    auto running = pool->async_run();
    running.handle().resume();
    io->poll();
    ASSERT_FALSE(running.handle().done());
    pool->request_stop();
    for (unsigned step = 0; step < 32; ++step)
        io->poll();
    ASSERT_TRUE(running.handle().done());
    running.handle().promise().result();
    pool.reset();
    io->poll();
}

TEST(mysql_pool_rejects_duplicate_maintenance_without_stopping_owner)
{
    auto io = cnetmod::make_io_context();
    cnetmod::mysql::pool_params options;
    options.initial_size = 0;
    cnetmod::mysql::connection_pool pool{*io, options};
    auto owner = pool.async_run();
    owner.handle().resume();
    ASSERT_FALSE(owner.handle().done());
    for (unsigned attempt = 0; attempt < 3; ++attempt)
    {
        auto duplicate = pool.async_run();
        duplicate.handle().resume();
        ASSERT_TRUE(duplicate.handle().done());
        bool rejected = false;
        try
        {
            duplicate.handle().promise().result();
        }
        catch (const std::system_error& error)
        {
            rejected = error.code() == std::errc::operation_in_progress;
        }
        ASSERT_TRUE(rejected);
        ASSERT_FALSE(owner.handle().done());
    }
    pool.request_stop();
    for (unsigned step = 0; step < 32; ++step)
        io->poll();
    ASSERT_TRUE(owner.handle().done());
    owner.handle().promise().result();
    auto stopped = pool.async_run();
    stopped.handle().resume();
    ASSERT_TRUE(stopped.handle().done());
    stopped.handle().promise().result();
}

TEST(mysql_sharded_duplicate_run_does_not_cancel_owner)
{
    auto io = cnetmod::make_io_context();
    cnetmod::mysql::pool_params options;
    options.initial_size = 0;
    cnetmod::mysql::sharded_connection_pool pool{*io, options, 3};
    auto owner = pool.async_run();
    owner.handle().resume();
    for (unsigned attempt = 0; attempt < 3; ++attempt)
    {
        auto duplicate = pool.async_run();
        duplicate.handle().resume();
        ASSERT_TRUE(duplicate.handle().done());
        bool rejected = false;
        try
        {
            duplicate.handle().promise().result();
        }
        catch (const std::system_error& error)
        {
            rejected = error.code() == std::errc::operation_in_progress;
        }
        ASSERT_TRUE(rejected);
        for (unsigned step = 0; step < 8; ++step)
            io->poll();
        ASSERT_FALSE(owner.handle().done());
    }
    pool.request_stop();
    for (unsigned step = 0; step < 32; ++step)
        io->poll();
    ASSERT_TRUE(owner.handle().done());
    owner.handle().promise().result();
    auto stopped = pool.async_run();
    stopped.handle().resume();
    for (unsigned step = 0; step < 32; ++step)
        io->poll();
    ASSERT_TRUE(stopped.handle().done());
    stopped.handle().promise().result();
}

TEST(mysql_sharded_scoped_workload_joins_on_success_and_exception)
{
    for (bool fail : {false, true})
    {
        auto io = cnetmod::make_io_context();
        cnetmod::mysql::pool_params options;
        options.initial_size = 0;
        cnetmod::mysql::sharded_connection_pool pool{*io, options, 2};
        bool completed = false;
        bool propagated = false;
        auto workload = [&]() -> cnetmod::task<void>
        {
            struct stop_on_exit
            {
                cnetmod::mysql::sharded_connection_pool& pool;
                ~stop_on_exit()
                {
                    pool.request_stop();
                }
            } stop{pool};
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
            if (fail)
                throw std::runtime_error("workload failed");
        };
        auto run = [&]() -> cnetmod::task<void>
        {
            try
            {
                co_await cnetmod::when_all(pool.async_run(), workload());
            }
            catch (const std::runtime_error& error)
            {
                propagated = std::string_view{error.what()} == "workload failed";
            }
            completed = true;
            io->stop();
        };
        cnetmod::spawn(*io, run());
        io->run();
        ASSERT_TRUE(completed);
        ASSERT_EQ(propagated, fail);
    }
}

TEST(mysql_sharded_stop_joins_two_event_loop_threads)
{
    for (unsigned trial = 0; trial < 32; ++trial)
    {
        auto first = cnetmod::make_io_context();
        auto second = cnetmod::make_io_context();
        cnetmod::mysql::pool_params options;
        options.initial_size = 0;
        auto pool = std::make_unique<cnetmod::mysql::sharded_connection_pool>(
            std::vector<cnetmod::io_context*>{first.get(), second.get()}, options);
        std::atomic<unsigned> ready{0};
        bool completed = false;
        std::exception_ptr failure;
        auto run = [&]() -> cnetmod::task<void>
        {
            try
            {
                co_await pool->async_run();
                completed = true;
            }
            catch (...)
            {
                failure = std::current_exception();
            }
            first->stop();
            second->stop();
        };
        auto signal_ready = [&](cnetmod::io_context& io) -> cnetmod::task<void>
        {
            (void)co_await cnetmod::async_timer_wait(io, std::chrono::milliseconds{1});
            ready.fetch_add(1);
            ready.notify_one();
        };
        cnetmod::spawn(*first, run());
        cnetmod::spawn(*first, signal_ready(*first));
        cnetmod::spawn(*second, signal_ready(*second));
        std::jthread first_thread([&]
            {
                first->run();
            });
        std::jthread second_thread([&]
            {
                second->run();
            });
        for (auto count = ready.load(); count < 2; count = ready.load())
            ready.wait(count);
        pool->request_stop();
        first_thread.join();
        second_thread.join();
        ASSERT_TRUE(completed);
        ASSERT_FALSE(static_cast<bool>(failure));
        pool.reset();
        first->restart();
        second->restart();
        first->poll();
        second->poll();
    }
}

TEST(mysql_pool_stop_and_caller_cancel_complete_waiters_once)
{
    auto io = cnetmod::make_io_context();
    cnetmod::mysql::pool_params options;
    options.initial_size = 0;
    cnetmod::mysql::connection_pool pool{*io, options};
    std::array<cnetmod::cancel_token, 32> tokens;
    std::array<unsigned, 32> completions{};
    unsigned completed = 0;
    auto acquire = [&](std::size_t index) -> cnetmod::task<void>
    {
        auto result = co_await pool.async_get_connection(tokens[index]);
        ASSERT_FALSE(result.has_value());
        ASSERT_TRUE(result.error() == std::errc::operation_canceled);
        ++completions[index];
        if (++completed == tokens.size())
            io->stop();
    };
    auto stop = [&]() -> cnetmod::task<void>
    {
        (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{10});
        ASSERT_EQ(pool.waiter_count(), tokens.size());
        for (std::size_t index = 0; index < tokens.size(); index += 2)
            tokens[index].cancel();
        co_await pool.cancel();
        for (auto& token : tokens)
            token.cancel();
        co_await pool.cancel();
    };
    for (std::size_t index = 0; index < tokens.size(); ++index)
        cnetmod::spawn(*io, acquire(index));
    cnetmod::spawn(*io, stop());
    io->run();
    ASSERT_EQ(pool.waiter_count(), 0U);
    for (auto count : completions)
        ASSERT_EQ(count, 1U);
}

TEST(mysql_pool_stop_races_cross_thread_caller_cancellation)
{
    for (unsigned trial = 0; trial < 32; ++trial)
    {
        auto io = cnetmod::make_io_context();
        cnetmod::mysql::pool_params options;
        options.initial_size = 0;
        cnetmod::mysql::connection_pool pool{*io, options};
        std::array<cnetmod::cancel_token, 16> tokens;
        std::array<unsigned, 16> completions{};
        std::atomic<bool> armed{false};
        unsigned completed = 0;
        std::jthread caller([&]
            {
                armed.wait(false);
                for (std::size_t index = tokens.size(); index > 0; --index)
                    tokens[index - 1].cancel();
            });
        auto acquire = [&](std::size_t index) -> cnetmod::task<void>
        {
            auto result = co_await pool.async_get_connection(tokens[index]);
            ASSERT_FALSE(result.has_value());
            ASSERT_TRUE(result.error() == std::errc::operation_canceled);
            ++completions[index];
            if (++completed == tokens.size())
                io->stop();
        };
        auto shutdown = [&]() -> cnetmod::task<void>
        {
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
            ASSERT_EQ(pool.waiter_count(), tokens.size());
            armed.store(true);
            armed.notify_one();
            co_await pool.cancel();
        };
        for (std::size_t index = 0; index < tokens.size(); ++index)
            cnetmod::spawn(*io, acquire(index));
        cnetmod::spawn(*io, shutdown());
        io->run();
        caller.join();
        ASSERT_EQ(pool.waiter_count(), 0U);
        for (auto count : completions)
            ASSERT_EQ(count, 1U);
    }
}

RUN_TESTS()
