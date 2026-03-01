// Example: Complex XML mapper scenarios
// Tests advanced MyBatis-style features

#include <cnetmod/orm.hpp>

import std;
import cnetmod.io;
import cnetmod.protocol.mysql;
import cnetmod.coro;
import cnetmod.core.log;

using namespace cnetmod;
using namespace cnetmod::mysql;
using namespace cnetmod::mysql::orm;

// Define User model
struct User {
    std::int64_t id = 0;
    std::string name;
    std::optional<std::string> email;
    int status = 0;
    std::time_t created_at = 0;
};

// Register model with ORM
CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(email, "email", varchar, NULLABLE),
    CNETMOD_FIELD(status, "status", int_),
    CNETMOD_FIELD(created_at, "created_at", timestamp, NULLABLE)
)

auto main() -> int {
    auto ctx = make_io_context();

    auto work = [&]() -> task<void> {
        // 1. Connect to MySQL
        connect_options opts;
        opts.host = "1.94.173.250";
        opts.port = 3306;
        opts.username = "root";
        opts.password = "ydc061566";
        opts.database = "yudao_mall";

        client cli(*ctx);
        auto conn_result = co_await cli.connect(opts);
        if (conn_result.is_err()) {
            logger::error("Connection failed: {}", conn_result.error_msg);
            co_return;
        }
        logger::info("Connected to MySQL");

        // 2. Load XML mapper
        mapper_registry registry;
        auto load_result = registry.load_file("mappers/user_mapper.xml");
        if (!load_result) {
            logger::error("Failed to load mapper: {}", load_result.error());
            co_return;
        }

        // 3. Create mapper session
        mapper_session session(cli, registry);
        session.set_sql_logging(true);

        // ========================================================================
        // Test 1: Complex search with nested foreach
        // ========================================================================
        logger::info("\\n=== Test 1: Nested foreach with IN/NOT IN ===");

        param_context ctx1 = param_context::from_map({
            {"keyword", param_value::from_string("test")},
            {"searchMode", param_value::from_string("prefix")},
            {"limit", param_value::from_int(10)},
            {"orderBy", param_value::from_string("`id` ASC")}
        });

        std::vector<param_context> include_ids;
        for (int i = 1; i <= 5; ++i) {
            include_ids.push_back(param_context::from_map({{"id", param_value::from_int(i)}}));
        }
        ctx1.add_collection("includeIds", std::move(include_ids));

        std::vector<param_context> exclude_ids;
        exclude_ids.push_back(param_context::from_map({{"id", param_value::from_int(999)}}));
        ctx1.add_collection("excludeIds", std::move(exclude_ids));

        auto result1 = co_await session.query<User>("UserMapper.complexSearch", ctx1);
        if (result1.ok()) {
            logger::info("✓ Found {} users", result1.data.size());
        } else {
            logger::error("✗ Query failed: {}", result1.error_msg);
        }

        // ========================================================================
        // Test 2: Batch INSERT
        // ========================================================================
        logger::info("\\n=== Test 2: Batch INSERT ===");

        param_context ctx2 = param_context::from_map({});
        std::vector<param_context> users_batch;

        for (int i = 1; i <= 3; ++i) {
            users_batch.push_back(param_context::from_map({
                {"name", param_value::from_string(std::format("BatchUser{}", i))},
                {"email", param_value::from_string(std::format("batch{}@test.com", i))},
                {"status", param_value::from_int(1)},
                {"created_at", param_value::from_int(std::time(nullptr))}
            }));
        }
        ctx2.add_collection("users", std::move(users_batch));

        auto result2 = co_await session.execute("UserMapper.batchInsert", ctx2);
        if (result2.ok()) {
            logger::info("✓ Batch inserted, affected: {}", result2.affected_rows);
        } else {
            logger::error("✗ Batch insert failed: {}", result2.error_msg);
        }

        // ========================================================================
        // Test 3: Batch UPDATE with CASE WHEN
        // ========================================================================
        logger::info("\\n=== Test 3: Batch UPDATE with CASE WHEN ===");

        param_context ctx3 = param_context::from_map({
            {"currentTime", param_value::from_string("NOW()")}
        });

        std::vector<param_context> updates;
        updates.push_back(param_context::from_map({
            {"id", param_value::from_int(1)},
            {"status", param_value::from_int(2)}
        }));
        updates.push_back(param_context::from_map({
            {"id", param_value::from_int(2)},
            {"status", param_value::from_int(3)}
        }));
        ctx3.add_collection("updates", std::move(updates));

        auto result3 = co_await session.execute("UserMapper.batchUpdateStatus", ctx3);
        if (result3.ok()) {
            logger::info("✓ Batch updated {} rows", result3.affected_rows);
        } else {
            logger::error("✗ Batch update failed: {}", result3.error_msg);
        }

        // ========================================================================
        // Test 4: Advanced filter with trim and nested choose
        // ========================================================================
        logger::info("\\n=== Test 4: Advanced filter with trim ===");

        auto result4 = co_await session.query<User>("UserMapper.advancedFilter",
            param_context::from_map({
                {"namePattern", param_value::from_string("%test%")},
                {"emailPattern", param_value::from_string("%@test.com%")},
                {"filterType", param_value::from_string("active")},
                {"orderBy", param_value::from_string("`created_at` DESC")},
                {"limit", param_value::from_int(10)},
                {"offset", param_value::from_int(0)}
            }));

        if (result4.ok()) {
            logger::info("✓ Advanced filter returned {} results", result4.data.size());
        } else {
            logger::error("✗ Query failed: {}", result4.error_msg);
        }

        // ========================================================================
        // Test 5: Multiple search modes (exact/prefix/contains)
        // ========================================================================
        logger::info("\\n=== Test 5: Search modes ===");

        for (const auto& mode : {"exact", "prefix", "contains"}) {
            param_context ctx5 = param_context::from_map({
                {"keyword", param_value::from_string("test")},
                {"searchMode", param_value::from_string(mode)},
                {"limit", param_value::from_int(5)},
                {"orderBy", param_value::from_string("`id` ASC")}
            });

            auto result5 = co_await session.query<User>("UserMapper.complexSearch", ctx5);
            if (result5.ok()) {
                logger::info("✓ Mode '{}': {} results", mode, result5.data.size());
            } else {
                logger::error("✗ Mode '{}' failed: {}", mode, result5.error_msg);
            }
        }

        logger::info("\\n=== All complex tests completed ===");
    };

    cnetmod::spawn(*ctx, work());
    ctx->run();

    return 0;
}
