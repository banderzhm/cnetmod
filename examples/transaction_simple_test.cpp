/// Simple test for transaction functionality
/// 简单的事务功能测试

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

struct TestUser {
    std::int64_t id = 0;
    std::string name;
};

CNETMOD_MODEL(TestUser, "test_tx_users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar)
)

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
        std::println("Connection failed: {}", rs.error_msg);
        ctx.stop();
        co_return;
    }
    std::println("Connected to MySQL");

    orm::db_session db(cli);

    // Setup
    co_await db.drop_table<TestUser>();
    co_await db.create_table<TestUser>();
    std::println("Table created");

    // Test 1: Simple transaction
    std::println("\n=== Test 1: Simple Transaction ===");
    TestUser user1;
    user1.name = "Alice";

    auto tx_rs = co_await db.transaction([&]() -> cn::task<void> {
        co_await db.insert(user1);
        std::println("Inserted user: id={}, name={}", user1.id, user1.name);
        co_return;
    });

    if (tx_rs.ok()) {
        std::println("✓ Transaction committed successfully");
    } else {
        std::println("✗ Transaction failed: {}", tx_rs.error_msg);
    }

    // Test 2: Transaction with rollback
    std::println("\n=== Test 2: Transaction Rollback ===");
    TestUser user2;
    user2.name = "Bob";

    auto tx_rs2 = co_await db.transaction([&]() -> cn::task<void> {
        co_await db.insert(user2);
        std::println("Inserted user: id={}, name={}", user2.id, user2.name);
        throw std::runtime_error("Simulated error");
        co_return;
    });

    if (tx_rs2.is_err()) {
        std::println("✓ Transaction rolled back as expected: {}", tx_rs2.error_msg);
    } else {
        std::println("✗ Transaction should have failed");
    }

    // Verify
    auto all = co_await db.find_all<TestUser>();
    std::println("\n=== Final Data ===");
    if (all.ok()) {
        std::println("Users in database: {}", all.data.size());
        for (auto& u : all.data) {
            std::println("  - id={}, name={}", u.id, u.name);
        }
        if (all.data.size() == 1 && all.data[0].name == "Alice") {
            std::println("✓ Rollback verified: only Alice exists");
        } else {
            std::println("✗ Rollback verification failed");
        }
    }

    co_await cli.quit();
    std::println("\nAll tests completed");
    ctx.stop();
}

auto main() -> int {
    try {
        std::println("=== Transaction Simple Test ===");
        cn::net_init net;
        auto ctx = cn::make_io_context();
        cn::spawn(*ctx, run(*ctx));
        ctx->run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "EXCEPTION: %s\n", e.what());
        return 1;
    }
    return 0;
}
