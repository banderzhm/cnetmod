# MySQL 事务管理

cnetmod 提供了优雅的协程风格事务管理，支持自动提交和回滚。

## 特性

- 自动提交/回滚
- Lambda 风格的事务块
- 支持事务隔离级别
- 异常安全（RAII 风格）
- 同时支持原始 SQL 和 ORM

## 快速开始

### 1. 使用 `client::transaction` (原始 SQL)

```cpp
auto rs = co_await cli.transaction([&]() -> cn::task<void> {
    co_await cli.execute("INSERT INTO users (name) VALUES ('Alice')");
    co_await cli.execute("INSERT INTO orders (user_id) VALUES (LAST_INSERT_ID())");
    co_return;
});

if (rs.is_err()) {
    std::println("事务失败: {}", rs.error_msg);
}
```

### 2. 使用 `db_session::transaction` (ORM 风格)

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

### 3. 指定事务隔离级别

```cpp
auto rs = co_await db.transaction([&]() -> cn::task<void> {
    co_await db.insert(user);
    co_return;
}, mysql::isolation_level::serializable);
```

## 隔离级别

支持的隔离级别：

```cpp
enum class isolation_level {
    read_uncommitted,   // 读未提交
    read_committed,     // 读已提交
    repeatable_read,    // 可重复读（MySQL 默认）
    serializable        // 串行化
};
```

## 自动回滚

当事务块中抛出异常时，事务会自动回滚：

```cpp
auto rs = co_await db.transaction([&]() -> cn::task<void> {
    co_await db.insert(user);
    
    if (user.balance < 100) {
        throw std::runtime_error("insufficient balance");
    }
    
    co_await db.insert(order);
    co_return;
});
// 如果抛出异常，user 和 order 都不会被插入
```

## 完整示例

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
        // 查询转出用户
        auto from_rs = co_await db.find_by_id<User>(
            mysql::param_value::from_int(from_id));
        if (from_rs.is_err() || from_rs.empty()) {
            throw std::runtime_error("from user not found");
        }
        auto from_user = from_rs.data[0];
        
        // 检查余额
        if (from_user.balance < amount) {
            throw std::runtime_error("insufficient balance");
        }
        
        // 查询转入用户
        auto to_rs = co_await db.find_by_id<User>(
            mysql::param_value::from_int(to_id));
        if (to_rs.is_err() || to_rs.empty()) {
            throw std::runtime_error("to user not found");
        }
        auto to_user = to_rs.data[0];
        
        // 更新余额
        from_user.balance -= amount;
        to_user.balance += amount;
        
        co_await db.update(from_user);
        co_await db.update(to_user);
        
        // 记录订单
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

## 注意事项

1. **异常处理**: 事务块中的任何异常都会触发回滚
2. **嵌套事务**: MySQL 不支持真正的嵌套事务，避免在事务中再次调用 `transaction`
3. **长事务**: 避免在事务中执行耗时操作，保持事务简短
4. **死锁**: 使用 `serializable` 隔离级别时要注意死锁风险

## API 参考

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

### 要求

- `Func` 必须是可调用对象
- `Func` 必须返回 `task<void>`
- `Func` 可以是 lambda、函数对象或函数指针

## 运行示例

```bash
# 编译
cmake --build build --target mysql_transaction

# 运行
./build/examples/mysql_transaction
```
