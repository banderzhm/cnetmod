# MySQL

异步 MySQL 客户端，支持 ORM、连接池和预处理语句。

## 基础用法

```cpp
import cnetmod;
import cnetmod.protocol.mysql;
using namespace cnetmod;

task<void> mysql_example(io_context& ctx) {
    mysql::client client(ctx);
    
    // Connect
    co_await client.connect({
        .host = "127.0.0.1",
        .port = 3306,
        .user = "root",
        .password = "password",
        .database = "test"
    });
    
    // Execute query
    auto result = co_await client.query("SELECT * FROM users");
    
    for (const auto& row : result.rows()) {
        std::println("ID: {}, Name: {}",
            row["id"].as_int(),
            row["name"].as_string());
    }
    
    co_await client.close();
}
```

## 预处理语句

```cpp
// Prepare statement
auto stmt = co_await client.prepare(
    "INSERT INTO users (name, email) VALUES (?, ?)"
);

// Execute with parameters
co_await stmt.execute({
    param_value::from_string("Alice"),
    param_value::from_string("alice@example.com")
});

// Reuse statement
co_await stmt.execute({
    param_value::from_string("Bob"),
    param_value::from_string("bob@example.com")
});
```

## 连接池

```cpp
mysql::pool pool(ctx, {
    .host = "127.0.0.1",
    .user = "root",
    .password = "password",
    .database = "test",
    .pool_size = 10
});

task<void> use_pool() {
    // Acquire connection
    auto conn = co_await pool.acquire();
    
    // Use connection
    auto result = co_await conn->query("SELECT * FROM users");
    
    // Automatically returned to pool when conn destroyed
}
```

## ORM

### 定义模型

```cpp
#include <cnetmod/orm.hpp>

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

### CRUD 操作

```cpp
task<void> orm_example(mysql::client& client) {
    orm::db_session db(client);
    
    // CREATE TABLE
    co_await db.create_table<User>();
    
    // INSERT
    User user;
    user.name = "Alice";
    user.email = "alice@example.com";
    co_await db.insert(user);
    std::println("Inserted user with ID: {}", user.id);
    
    // SELECT ALL
    auto users = co_await db.find_all<User>();
    for (const auto& u : users) {
        std::println("User: {} ({})", u.name, u.email.value_or("no email"));
    }
    
    // SELECT BY ID
    auto found = co_await db.find_by_id<User>(param_value::from_int(1));
    if (found) {
        std::println("Found: {}", found->name);
    }
    
    // UPDATE
    user.name = "Alice Smith";
    co_await db.update(user);
    
    // DELETE
    co_await db.remove(user);
}
```

### 查询构建器

```cpp
// SELECT with WHERE
auto results = co_await db.find(
    orm::select<User>()
        .where("`name` = {}", {param_value::from_string("Alice")})
);

// SELECT with ORDER BY and LIMIT
auto top_users = co_await db.find(
    orm::select<User>()
        .where("`email` IS NOT NULL")
        .order_by("`created_at` DESC")
        .limit(10)
        .offset(0)
);

// COUNT
auto count = co_await db.count<User>(
    orm::select<User>()
        .where("`email` IS NOT NULL")
);
```

### 批量操作

```cpp
// Batch insert
std::vector<User> users = {
    {.name = "Alice", .email = "alice@example.com"},
    {.name = "Bob", .email = "bob@example.com"},
    {.name = "Charlie", .email = "charlie@example.com"}
};

co_await db.insert_many(users);

// Batch delete
co_await db.remove(
    orm::delete_of<User>()
        .where("`created_at` < {}", {param_value::from_int(old_timestamp)})
);
```

### 自动迁移

```cpp
// Detect schema changes and apply ALTER TABLE
co_await orm::sync_schema<User>(client);

// Example: Add new field to struct
struct User {
    std::int64_t id = 0;
    std::string name;
    std::optional<std::string> email;
    std::string phone;  // NEW FIELD
};

// sync_schema will execute: ALTER TABLE users ADD COLUMN phone VARCHAR(255)
co_await orm::sync_schema<User>(client);
```

### UUID 主键

```cpp
struct Tag {
    orm::uuid id;
    std::string name;
};

CNETMOD_MODEL(Tag, "tags",
    CNETMOD_FIELD(id, "id", char_, UUID_PK_FLAGS, UUID_PK_STRATEGY),
    CNETMOD_FIELD(name, "name", varchar)
)

// Usage
Tag tag;
tag.name = "important";
co_await db.insert(tag);
// tag.id automatically generated (e.g., "550e8400-e29b-41d4-a716-446655440000")
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

// Setup
orm::snowflake_generator sf(/*machine_id=*/1);
orm::db_session db(client, sf);

// Usage
Event event;
event.title = "Conference";
co_await db.insert(event);
// event.id automatically generated (e.g., 1234567890123456789)
```

## 事务

```cpp
task<void> transaction_example(mysql::client& client) {
    // Begin transaction
    co_await client.query("START TRANSACTION");
    
    try {
        // Execute queries
        co_await client.query("UPDATE accounts SET balance = balance - 100 WHERE id = 1");
        co_await client.query("UPDATE accounts SET balance = balance + 100 WHERE id = 2");
        
        // Commit
        co_await client.query("COMMIT");
    } catch (...) {
        // Rollback on error
        co_await client.query("ROLLBACK");
        throw;
    }
}
```

## 管道（批量查询）

```cpp
mysql::pipeline pipe(client);

// Queue multiple queries
pipe.add("SELECT * FROM users WHERE id = 1");
pipe.add("SELECT * FROM posts WHERE user_id = 1");
pipe.add("SELECT * FROM comments WHERE user_id = 1");

// Execute all at once
auto results = co_await pipe.execute();

// Process results
for (const auto& result : results) {
    for (const auto& row : result.rows()) {
        // Process row...
    }
}
```

## 错误处理

```cpp
try {
    co_await client.query("SELECT * FROM non_existent_table");
} catch (const mysql::table_not_found& e) {
    std::println("Table not found: {}", e.what());
} catch (const mysql::syntax_error& e) {
    std::println("SQL syntax error: {}", e.what());
} catch (const mysql::connection_error& e) {
    std::println("Connection error: {}", e.what());
} catch (const std::exception& e) {
    std::println("Error: {}", e.what());
}
```

## 性能提示

1. **使用连接池**处理并发请求
2. **使用预处理语句**处理重复查询
3. **使用 `insert_many()` 批量插入**
4. **使用管道**处理多个独立查询
5. **索引频繁查询的列**
6. **对大结果集使用 `LIMIT`**
7. **在 MySQL 配置中启用查询缓存**

## 下一步

- **[HTTP 服务器](http.md)** - 使用 MySQL 构建 REST API
- **[Redis](redis.md)** - 缓存 MySQL 查询
- **[ORM 指南](../advanced/orm-guide.md)** - 高级 ORM 特性
