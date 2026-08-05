#include "test_framework.hpp"
#include <cnetmod/orm.hpp>

import std;
import nlohmann.json;
import cnetmod.orm;

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

RUN_TESTS()
