// Example: Using MyBatis-Plus style features in cnetmod ORM
//
// This example demonstrates all the new features:
// 1. BaseMapper - Generic CRUD operations
// 2. QueryWrapper - Fluent query builder
// 3. Pagination - Page-based queries
// 4. Logical Delete - Soft delete support
// 5. Auto Fill - Automatic timestamp fields
// 6. Optimistic Lock - Version-based concurrency control
// 7. ResultMap - Custom result mapping
// 8. Cache - Query result caching
// 9. Enum Handler - Enum type support
// 10. TypeHandler - Custom type conversion
// 11. Multi-Tenant - Tenant isolation
// 12. Performance Analysis - SQL performance monitoring
// 13. Code Generator - Generate code from database tables

#include <cnetmod/orm.hpp>

import std;
import cnetmod.io;
import cnetmod.protocol.mysql;
import cnetmod.coro;
import cnetmod.core.log;

using namespace cnetmod;
using namespace cnetmod::mysql;
using namespace cnetmod::mysql::orm;

// =============================================================================
// Define User model with advanced features
// =============================================================================

enum class UserStatus {
    INACTIVE = 0,
    ACTIVE = 1,
    BANNED = 2
};

struct User {
    std::int64_t id = 0;
    std::string name;
    std::optional<std::string> email;
    UserStatus status = UserStatus::INACTIVE;
    std::int32_t version = 0;           // For optimistic lock
    std::int32_t deleted = 0;           // For logical delete
    std::time_t created_at = 0;         // Auto-fill on insert
    std::time_t updated_at = 0;         // Auto-fill on update
    std::int64_t tenant_id = 0;         // For multi-tenant
};

// Register model with ORM
CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar, NONE),
    CNETMOD_FIELD(email, "email", varchar, NULLABLE),
    CNETMOD_FIELD(status, "status", int_, NONE),
    CNETMOD_FIELD(version, "version", int_, VERSION),
    CNETMOD_FIELD(deleted, "deleted", tinyint, LOGIC_DELETE),
    CNETMOD_FIELD(created_at, "created_at", timestamp, FILL_INSERT),
    CNETMOD_FIELD(updated_at, "updated_at", timestamp, FILL_INSERT_UPDATE),
    CNETMOD_FIELD(tenant_id, "tenant_id", bigint, TENANT_ID)
)

auto main() -> int {
    auto ctx = make_io_context();

    auto work = [&]() -> task<void> {
        // Connect to MySQL
        connect_options opts;
        opts.host = "127.0.0.1";
        opts.port = 3306;
        opts.username = "root";
        opts.password = "your_password";
        opts.database = "yudao_mall";

        client cli(*ctx);
        auto conn_result = co_await cli.connect(opts);
        if (conn_result.is_err()) {
            logger::error("Connection failed: {}", conn_result.error_msg);
            co_return;
        }
        logger::info("Connected to MySQL");

        // =====================================================================
        // 1. BaseMapper - Generic CRUD operations
        // =====================================================================
        logger::info("\n=== 1. BaseMapper Demo ===");

        base_mapper<User> user_mapper(cli);

        // Insert
        User new_user;
        new_user.name = "Alice";
        new_user.email = "alice@example.com";
        new_user.status = UserStatus::ACTIVE;

        auto insert_result = co_await user_mapper.insert(new_user);
        if (insert_result.ok()) {
            logger::info("Inserted user, ID: {}", insert_result.last_insert_id);
        }

        // Select by ID
        auto user = co_await user_mapper.select_by_id(1);
        if (user) {
            logger::info("Found user: {}", user->name);
        }

        // Update
        if (user) {
            user->name = "Alice Updated";
            co_await user_mapper.update_by_id(*user);
        }

        // Delete
        co_await user_mapper.delete_by_id(1);

        // =====================================================================
        // 2. QueryWrapper - Fluent query builder
        // =====================================================================
        logger::info("\n=== 2. QueryWrapper Demo ===");

        query_wrapper<User> wrapper;
        wrapper.eq("status", 1)
               .like("name", "%Alice%")
               .gt("created_at", 1609459200)
               .order_by_desc("id")
               .limit(10);

        auto users = co_await user_mapper.select_list(wrapper);
        logger::info("Found {} users", users.size());

        // Complex query with nested conditions
        query_wrapper<User> complex_wrapper;
        complex_wrapper.eq("status", 1)
                       .and_(query_wrapper<User>{}
                           .like("name", "%test%")
                           .or_()
                           .like("email", "%test%"));

        // Count
        auto count = co_await user_mapper.select_count(complex_wrapper);
        logger::info("Total count: {}", count);

        // =====================================================================
        // 3. Pagination
        // =====================================================================
        logger::info("\n=== 3. Pagination Demo ===");

        query_wrapper<User> page_wrapper;
        page_wrapper.eq("status", 1);

        auto page_result = co_await user_mapper.select_page(1, 10, page_wrapper);
        logger::info("Page {}/{}, Total: {}, Records: {}",
            page_result.current_page,
            page_result.total_pages,
            page_result.total,
            page_result.records.size());

        // =====================================================================
        // 4. Logical Delete
        // =====================================================================
        logger::info("\n=== 4. Logical Delete Demo ===");

        // Enable logical delete
        global_logical_delete_interceptor().set_enabled(true);

        // Delete will become UPDATE users SET deleted = 1
        co_await user_mapper.delete_by_id(2);

        // SELECT will automatically add WHERE deleted = 0
        auto active_users = co_await user_mapper.select_list();
        logger::info("Active users: {}", active_users.size());

        // =====================================================================
        // 5. Auto Fill
        // =====================================================================
        logger::info("\n=== 5. Auto Fill Demo ===");

        // Register auto-fill for User model
        global_auto_fill_interceptor().register_from_metadata<User>();

        User auto_fill_user;
        auto_fill_user.name = "Bob";
        // created_at and updated_at will be filled automatically
        global_auto_fill_interceptor().fill_insert_fields(auto_fill_user);

        co_await user_mapper.insert(auto_fill_user);

        // =====================================================================
        // 6. Optimistic Lock
        // =====================================================================
        logger::info("\n=== 6. Optimistic Lock Demo ===");

        auto user_with_version = co_await user_mapper.select_by_id(3);
        if (user_with_version) {
            user_with_version->name = "Updated with version check";

            // Update with version check
            bool success = co_await update_with_version_check(cli, *user_with_version);
            if (success) {
                logger::info("Update successful, new version: {}", user_with_version->version);
            } else {
                logger::error("Update failed: version mismatch (optimistic lock conflict)");
            }
        }

        // =====================================================================
        // 7. ResultMap (used with XML mapper)
        // =====================================================================
        logger::info("\n=== 7. ResultMap Demo ===");

        // ResultMap is typically used with XML mappers
        // See user_mapper.xml for <resultMap> definitions

        // =====================================================================
        // 8. Cache
        // =====================================================================
        logger::info("\n=== 8. Cache Demo ===");

        // Enable second-level cache
        auto& cache = global_second_level_cache();
        cache.set_enabled(true);

        // First query - cache miss
        query_wrapper<User> cache_wrapper;
        cache_wrapper.eq("id", 1);
        auto [sql1, params1] = cache_wrapper.build_select_sql();

        cache_key key1{"select_by_id", sql1, params1};
        auto cached1 = cache.get(key1);
        if (!cached1) {
            logger::info("Cache miss - executing query");
            // Execute query and cache result
            // cache.put(key1, result_set);
        }

        // Second query - cache hit
        auto cached2 = cache.get(key1);
        if (cached2) {
            logger::info("Cache hit!");
        }

        // =====================================================================
        // 9. Enum Handler
        // =====================================================================
        logger::info("\n=== 9. Enum Handler Demo ===");

        // Register enum handler
        global_enum_registry().register_int_enum<UserStatus>();

        // Or use string enum
        global_enum_registry().register_string_enum<UserStatus>({
            {UserStatus::INACTIVE, "inactive"},
            {UserStatus::ACTIVE, "active"},
            {UserStatus::BANNED, "banned"}
        });

        // =====================================================================
        // 10. TypeHandler
        // =====================================================================
        logger::info("\n=== 10. TypeHandler Demo ===");

        // Register custom type handler for JSON
        global_type_handler_registry().register_handler<std::string>(
            std::make_unique<json_type_handler>());

        // Register UUID handler
        global_type_handler_registry().register_handler<std::string>(
            std::make_unique<uuid_type_handler>());

        // =====================================================================
        // 11. Multi-Tenant
        // =====================================================================
        logger::info("\n=== 11. Multi-Tenant Demo ===");

        // Set tenant context
        {
            tenant_guard guard(1001); // Tenant ID = 1001

            // All queries will automatically add WHERE tenant_id = 1001
            auto tenant_users = co_await user_mapper.select_list();
            logger::info("Tenant 1001 users: {}", tenant_users.size());
        }

        // Tenant context cleared after guard scope

        // =====================================================================
        // 12. Performance Analysis
        // =====================================================================
        logger::info("\n=== 12. Performance Analysis Demo ===");

        auto& perf = global_performance_interceptor();
        perf.set_enabled(true);

        // Execute some queries
        {
            performance_timer timer("SELECT * FROM users WHERE id = 1");
            co_await user_mapper.select_by_id(1);
            timer.set_affected_rows(1);
        }

        // Get statistics
        auto [total_queries, slow_queries, total_time] = perf.get_summary();
        logger::info("Total queries: {}, Slow queries: {}, Avg time: {}μs",
            total_queries, slow_queries, perf.get_average_time().count());

        // Get slow queries
        auto slow_list = perf.get_slow_queries();
        for (auto& stat : slow_list) {
            logger::warn("Slow query: {} ({}μs)", stat.sql, stat.execution_time.count());
        }

        // =====================================================================
        // 13. Code Generator
        // =====================================================================
        logger::info("\n=== 13. Code Generator Demo ===");

        generator_config gen_config;
        gen_config.output_dir = "./generated";
        gen_config.namespace_name = "myapp";
        gen_config.table_prefix = "t_";

        code_generator generator(gen_config);

        // Generate code for a table
        auto gen_result = co_await generator.generate_all(cli, "users");
        if (gen_result) {
            logger::info("Code generated successfully in ./generated/");
        } else {
            logger::error("Code generation failed: {}", gen_result.error());
        }

        logger::info("\n=== All demos completed ===");
    };

    cnetmod::spawn(*ctx, work());
    ctx->run();

    return 0;
}