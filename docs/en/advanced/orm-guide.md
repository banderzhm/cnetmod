# ORM Guide

Comprehensive guide to cnetmod's Object-Relational Mapping (ORM) system for MySQL.

## Overview

cnetmod ORM provides:
- **Model definition**: Map C++ structs to database tables
- **CRUD operations**: Create, Read, Update, Delete
- **Query builder**: Type-safe query construction (QueryWrapper)
- **XML Mappers**: MyBatis-style XML configuration with dynamic SQL
- **BaseMapper**: Generic CRUD operations interface
- **Auto-migration**: Automatic schema synchronization
- **ID generation**: Auto-increment, UUID, Snowflake
- **Pagination**: Page-based query results
- **Logical Delete**: Soft delete support
- **Auto Fill**: Automatic timestamp fields
- **Optimistic Lock**: Version-based concurrency control
- **Multi-Tenant**: Tenant isolation support
- **Cache**: Query result caching
- **Enum Handler**: Enum type support
- **Type Handler**: Custom type conversion
- **Performance Analysis**: SQL performance monitoring
- **Code Generator**: Generate code from database tables

## Quick Start

### Define a Model

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

### Basic CRUD

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

## Model Definition

### Field Types

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

### Field Flags

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

### Optional Fields

Use `std::optional<T>` for nullable fields:

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

## CRUD Operations

### Insert

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

### Select

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

### Update

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

### Delete

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

## Query Builder

### WHERE Clause

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

### LIMIT and OFFSET

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

### Complex Queries

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

## ID Generation Strategies

### Auto-Increment (Default)

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

**Snowflake ID format**:
```
| 41 bits: timestamp | 10 bits: machine ID | 12 bits: sequence |
```

Benefits:
- Sortable by time
- Distributed generation
- No database round-trip

## Schema Migration

### Create Table

```cpp
// Create table if not exists
co_await db.create_table<User>();

// Drop and recreate
co_await db.drop_table<User>();
co_await db.create_table<User>();
```

### Auto-Migration

Automatically detect schema changes and apply ALTER TABLE:

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

**Supported migrations**:
- Add column
- Drop column
- Change column type
- Add/drop index
- Change nullability

**Not supported** (requires manual migration):
- Rename column
- Complex type changes
- Data transformations

### Manual Migration

```cpp
// Complex migration
co_await client.query("ALTER TABLE users RENAME COLUMN old_name TO new_name");
co_await client.query("UPDATE users SET status = 'active' WHERE status IS NULL");
```

## Advanced Patterns

### Soft Delete

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

### Timestamps

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

### JSON Fields

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

## XML Mappers (MyBatis-Style)

cnetmod ORM supports MyBatis-style XML mappers for complex SQL queries with dynamic conditions.

### Basic XML Mapper

Create `mappers/user_mapper.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<mapper namespace="UserMapper">
    <!-- Simple SELECT -->
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

### Using XML Mappers

```cpp
// Load mapper
mapper_registry registry;
registry.load_file("mappers/user_mapper.xml");

// Create session
mapper_session session(cli, registry);

// Query
auto result = co_await session.query<User>("UserMapper.findById",
    param_context::from_map({{"id", param_value::from_int(1)}}));

// Execute (INSERT/UPDATE/DELETE)
auto exec_result = co_await session.execute("UserMapper.insertUser", user);
```

### Dynamic SQL with `<if>`

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

### Dynamic SQL with `<where>` and `<trim>`

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

### Dynamic SQL with `<choose>`, `<when>`, `<otherwise>`

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

### Dynamic SQL with `<foreach>`

```xml
<select id="findByIds" resultType="User">
    SELECT * FROM users
    WHERE id IN
    <foreach collection="ids" item="item" open="(" separator="," close=")">
        #{item.id}
    </foreach>
</select>
```

Usage:

```cpp
param_context ctx = param_context::from_map({});
std::vector<param_context> id_list;
for (int id : {1, 2, 3, 4, 5}) {
    id_list.push_back(param_context::from_map({{"id", param_value::from_int(id)}}));
}
ctx.add_collection("ids", std::move(id_list));

auto result = co_await session.query<User>("UserMapper.findByIds", ctx);
```

### Batch INSERT with `<foreach>`

```xml
<insert id="batchInsert">
    INSERT INTO users (name, email, status, created_at)
    VALUES
    <foreach collection="users" item="user" separator=",">
        (#{user.name}, #{user.email}, #{user.status}, #{user.created_at})
    </foreach>
</insert>
```

### Batch UPDATE with CASE WHEN

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

### Selective UPDATE

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

## BaseMapper - Generic CRUD Interface

BaseMapper provides a generic interface for common CRUD operations:

```cpp
base_mapper<User> user_mapper(cli);

// INSERT
User user{.name = "Alice", .email = "alice@example.com"};
auto result = co_await user_mapper.insert(user);

// SELECT by ID
auto user_opt = co_await user_mapper.select_by_id(1);

// SELECT all
auto users = co_await user_mapper.select_list();

// SELECT with condition
query_wrapper<User> wrapper;
wrapper.eq("status", 1);
auto active_users = co_await user_mapper.select_list(wrapper);

// UPDATE by ID
user.name = "Alice Updated";
co_await user_mapper.update_by_id(user);

// DELETE by ID
co_await user_mapper.delete_by_id(1);

// COUNT
auto count = co_await user_mapper.select_count();
```

## QueryWrapper - Fluent Query Builder

QueryWrapper provides a fluent API for building queries:

```cpp
query_wrapper<User> wrapper;

// Equality
wrapper.eq("status", 1);

// Comparison
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

// Logical operators
wrapper.eq("status", 1)
       .and_(query_wrapper<User>{}.like("name", "%test%")
                                   .or_()
                                   .like("email", "%test%"));

// Execute query
auto users = co_await user_mapper.select_list(wrapper);
```

## Pagination

```cpp
base_mapper<User> user_mapper(cli);

query_wrapper<User> wrapper;
wrapper.eq("status", 1);

// Page 1, 10 records per page
auto page_result = co_await user_mapper.select_page(1, 10, wrapper);

std::println("Page {}/{}", page_result.current_page, page_result.total_pages);
std::println("Total records: {}", page_result.total);
std::println("Records on this page: {}", page_result.records.size());

for (auto& user : page_result.records) {
    std::println("User: {}", user.name);
}
```

## Logical Delete (Soft Delete)

Enable soft delete by marking a field with `LOGIC_DELETE` flag:

```cpp
struct User {
    std::int64_t id = 0;
    std::string name;
    std::int32_t deleted = 0;  // 0 = not deleted, 1 = deleted
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(deleted, "deleted", tinyint, LOGIC_DELETE)
)

// Enable logical delete globally
global_logical_delete_interceptor().set_enabled(true);

// DELETE becomes UPDATE users SET deleted = 1
co_await user_mapper.delete_by_id(1);

// SELECT automatically adds WHERE deleted = 0
auto users = co_await user_mapper.select_list();
```

## Auto Fill (Automatic Timestamps)

Automatically fill fields on INSERT/UPDATE:

```cpp
struct User {
    std::int64_t id = 0;
    std::string name;
    std::time_t created_at = 0;  // Auto-fill on INSERT
    std::time_t updated_at = 0;  // Auto-fill on INSERT and UPDATE
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(created_at, "created_at", timestamp, FILL_INSERT),
    CNETMOD_FIELD(updated_at, "updated_at", timestamp, FILL_INSERT_UPDATE)
)

// Register auto-fill
global_auto_fill_interceptor().register_from_metadata<User>();

// Fields are filled automatically
User user{.name = "Alice"};
co_await user_mapper.insert(user);
// created_at and updated_at are set automatically
```

## Optimistic Lock

Prevent concurrent update conflicts using version field:

```cpp
struct User {
    std::int64_t id = 0;
    std::string name;
    std::int32_t version = 0;  // Version field
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(version, "version", int_, VERSION)
)

// Update with version check
auto user = co_await user_mapper.select_by_id(1);
if (user) {
    user->name = "Updated";
    bool success = co_await update_with_version_check(cli, *user);
    if (success) {
        std::println("Update successful, new version: {}", user->version);
    } else {
        std::println("Update failed: version conflict");
    }
}
```

## Multi-Tenant Support

Automatically filter queries by tenant ID:

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

// Set tenant context
{
    tenant_guard guard(1001);  // Tenant ID = 1001
    
    // All queries automatically add WHERE tenant_id = 1001
    auto users = co_await user_mapper.select_list();
}
// Tenant context cleared
```

## Query Cache

Enable second-level cache for query results:

```cpp
auto& cache = global_second_level_cache();
cache.set_enabled(true);

// First query - cache miss
auto users1 = co_await user_mapper.select_list();

// Second query - cache hit (if same query)
auto users2 = co_await user_mapper.select_list();
```

## Enum Handler

Support for enum types:

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

// Register enum handler (int-based)
global_enum_registry().register_int_enum<UserStatus>();

// Or string-based
global_enum_registry().register_string_enum<UserStatus>({
    {UserStatus::INACTIVE, "inactive"},
    {UserStatus::ACTIVE, "active"},
    {UserStatus::BANNED, "banned"}
});
```

## Performance Analysis

Monitor SQL performance:

```cpp
auto& perf = global_performance_interceptor();
perf.set_enabled(true);

// Execute queries
co_await user_mapper.select_list();

// Get statistics
auto [total_queries, slow_queries, total_time] = perf.get_summary();
std::println("Total: {}, Slow: {}, Avg: {}μs",
    total_queries, slow_queries, perf.get_average_time().count());

// Get slow queries
auto slow_list = perf.get_slow_queries();
for (auto& stat : slow_list) {
    std::println("Slow query: {} ({}μs)", stat.sql, stat.execution_time.count());
}
```

## Code Generator

Generate model and mapper code from existing database tables:

```cpp
generator_config config;
config.output_dir = "./generated";
config.namespace_name = "myapp";
config.table_prefix = "t_";

code_generator generator(config);

// Generate code for a table
auto result = co_await generator.generate_all(cli, "users");
if (result) {
    std::println("Code generated in ./generated/");
}
```

## Performance Tips

1. **Use batch insert**: `insert_many()` is much faster than multiple `insert()`
2. **Index frequently queried columns**: Add INDEX flag
3. **Use connection pooling**: Reuse connections
4. **Limit result sets**: Always use `.limit()` for large tables
5. **Use prepared statements**: ORM uses them automatically
6. **Avoid N+1 queries**: Fetch related data in one query
7. **Use transactions**: For multiple related operations

## Best Practices

1. **Always define primary key**: Use `PK` flag
2. **Use `std::optional<T>` for nullable fields**: Type-safe nullability
3. **Set appropriate field types**: Match MySQL types
4. **Add indexes**: For frequently queried columns
5. **Use auto-migration carefully**: Review generated SQL
6. **Version your schema**: Track migrations
7. **Test migrations**: On staging before production

## Examples

See complete examples:
- `examples/mysql_xml_mapper.cpp` - Basic XML mapper usage
- `examples/mysql_xml_complex.cpp` - Advanced XML features
- `examples/mybatis_plus_demo.cpp` - All MyBatis-Plus style features

## Next Steps

- **[MySQL Guide](../protocols/mysql.md)** - MySQL client basics
- **[Examples](../examples.md)** - More ORM examples
