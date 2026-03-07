// Example: Using MyBatis-style XML mappers with cnetmod ORM
//
// Compile: Add this file to your CMakeLists.txt
// Run: Ensure MySQL server is running and update connection details

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

// Register model with ORM (must be at global scope)
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

        // 2. Load XML mapper files
        mapper_registry registry;

        // Debug: Print current working directory
        auto cwd = std::filesystem::current_path();
        logger::info("Current working directory: {}", cwd.string());

        // Try multiple paths (relative to build directory where the executable runs)
        std::vector<std::string> paths = {
            "mappers/user_mapper.xml",                           // Subdirectory (CMake copies here)
            "user_mapper.xml",                                   // Same directory as executable
        };

        bool loaded = false;
        for (const auto& path : paths) {
            // Debug: Check if file exists
            bool exists = std::filesystem::exists(path);
            logger::info("Trying path: {} (exists: {})", path, exists);

            auto load_result = registry.load_file(path);
            if (load_result) {
                logger::info("Loaded mapper from: {}", path);
                loaded = true;
                break;
            } else {
                logger::error("Failed to load {}: {}", path, load_result.error());
            }
        }

        if (!loaded) {
            logger::error("Failed to load mapper XML file. Tried paths:");
            for (const auto& path : paths) {
                logger::error("  - {}", path);
            }
            co_return;
        }

        // 3. Auto-create table if not exists
        logger::info("Syncing table schema...");
        auto sync_result = co_await sync_schema<User>(cli);
        if (sync_result.is_err()) {
            logger::error("Failed to sync schema: {}", sync_result.error_msg);
            co_return;
        }
        if (sync_result.created) {
            logger::info("Table 'users' created successfully");
        } else if (!sync_result.diff.changes.empty()) {
            logger::info("Applied {} schema changes", sync_result.diff.changes.size());
        } else {
            logger::info("Table schema is up to date");
        }

        // 4. Create mapper session
        mapper_session session(cli, registry);

        // Enable SQL logging to see generated SQL
        session.set_sql_logging(true);

        // ========================================================================
        // Example 1: Query with explicit params (map)
        // ========================================================================
        logger::info("\n=== Example 1: Query with map params ===");
        auto result1 = co_await session.query<User>("UserMapper.findByCondition",
            param_context::from_map({
                {"name",   param_value::from_string("Alice")},
                {"status", param_value::from_int(1)},
                {"limit",  param_value::from_int(10)}
            }));

        if (result1.ok()) {
            logger::info("Found {} users:", result1.data.size());
            for (auto& user : result1.data) {
                logger::info("  - ID: {}, Name: {}, Email: {}, Status: {}",
                    user.id, user.name,
                    user.email.value_or("(null)"), user.status);
            }
        } else {
            logger::error("Query failed: {}", result1.error_msg);
        }

        // ========================================================================
        // Example 2: Query with model as param source
        // ========================================================================
        logger::info("\n=== Example 2: Query with model params ===");
        User search_model;
        search_model.name = "Bob";
        search_model.status = 1;

        auto result2 = co_await session.query<User>("UserMapper.findByCondition", search_model);
        if (result2.ok()) {
            logger::info("Found {} users matching model", result2.data.size());
        } else {
            logger::error("Query failed: {}", result2.error_msg);
        }

        // ========================================================================
        // Example 3: INSERT with model
        // ========================================================================
        logger::info("\n=== Example 3: INSERT ===");
        User new_user;
        new_user.name = "Charlie";
        new_user.email = "charlie@example.com";
        new_user.status = 1;
        new_user.created_at = std::time(nullptr);

        auto insert_result = co_await session.execute("UserMapper.insertUser", new_user);
        if (insert_result.ok()) {
            logger::info("Inserted user, last_insert_id: {}", insert_result.last_insert_id);
        } else {
            logger::error("Insert failed: {}", insert_result.error_msg);
        }

        // ========================================================================
        // Example 4: UPDATE with selective fields
        // ========================================================================
        logger::info("\n=== Example 4: UPDATE selective ===");
        auto update_result = co_await session.execute("UserMapper.updateSelective",
            param_context::from_map({
                {"id",     param_value::from_int(1)},
                {"email",  param_value::from_string("newemail@example.com")},
                // name and status are null, so they won't be updated
            }));

        if (update_result.ok()) {
            logger::info("Updated {} rows", update_result.affected_rows);
        } else {
            logger::error("Update failed: {}", update_result.error_msg);
        }

        // ========================================================================
        // Example 5: foreach with collection
        // ========================================================================
        logger::info("\n=== Example 5: foreach (IN clause) ===");

        // Build collection of IDs
        param_context ctx_with_collection = param_context::from_map({});
        std::vector<param_context> id_list;
        for (int i = 1; i <= 5; ++i) {
            param_context item = param_context::from_map({
                {"id", param_value::from_int(i)}
            });
            id_list.push_back(std::move(item));
        }
        ctx_with_collection.add_collection("ids", std::move(id_list));

        auto result5 = co_await session.query<User>("UserMapper.findByIds", ctx_with_collection);
        if (result5.ok()) {
            logger::info("Found {} users by IDs", result5.data.size());
        } else {
            logger::error("Query failed: {}", result5.error_msg);
        }

        // ========================================================================
        // Example 6: choose/when/otherwise
        // ========================================================================
        logger::info("\n=== Example 6: choose/when/otherwise ===");
        auto result6 = co_await session.query<User>("UserMapper.findByRole",
            param_context::from_map({
                {"role", param_value::from_string("admin")}
            }));

        if (result6.ok()) {
            logger::info("Found {} admin users", result6.data.size());
        } else {
            logger::error("Query failed: {}", result6.error_msg);
        }

        // ========================================================================
        // Example 7: Complex search with multiple conditions
        // ========================================================================
        logger::info("\n=== Example 7: Complex search ===");
        auto result7 = co_await session.query<User>("UserMapper.searchUsers",
            param_context::from_map({
                {"keyword",  param_value::from_string("%test%")},
                {"status",   param_value::from_int(1)},
                {"limit",    param_value::from_int(20)},
                {"offset",   param_value::from_int(0)},
                {"orderBy",  param_value::from_string("`created_at` DESC")}
            }));

        if (result7.ok()) {
            logger::info("Search returned {} results", result7.data.size());
        } else {
            logger::error("Query failed: {}", result7.error_msg);
        }

        // ========================================================================
        // Example 8: DELETE
        // ========================================================================
        logger::info("\n=== Example 8: DELETE ===");
        auto delete_result = co_await session.execute("UserMapper.deleteByStatus",
            param_context::from_map({
                {"status", param_value::from_int(0)}
            }));

        if (delete_result.ok()) {
            logger::info("Deleted {} inactive users", delete_result.affected_rows);
        } else {
            logger::error("Delete failed: {}", delete_result.error_msg);
        }

        logger::info("\n=== All examples completed ===");
    };

    cnetmod::spawn(*ctx, work());
    ctx->run();

    return 0;
}