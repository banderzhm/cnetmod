# ORM 指南

cnetmod 的 MySQL 对象关系映射（ORM）系统综合指南。

## 概述

cnetmod ORM 提供：
- **模型定义**：将 C++ 结构映射到数据库表
- **CRUD 操作**：创建、读取、更新、删除
- **查询构建器**：类型安全的查询构造
- **自动迁移**：自动模式同步
- **ID 生成**：自动递增、UUID、Snowflake
- **关系**：一对多、多对多（计划中）

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

## 下一步

- **[MySQL 指南](../protocols/mysql.md)** - MySQL 客户端基础
- **[查询优化](query-optimization.md)** - 优化数据库查询
- **[事务](transactions.md)** - 有效使用事务
- **[测试](testing.md)** - 测试 ORM 代码
