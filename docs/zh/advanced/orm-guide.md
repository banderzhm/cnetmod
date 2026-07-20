# ORM 指南

cnetmod 的 MySQL 对象关系映射（ORM）系统综合指南。

## 概述

cnetmod ORM 提供：
- **模型定义**：将 C++ 结构映射到数据库表
- **CRUD 操作**：创建、读取、更新、删除
- **查询构建器**：类型安全的查询构造（QueryWrapper）
- **XML 映射器**：MyBatis 风格的 XML 配置与动态 SQL
- **BaseMapper**：通用 CRUD 操作接口
- **自动迁移**：自动模式同步
- **ID 生成**：自动递增、UUID、Snowflake
- **分页**：基于页面的查询结果
- **逻辑删除**：软删除支持
- **自动填充**：自动时间戳字段
- **乐观锁**：基于版本的并发控制
- **多租户**：租户隔离支持
- **缓存**：查询结果缓存
- **枚举处理器**：枚举类型支持
- **类型处理器**：自定义类型转换
- **性能分析**：SQL 性能监控
- **代码生成器**：从数据库表生成代码

## 快速开始

### 定义模型

```cpp
#include <cnetmod/orm.hpp>
import cnetmod.protocol.mysql;

struct User {
    std::int64_t id = 0;
    std::string name;
    std::optional<std::string> email;
    std::time_t created_at = 0;
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(email, "email", varchar, NULLABLE),
    CNETMOD_FIELD(created_at, "created_at", timestamp)
)
```

### 基础 CRUD

```cpp
task<void> crud_example(mysql::client& client) {
    orm::db_session db(client);
    
    // CREATE TABLE
    co_await db.create_table<User>();
    
    // INSERT
    User user{.name = "Alice", .email = "alice@example.com"};
    co_await db.insert(user);
    std::println("Inserted user ID: {}", user.id);
    
    // SELECT
    auto users = co_await db.find_all<User>();
    
    // UPDATE
    user.name = "Alice Smith";
    co_await db.update(user);
    
    // DELETE
    co_await db.remove(user);
}
```

## 模型定义

### 字段类型

```cpp
// Integer types
CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC)
CNETMOD_FIELD(age, "age", int_)
CNETMOD_FIELD(count, "count", smallint)
CNETMOD_FIELD(tiny, "tiny", tinyint)

// String types
CNETMOD_FIELD(name, "name", varchar)
CNETMOD_FIELD(description, "description", text)
CNETMOD_FIELD(content, "content", longtext)

// Floating point
CNETMOD_FIELD(price, "price", decimal)
CNETMOD_FIELD(rating, "rating", float_)
CNETMOD_FIELD(score, "score", double_)

// Date/Time
CNETMOD_FIELD(created_at, "created_at", timestamp)
CNETMOD_FIELD(updated_at, "updated_at", datetime)
CNETMOD_FIELD(birth_date, "birth_date", date)

// Binary
CNETMOD_FIELD(data, "data", blob)
CNETMOD_FIELD(file, "file", longblob)

// Boolean
CNETMOD_FIELD(is_active, "is_active", bool_)

// JSON (MySQL 5.7+)
CNETMOD_FIELD(metadata, "metadata", json)
```

### 字段标志

```cpp
// Primary key
CNETMOD_FIELD(id, "id", bigint, PK)

// Auto-increment
CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC)

// Nullable
CNETMOD_FIELD(email, "email", varchar, NULLABLE)

// Unique
CNETMOD_FIELD(username, "username", varchar, UNIQUE)

// Default value
CNETMOD_FIELD(status, "status", varchar, DEFAULT("active"))

// Not null
CNETMOD_FIELD(name, "name", varchar, NOT_NULL)

// Index
CNETMOD_FIELD(email, "email", varchar, INDEX)

// Combine flags
CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC | NOT_NULL)
```

### 可选字段

对可空字段使用 `std::optional<T>`：

```cpp
struct User {
    std::int64_t id = 0;
    std::string name;                    // NOT NULL
    std::optional<std::string> email;    // NULLABLE
    std::optional<int> age;              // NULLABLE
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar, NOT_NULL),
    CNETMOD_FIELD(email, "email", varchar, NULLABLE),
    CNETMOD_FIELD(age, "age", int_, NULLABLE)
)
```

## CRUD 操作

### 插入

```cpp
// Single insert
User user{.name = "Alice", .email = "alice@example.com"};
co_await db.insert(user);
// user.id is now set (auto-increment)

// Batch insert
std::vector<User> users = {
    {.name = "Alice", .email = "alice@example.com"},
    {.name = "Bob", .email = "bob@example.com"},
    {.name = "Charlie", .email = "charlie@example.com"}
};
co_await db.insert_many(users);
// All IDs are set
```

### 查询

```cpp
// Find all
auto all_users = co_await db.find_all<User>();

// Find by ID
auto user = co_await db.find_by_id<User>(param_value::from_int(1));
if (user) {
    std::println("Found: {}", user->name);
}

// Find with condition
auto results = co_await db.find(
    orm::select<User>()
        .where("`name` = {}", {param_value::from_string("Alice")})
);

// Count
auto count = co_await db.count<User>();
```

### 更新

```cpp
// Update by primary key
User user = *co_await db.find_by_id<User>(param_value::from_int(1));
user.name = "Alice Smith";
co_await db.update(user);

// Update with condition
co_await db.update(
    orm::update<User>()
        .set("`name` = {}", {param_value::from_string("Bob")})
        .where("`id` = {}", {param_value::from_int(2)})
);
```

### 删除

```cpp
// Delete by model
User user = *co_await db.find_by_id<User>(param_value::from_int(1));
co_await db.remove(user);

// Delete by ID
co_await db.remove_by_id<User>(param_value::from_int(1));

// Delete with condition
co_await db.remove(
    orm::delete_of<User>()
        .where("`created_at` < {}", {param_value::from_int(old_timestamp)})
);
```

## 查询构建器

### WHERE 子句

```cpp
// Simple condition
auto results = co_await db.find(
    orm::select<User>()
        .where("`name` = {}", {param_value::from_string("Alice")})
);

// Multiple conditions (AND)
auto results = co_await db.find(
    orm::select<User>()
        .where("`name` = {} AND `age` > {}", {
            param_value::from_string("Alice"),
            param_value::from_int(18)
        })
);

// OR condition
auto results = co_await db.find(
    orm::select<User>()
        .where("`name` = {} OR `email` = {}", {
            param_value::from_string("Alice"),
            param_value::from_string("alice@example.com")
        })
);

// IN clause
auto results = co_await db.find(
    orm::select<User>()
        .where("`id` IN (1, 2, 3)")
);

// LIKE
auto results = co_await db.find(
    orm::select<User>()
        .where("`name` LIKE {}", {param_value::from_string("%Alice%")})
);

// IS NULL / IS NOT NULL
auto results = co_await db.find(
    orm::select<User>()
        .where("`email` IS NOT NULL")
);
```

### ORDER BY

```cpp
// Ascending
auto results = co_await db.find(
    orm::select<User>()
        .order_by("`name` ASC")
);

// Descending
auto results = co_await db.find(
    orm::select<User>()
        .order_by("`created_at` DESC")
);

// Multiple columns
auto results = co_await db.find(
    orm::select<User>()
        .order_by("`age` DESC, `name` ASC")
);
```

### LIMIT 和 OFFSET

```cpp
// First 10 users
auto results = co_await db.find(
    orm::select<User>()
        .limit(10)
);

// Pagination (page 2, 10 per page)
auto results = co_await db.find(
    orm::select<User>()
        .limit(10)
        .offset(10)
);

// Top 5 oldest users
auto results = co_await db.find(
    orm::select<User>()
        .order_by("`created_at` ASC")
        .limit(5)
);
```

### 复杂查询

```cpp
// Combine all features
auto results = co_await db.find(
    orm::select<User>()
        .where("`age` > {} AND `email` IS NOT NULL", {
            param_value::from_int(18)
        })
        .order_by("`created_at` DESC")
        .limit(20)
        .offset(0)
);
```

## ID 生成策略

### 自动递增（默认）

```cpp
struct User {
    std::int64_t id = 0;
    std::string name;
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar)
)

// Usage
User user{.name = "Alice"};
co_await db.insert(user);
// user.id is set by database (e.g., 1, 2, 3, ...)
```

### UUID

```cpp
struct Tag {
    orm::uuid id;  // Special UUID type
    std::string name;
};

CNETMOD_MODEL(Tag, "tags",
    CNETMOD_FIELD(id, "id", char_, UUID_PK_FLAGS, UUID_PK_STRATEGY),
    CNETMOD_FIELD(name, "name", varchar)
)

// Usage
Tag tag{.name = "important"};
co_await db.insert(tag);
// tag.id is generated (e.g., "550e8400-e29b-41d4-a716-446655440000")
```

### Snowflake ID

```cpp
struct Event {
    std::int64_t id = 0;
    std::string title;
};

CNETMOD_MODEL(Event, "events",
    CNETMOD_FIELD(id, "id", bigint, SNOWFLAKE_PK_FLAGS, SNOWFLAKE_PK_STRATEGY),
    CNETMOD_FIELD(title, "title", varchar)
)

// Setup: Create Snowflake generator
orm::snowflake_generator sf(/*machine_id=*/1);
orm::db_session db(client, sf);

// Usage
Event event{.title = "Conference"};
co_await db.insert(event);
// event.id is generated (e.g., 1234567890123456789)
```

**Snowflake ID 格式**：
```
| 41 bits: timestamp | 10 bits: machine ID | 12 bits: sequence |
```

优势：
- 按时间可排序
- 分布式生成
- 无需数据库往返

## 模式迁移

### 创建表

```cpp
// Create table if not exists
co_await db.create_table<User>();

// Drop and recreate
co_await db.drop_table<User>();
co_await db.create_table<User>();
```

### 自动迁移

自动检测模式更改并应用 ALTER TABLE：

```cpp
// Initial schema
struct User {
    std::int64_t id = 0;
    std::string name;
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar)
)

co_await db.create_table<User>();

// Later: Add new field
struct User {
    std::int64_t id = 0;
    std::string name;
    std::string email;  // NEW FIELD
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(email, "email", varchar)  // NEW FIELD
)

// Sync schema (applies ALTER TABLE)
co_await orm::sync_schema<User>(client);
// Executes: ALTER TABLE users ADD COLUMN email VARCHAR(255)
```

**支持的迁移**：
- 添加列
- 删除列
- 更改列类型
- 添加/删除索引
- 更改可空性

**不支持**（需要手动迁移）：
- 重命名列
- 复杂类型更改
- 数据转换

### 手动迁移

```cpp
// Complex migration
co_await client.query("ALTER TABLE users RENAME COLUMN old_name TO new_name");
co_await client.query("UPDATE users SET status = 'active' WHERE status IS NULL");
```

## 高级模式

### 软删除

```cpp
struct User {
    std::int64_t id = 0;
    std::string name;
    std::optional<std::time_t> deleted_at;
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(deleted_at, "deleted_at", timestamp, NULLABLE)
)

// Soft delete
task<void> soft_delete(orm::db_session& db, User& user) {
    user.deleted_at = std::time(nullptr);
    co_await db.update(user);
}

// Query only non-deleted
auto active_users = co_await db.find(
    orm::select<User>()
        .where("`deleted_at` IS NULL")
);
```

### 时间戳

```cpp
struct Post {
    std::int64_t id = 0;
    std::string title;
    std::time_t created_at = 0;
    std::time_t updated_at = 0;
};

CNETMOD_MODEL(Post, "posts",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(title, "title", varchar),
    CNETMOD_FIELD(created_at, "created_at", timestamp),
    CNETMOD_FIELD(updated_at, "updated_at", timestamp)
)

// Auto-set timestamps
task<void> create_post(orm::db_session& db, Post& post) {
    post.created_at = std::time(nullptr);
    post.updated_at = post.created_at;
    co_await db.insert(post);
}

task<void> update_post(orm::db_session& db, Post& post) {
    post.updated_at = std::time(nullptr);
    co_await db.update(post);
}
```

### JSON 字段

```cpp
struct Product {
    std::int64_t id = 0;
    std::string name;
    std::string metadata;  // JSON string
};

CNETMOD_MODEL(Product, "products",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(metadata, "metadata", json)
)

// Usage
Product product;
product.name = "Laptop";
product.metadata = R"({"brand":"Dell","ram":"16GB"})";
co_await db.insert(product);

// Query JSON field (MySQL 5.7+)
auto results = co_await db.find(
    orm::select<Product>()
        .where("JSON_EXTRACT(`metadata`, '$.brand') = 'Dell'")
);
```

## XML 映射器（MyBatis 风格）

cnetmod ORM 支持 MyBatis 风格的 XML 映射器，用于复杂的 SQL 查询和动态条件。

### 基础 XML 映射器

创建 `mappers/user_mapper.xml`：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<mapper namespace="UserMapper">
    <!-- 简单 SELECT -->
    <select id="findById" resultType="User">
        SELECT * FROM users WHERE id = #{id}
    </select>
    
    <!-- INSERT -->
    <insert id="insertUser">
        INSERT INTO users (name, email, status, created_at)
        VALUES (#{name}, #{email}, #{status}, #{created_at})
    </insert>
    
    <!-- UPDATE -->
    <update id="updateUser">
        UPDATE users SET name = #{name}, email = #{email}
        WHERE id = #{id}
    </update>
    
    <!-- DELETE -->
    <delete id="deleteUser">
        DELETE FROM users WHERE id = #{id}
    </delete>
</mapper>
```

### 使用 XML 映射器

```cpp
// 加载映射器
mapper_registry registry;
registry.load_file("mappers/user_mapper.xml");

// 创建会话
mapper_session session(cli, registry);

// 查询
auto result = co_await session.query<User>("UserMapper.findById",
    param_context::from_map({{"id", param_value::from_int(1)}}));

// 执行（INSERT/UPDATE/DELETE）
auto exec_result = co_await session.execute("UserMapper.insertUser", user);
```

### 使用 `<if>` 的动态 SQL

```xml
<select id="findByCondition" resultType="User">
    SELECT * FROM users
    WHERE 1=1
    <if test="name != null">
        AND name = #{name}
    </if>
    <if test="status != null">
        AND status = #{status}
    </if>
    <if test="email != null">
        AND email LIKE #{email}
    </if>
</select>
```

### 使用 `<where>` 和 `<trim>` 的动态 SQL

```xml
<select id="searchUsers" resultType="User">
    SELECT * FROM users
    <where>
        <if test="keyword != null">
            AND (name LIKE #{keyword} OR email LIKE #{keyword})
        </if>
        <if test="status != null">
            AND status = #{status}
        </if>
    </where>
    <if test="orderBy != null">
        ORDER BY ${orderBy}
    </if>
    <if test="limit != null">
        LIMIT #{limit}
    </if>
</select>
```

### 使用 `<choose>`、`<when>`、`<otherwise>` 的动态 SQL

```xml
<select id="findByRole" resultType="User">
    SELECT * FROM users
    WHERE 1=1
    <choose>
        <when test="role == 'admin'">
            AND status = 2
        </when>
        <when test="role == 'user'">
            AND status = 1
        </when>
        <otherwise>
            AND status = 0
        </otherwise>
    </choose>
</select>
```

### 使用 `<foreach>` 的动态 SQL

```xml
<select id="findByIds" resultType="User">
    SELECT * FROM users
    WHERE id IN
    <foreach collection="ids" item="item" open="(" separator="," close=")">
        #{item.id}
    </foreach>
</select>
```

使用方法：

```cpp
param_context ctx = param_context::from_map({});
std::vector<param_context> id_list;
for (int id : {1, 2, 3, 4, 5}) {
    id_list.push_back(param_context::from_map({{"id", param_value::from_int(id)}}));
}
ctx.add_collection("ids", std::move(id_list));

auto result = co_await session.query<User>("UserMapper.findByIds", ctx);
```

### 使用 `<foreach>` 的批量 INSERT

```xml
<insert id="batchInsert">
    INSERT INTO users (name, email, status, created_at)
    VALUES
    <foreach collection="users" item="user" separator=",">
        (#{user.name}, #{user.email}, #{user.status}, #{user.created_at})
    </foreach>
</insert>
```

### 使用 CASE WHEN 的批量 UPDATE

```xml
<update id="batchUpdateStatus">
    UPDATE users
    SET status = CASE id
        <foreach collection="updates" item="item">
            WHEN #{item.id} THEN #{item.status}
        </foreach>
    END,
    updated_at = #{currentTime}
    WHERE id IN
    <foreach collection="updates" item="item" open="(" separator="," close=")">
        #{item.id}
    </foreach>
</update>
```

### 选择性 UPDATE

```xml
<update id="updateSelective">
    UPDATE users
    <set>
        <if test="name != null">name = #{name},</if>
        <if test="email != null">email = #{email},</if>
        <if test="status != null">status = #{status},</if>
    </set>
    WHERE id = #{id}
</update>
```

## BaseMapper - 通用 CRUD 接口

BaseMapper 提供通用的 CRUD 操作接口：

```cpp
base_mapper<User> user_mapper(cli);

// INSERT
User user{.name = "Alice", .email = "alice@example.com"};
auto result = co_await user_mapper.insert(user);

// 根据 ID 查询
auto user_opt = co_await user_mapper.select_by_id(1);

// 查询所有
auto users = co_await user_mapper.select_list();

// 带条件查询
query_wrapper<User> wrapper;
wrapper.eq("status", 1);
auto active_users = co_await user_mapper.select_list(wrapper);

// 根据 ID 更新
user.name = "Alice Updated";
co_await user_mapper.update_by_id(user);

// 根据 ID 删除
co_await user_mapper.delete_by_id(1);

// 计数
auto count = co_await user_mapper.select_count();
```

## QueryWrapper - 流式查询构建器

QueryWrapper 提供流式 API 构建查询：

```cpp
query_wrapper<User> wrapper;

// 相等
wrapper.eq("status", 1);

// 比较
wrapper.gt("age", 18)
       .lt("age", 65)
       .ge("score", 60)
       .le("score", 100);

// LIKE
wrapper.like("name", "%Alice%")
       .like_left("email", "@gmail.com")   // %@gmail.com
       .like_right("phone", "138");         // 138%

// IN / NOT IN
wrapper.in("id", {1, 2, 3, 4, 5});
wrapper.not_in("status", {0, -1});

// IS NULL / IS NOT NULL
wrapper.is_null("deleted_at");
wrapper.is_not_null("email");

// BETWEEN
wrapper.between("created_at", 1609459200, 1640995200);

// ORDER BY
wrapper.order_by_asc("name")
       .order_by_desc("created_at");

// LIMIT / OFFSET
wrapper.limit(10).offset(20);

// 逻辑运算符
wrapper.eq("status", 1)
       .and_(query_wrapper<User>{}.like("name", "%test%")
                                   .or_()
                                   .like("email", "%test%"));

// 执行查询
auto users = co_await user_mapper.select_list(wrapper);
```

## 分页

```cpp
base_mapper<User> user_mapper(cli);

query_wrapper<User> wrapper;
wrapper.eq("status", 1);

// 第 1 页，每页 10 条记录
auto page_result = co_await user_mapper.select_page(1, 10, wrapper);

std::println("第 {}/{} 页", page_result.current_page, page_result.total_pages);
std::println("总记录数: {}", page_result.total);
std::println("本页记录数: {}", page_result.records.size());

for (auto& user : page_result.records) {
    std::println("用户: {}", user.name);
}
```

## 逻辑删除（软删除）

通过 `LOGIC_DELETE` 标志启用软删除：

```cpp
struct User {
    std::int64_t id = 0;
    std::string name;
    std::int32_t deleted = 0;  // 0 = 未删除, 1 = 已删除
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(deleted, "deleted", tinyint, LOGIC_DELETE)
)

// 全局启用逻辑删除
global_logical_delete_interceptor().set_enabled(true);

// DELETE 变成 UPDATE users SET deleted = 1
co_await user_mapper.delete_by_id(1);

// SELECT 自动添加 WHERE deleted = 0
auto users = co_await user_mapper.select_list();
```

## 自动填充（自动时间戳）

在 INSERT/UPDATE 时自动填充字段：

```cpp
struct User {
    std::int64_t id = 0;
    std::string name;
    std::time_t created_at = 0;  // INSERT 时自动填充
    std::time_t updated_at = 0;  // INSERT 和 UPDATE 时自动填充
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(created_at, "created_at", timestamp, FILL_INSERT),
    CNETMOD_FIELD(updated_at, "updated_at", timestamp, FILL_INSERT_UPDATE)
)

// 注册自动填充
global_auto_fill_interceptor().register_from_metadata<User>();

// 字段自动填充
User user{.name = "Alice"};
co_await user_mapper.insert(user);
// created_at 和 updated_at 自动设置
```

## 乐观锁

使用版本字段防止并发更新冲突：

```cpp
struct User {
    std::int64_t id = 0;
    std::string name;
    std::int32_t version = 0;  // 版本字段
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(version, "version", int_, VERSION)
)

// 带版本检查的更新
auto user = co_await user_mapper.select_by_id(1);
if (user) {
    user->name = "Updated";
    bool success = co_await update_with_version_check(cli, *user);
    if (success) {
        std::println("更新成功，新版本: {}", user->version);
    } else {
        std::println("更新失败：版本冲突");
    }
}
```

## 多租户支持

自动按租户 ID 过滤查询：

```cpp
struct User {
    std::int64_t id = 0;
    std::string name;
    std::int64_t tenant_id = 0;
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(tenant_id, "tenant_id", bigint, TENANT_ID)
)

// 设置租户上下文
{
    tenant_guard guard(1001);  // 租户 ID = 1001
    
    // 所有查询自动添加 WHERE tenant_id = 1001
    auto users = co_await user_mapper.select_list();
}
// 租户上下文清除
```

## 查询缓存

启用查询结果的二级缓存：

```cpp
auto& cache = global_second_level_cache();
cache.set_enabled(true);

// 第一次查询 - 缓存未命中
auto users1 = co_await user_mapper.select_list();

// 第二次查询 - 缓存命中（如果是相同查询）
auto users2 = co_await user_mapper.select_list();
```

## 枚举处理器

支持枚举类型：

```cpp
enum class UserStatus {
    INACTIVE = 0,
    ACTIVE = 1,
    BANNED = 2
};

struct User {
    std::int64_t id = 0;
    std::string name;
    UserStatus status = UserStatus::INACTIVE;
};

// 注册枚举处理器（基于整数）
global_enum_registry().register_int_enum<UserStatus>();

// 或基于字符串
global_enum_registry().register_string_enum<UserStatus>({
    {UserStatus::INACTIVE, "inactive"},
    {UserStatus::ACTIVE, "active"},
    {UserStatus::BANNED, "banned"}
});
```

## 性能分析

监控 SQL 性能：

```cpp
auto& perf = global_performance_interceptor();
perf.set_enabled(true);

// 执行查询
co_await user_mapper.select_list();

// 获取统计信息
auto [total_queries, slow_queries, total_time] = perf.get_summary();
std::println("总数: {}, 慢查询: {}, 平均: {}μs",
    total_queries, slow_queries, perf.get_average_time().count());

// 获取慢查询列表
auto slow_list = perf.get_slow_queries();
for (auto& stat : slow_list) {
    std::println("慢查询: {} ({}μs)", stat.sql, stat.execution_time.count());
}
```

## 代码生成器

从现有数据库表生成模型和映射器代码：

```cpp
generator_config config;
config.output_dir = "./generated";
config.namespace_name = "myapp";
config.table_prefix = "t_";

code_generator generator(config);

// 为表生成代码
auto result = co_await generator.generate_all(cli, "users");
if (result) {
    std::println("代码已生成到 ./generated/");
}
```

## 性能提示

1. **使用批量插入**：`insert_many()` 比多次 `insert()` 快得多
2. **索引频繁查询的列**：添加 INDEX 标志
3. **使用连接池**：重用连接
4. **限制结果集**：对大表始终使用 `.limit()`
5. **使用预处理语句**：ORM 自动使用它们
6. **避免 N+1 查询**：在一个查询中获取相关数据
7. **使用事务**：用于多个相关操作

## 最佳实践

1. **始终定义主键**：使用 `PK` 标志
2. **对可空字段使用 `std::optional<T>`**：类型安全的可空性
3. **设置适当的字段类型**：匹配 MySQL 类型
4. **添加索引**：用于频繁查询的列
5. **谨慎使用自动迁移**：审查生成的 SQL
6. **版本化模式**：跟踪迁移
7. **测试迁移**：在生产前在暂存环境测试

## 示例

查看完整示例：
- `examples/mysql_xml_mapper.cpp` - 基础 XML 映射器使用
- `examples/mysql_xml_complex.cpp` - 高级 XML 特性
- `examples/mybatis_plus_demo.cpp` - 所有 MyBatis-Plus 风格特性

## 下一步

- **[MySQL 指南](../protocols/mysql.md)** - MySQL 客户端基础
- **[示例](../examples.md)** - 更多 ORM 示例
