# MySQL Transaction Management

cnetmod provides elegant coroutine-style transaction management with automatic commit and rollback.

## Features

- Automatic commit/rollback
- Lambda-style transaction blocks
- Transaction isolation level support
- Exception-safe (RAII style)
- Supports both raw SQL and ORM

## Quick Start

### 1. Using `client::transaction` (Raw SQL)

```cpp
auto rs = co_await cli.transaction([&]() -> cn::task<void> {
    co_await cli.execute("INSERT INTO users (name) VALUES ('Alice')");
    co_await cli.execute("INSERT INTO orders (user_id) VALUES (LAST_INSERT_ID())");
    co_return;
});

if (rs.is_err()) {
    std::println("Transaction failed: {}", rs.error_msg);
}
```

### 2. Using `db_session::transaction` (ORM Style)

```cpp
User user;
user.name = "Bob";

Order order;
order.product = "Laptop";

auto rs = co_await db.transaction([&]() -> cn::task<void> {
    co_await db.insert(user);
    order.user_id = user.id;
    co_await db.insert(order);
    co_return;
});
```

### 3. Specifying Isolation Level

```cpp
auto rs = co_await db.transaction([&]() -> cn::task<void> {
    co_await db.insert(user);
    co_return;
}, mysql::isolation_level::serializable);
```

## Isolation Levels

Supported isolation levels:

```cpp
enum class isolation_level {
    read_uncommitted,   // Read Uncommitted
    read_committed,     // Read Committed
    repeatable_read,    // Repeatable Read (MySQL default)
    serializable        // Serializable
};
```

## Automatic Rollback

When an exception is thrown within a transaction block, the transaction is automatically rolled back:

```cpp
auto rs = co_await db.transaction([&]() -> cn::task<void> {
    co_await db.insert(user);
    
    if (user.balance < 100) {
        throw std::runtime_error("insufficient balance");
    }
    
    co_await db.insert(order);
    co_return;
});
// If an exception is thrown, neither user nor order will be inserted
```

## Complete Example

```cpp
#include <cnetmod/config.hpp>
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.mysql;
#include <cnetmod/orm.hpp>

namespace cn = cnetmod;
namespace mysql = cn::mysql;
namespace orm = mysql::orm;

struct User {
    std::int64_t id = 0;
    std::string name;
    double balance = 0.0;
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(balance, "balance", double_)
)

struct Order {
    std::int64_t id = 0;
    std::int64_t user_id = 0;
    double amount = 0.0;
};

CNETMOD_MODEL(Order, "orders",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(user_id, "user_id", bigint),
    CNETMOD_FIELD(amount, "amount", double_)
)

auto transfer_money(orm::db_session& db, 
                   std::int64_t from_id, 
                   std::int64_t to_id, 
                   double amount) -> cn::task<void> {
    auto rs = co_await db.transaction([&]() -> cn::task<void> {
        // Query sender
        auto from_rs = co_await db.find_by_id<User>(
            mysql::param_value::from_int(from_id));
        if (from_rs.is_err() || from_rs.empty()) {
            throw std::runtime_error("from user not found");
        }
        auto from_user = from_rs.data[0];
        
        // Check balance
        if (from_user.balance < amount) {
            throw std::runtime_error("insufficient balance");
        }
        
        // Query receiver
        auto to_rs = co_await db.find_by_id<User>(
            mysql::param_value::from_int(to_id));
        if (to_rs.is_err() || to_rs.empty()) {
            throw std::runtime_error("to user not found");
        }
        auto to_user = to_rs.data[0];
        
        // Update balances
        from_user.balance -= amount;
        to_user.balance += amount;
        
        co_await db.update(from_user);
        co_await db.update(to_user);
        
        // Record order
        Order order;
        order.user_id = from_id;
        order.amount = amount;
        co_await db.insert(order);
        
        co_return;
    }, mysql::isolation_level::serializable);
    
    if (rs.is_err()) {
        throw std::runtime_error(rs.error_msg);
    }
}
```

## Best Practices

1. **Exception Handling**: Any exception in the transaction block triggers a rollback
2. **Nested Transactions**: MySQL doesn't support true nested transactions, avoid calling `transaction` within a transaction
3. **Long Transactions**: Avoid time-consuming operations in transactions, keep them short
4. **Deadlocks**: Be aware of deadlock risks when using `serializable` isolation level

## API Reference

### `client::transaction`

```cpp
template <typename Func>
auto transaction(Func&& func) -> task<result_set>;

template <typename Func>
auto transaction(Func&& func, isolation_level level) -> task<result_set>;
```

### `db_session::transaction`

```cpp
template <typename Func>
auto transaction(Func&& func) -> task<result_set>;

template <typename Func>
auto transaction(Func&& func, isolation_level level) -> task<result_set>;
```

### Requirements

- `Func` must be a callable object
- `Func` must return `task<void>`
- `Func` can be a lambda, function object, or function pointer

## Running the Example

```bash
# Build
cmake --build build --target mysql_transaction

# Run
./build/examples/mysql_transaction
```
