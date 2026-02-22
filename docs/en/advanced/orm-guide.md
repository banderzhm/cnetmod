# ORM Guide

Comprehensive guide to cnetmod's Object-Relational Mapping (ORM) system for MySQL.

## Overview

cnetmod ORM provides:
- **Model definition**: Map C++ structs to database tables
- **CRUD operations**: Create, Read, Update, Delete
- **Query builder**: Type-safe query construction
- **Auto-migration**: Automatic schema synchronization
- **ID generation**: Auto-increment, UUID, Snowflake
- **Relationships**: One-to-many, many-to-many (planned)

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

## Next Steps

- **[MySQL Guide](../protocols/mysql.md)** - MySQL client basics
- **[Query Optimization](query-optimization.md)** - Optimize database queries
- **[Transactions](transactions.md)** - Use transactions effectively
- **[Testing](testing.md)** - Test ORM code
