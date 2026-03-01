/// cnetmod example — MySQL Transaction (事务管理演示)
/// 演示 client::transaction 和 db_session::transaction 的使用
/// 需要本地 MySQL 运行，数据库: mall

#include <cnetmod/config.hpp>
#include <cstdio>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.mysql;

#include <cnetmod/orm.hpp>

namespace cn = cnetmod;
namespace mysql = cn::mysql;
namespace orm = mysql::orm;

// =============================================================================
// 模型定义
// =============================================================================

struct User {
    std::int64_t id       = 0;
    std::string  name;
    std::string  email;
    double       balance  = 0.0;
};

CNETMOD_MODEL(User, "tx_users",
    CNETMOD_FIELD(id,      "id",      bigint,  PK | AUTO_INC),
    CNETMOD_FIELD(name,    "name",    varchar),
    CNETMOD_FIELD(email,   "email",   varchar),
    CNETMOD_FIELD(balance, "balance", double_)
)

struct Order {
    std::int64_t id         = 0;
    std::int64_t user_id    = 0;
    std::string  product;
    double       amount     = 0.0;
    std::string  status;
};

CNETMOD_MODEL(Order, "tx_orders",
    CNETMOD_FIELD(id,       "id",       bigint,  PK | AUTO_INC),
    CNETMOD_FIELD(user_id,  "user_id",  bigint),
    CNETMOD_FIELD(product,  "product",  varchar),
    CNETMOD_FIELD(amount,   "amount",   double_),
    CNETMOD_FIELD(status,   "status",   varchar)
)

// =============================================================================
// Demo 1: 使用 client::transaction (原始 SQL)
// =============================================================================

auto demo_client_transaction(mysql::client& cli) -> cn::task<void> {
    std::println("\n=== Demo 1: client::transaction (原始 SQL) ===");

    // 使用 lambda 执行事务
    auto rs = co_await cli.transaction([&]() -> cn::task<void> {
        // 插入用户
        co_await cli.execute(
            "INSERT INTO tx_users (name, email, balance) VALUES ('Alice', 'alice@example.com', 1000.0)"
        );

        // 插入订单
        co_await cli.execute(
            "INSERT INTO tx_orders (user_id, product, amount, status) "
            "VALUES (LAST_INSERT_ID(), 'Laptop', 800.0, 'pending')"
        );

        // 更新用户余额
        co_await cli.execute(
            "UPDATE tx_users SET balance = balance - 800.0 WHERE id = LAST_INSERT_ID()"
        );

        std::println("  事务内操作完成");
        co_return;
    });

    if (rs.is_err()) {
        std::println("  事务失败: {}", rs.error_msg);
    } else {
        std::println("  事务成功提交");
    }
}

// =============================================================================
// Demo 2: 使用 db_session::transaction (ORM 风格)
// =============================================================================

auto demo_session_transaction(orm::db_session& db) -> cn::task<void> {
    std::println("\n=== Demo 2: db_session::transaction (ORM 风格) ===");

    // 准备数据
    User user;
    user.name    = "Bob";
    user.email   = "bob@example.com";
    user.balance = 2000.0;

    Order order;
    order.product = "Phone";
    order.amount  = 500.0;
    order.status  = "pending";

    // 使用事务
    auto rs = co_await db.transaction([&]() -> cn::task<void> {
        // 插入用户
        auto user_rs = co_await db.insert(user);
        if (user_rs.is_err()) {
            throw std::runtime_error(user_rs.error_msg);
        }
        std::println("  插入用户: id={}, name={}", user.id, user.name);

        // 设置订单的 user_id
        order.user_id = user.id;

        // 插入订单
        auto order_rs = co_await db.insert(order);
        if (order_rs.is_err()) {
            throw std::runtime_error(order_rs.error_msg);
        }
        std::println("  插入订单: id={}, product={}", order.id, order.product);

        // 更新用户余额
        user.balance -= order.amount;
        auto update_rs = co_await db.update(user);
        if (update_rs.is_err()) {
            throw std::runtime_error(update_rs.error_msg);
        }
        std::println("  更新余额: balance={}", user.balance);

        co_return;
    });

    if (rs.is_err()) {
        std::println("  事务失败: {}", rs.error_msg);
    } else {
        std::println("  事务成功提交");
    }
}

// =============================================================================
// Demo 3: 带隔离级别的事务
// =============================================================================

auto demo_transaction_with_isolation(orm::db_session& db) -> cn::task<void> {
    std::println("\n=== Demo 3: 带隔离级别的事务 ===");

    User user;
    user.name    = "Charlie";
    user.email   = "charlie@example.com";
    user.balance = 3000.0;

    // 使用 SERIALIZABLE 隔离级别
    auto rs = co_await db.transaction([&]() -> cn::task<void> {
        auto user_rs = co_await db.insert(user);
        if (user_rs.is_err()) {
            throw std::runtime_error(user_rs.error_msg);
        }
        std::println("  插入用户 (SERIALIZABLE): id={}, name={}", user.id, user.name);
        co_return;
    }, mysql::isolation_level::serializable);

    if (rs.is_err()) {
        std::println("  事务失败: {}", rs.error_msg);
    } else {
        std::println("  事务成功提交 (隔离级别: SERIALIZABLE)");
    }
}

// =============================================================================
// Demo 4: 事务回滚演示
// =============================================================================

auto demo_transaction_rollback(orm::db_session& db) -> cn::task<void> {
    std::println("\n=== Demo 4: 事务回滚演示 ===");

    User user;
    user.name    = "David";
    user.email   = "david@example.com";
    user.balance = 100.0;

    // 故意在事务中抛出异常，触发回滚
    auto rs = co_await db.transaction([&]() -> cn::task<void> {
        auto user_rs = co_await db.insert(user);
        if (user_rs.is_err()) {
            throw std::runtime_error(user_rs.error_msg);
        }
        std::println("  插入用户: id={}, name={}", user.id, user.name);

        // 模拟业务逻辑错误
        if (user.balance < 500.0) {
            std::println("  余额不足，触发回滚");
            throw std::runtime_error("insufficient balance");
        }

        co_return;
    });

    if (rs.is_err()) {
        std::println("  事务已回滚: {}", rs.error_msg);
    } else {
        std::println("  事务成功提交");
    }

    // 验证回滚：查询用户是否存在
    auto find_rs = co_await db.find_all<User>();
    if (find_rs.ok()) {
        bool found = false;
        for (auto& u : find_rs.data) {
            if (u.name == "David") {
                found = true;
                break;
            }
        }
        if (found) {
            std::println("  验证失败: 用户 David 仍然存在（回滚未生效）");
        } else {
            std::println("  验证成功: 用户 David 不存在（回滚已生效）");
        }
    }
}

// =============================================================================
// 入口
// =============================================================================

auto run(cn::io_context& ctx) -> cn::task<void> {
    mysql::client cli(ctx);

    mysql::connect_options opts;
    opts.host     = "127.0.0.1";
    opts.port     = 3306;
    opts.username = "root";
    opts.password = "your_password";
    opts.database = "mall";
    opts.ssl      = mysql::ssl_mode::disable;

    auto rs = co_await cli.connect(std::move(opts));
    if (rs.is_err()) {
        std::println("MySQL 连接失败: {}", rs.error_msg);
        ctx.stop();
        co_return;
    }
    std::println("已连接 MySQL (mall)");

    // 创建 ORM 会话
    orm::db_session db(cli);

    // 清理旧表
    co_await db.drop_table<User>();
    co_await db.drop_table<Order>();

    // 创建表
    co_await db.create_table<User>();
    co_await db.create_table<Order>();
    std::println("表已创建");

    // 运行演示
    co_await demo_client_transaction(cli);
    co_await demo_session_transaction(db);
    co_await demo_transaction_with_isolation(db);
    co_await demo_transaction_rollback(db);

    // 查看最终结果
    std::println("\n=== 最终数据 ===");
    auto users = co_await db.find_all<User>();
    if (users.ok()) {
        std::println("用户列表:");
        for (auto& u : users.data) {
            std::println("  id={}, name={:<10s}, email={:<20s}, balance={}",
                         u.id, u.name, u.email, u.balance);
        }
    }

    auto orders = co_await db.find_all<Order>();
    if (orders.ok()) {
        std::println("订单列表:");
        for (auto& o : orders.data) {
            std::println("  id={}, user_id={}, product={:<10s}, amount={}, status={}",
                         o.id, o.user_id, o.product, o.amount, o.status);
        }
    }

    co_await cli.quit();
    std::println("\nDone.");
    ctx.stop();
}

auto main() -> int {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    try {
        std::fprintf(stderr, "[mysql_transaction] starting...\n");
        std::println("=== cnetmod: MySQL Transaction Demo ===");

        cn::net_init net;
        auto ctx = cn::make_io_context();
        std::fprintf(stderr, "[mysql_transaction] io_context created, spawning...\n");
        cn::spawn(*ctx, run(*ctx));
        ctx->run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[mysql_transaction] EXCEPTION: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "[mysql_transaction] UNKNOWN EXCEPTION\n");
        return 1;
    }

    return 0;
}
