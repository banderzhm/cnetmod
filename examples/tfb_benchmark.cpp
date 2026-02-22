/// cnetmod — TechEmpower Framework Benchmark (TFB)
///
/// Implements the following TFB test types:
///   1. JSON Serialization  — GET /json
///   2. Single DB Query     — GET /db
///   3. Multiple DB Queries — GET /queries?queries=N
///   4. Fortunes            — GET /fortunes
///   5. Database Updates    — GET /updates?queries=N
///   6. Plaintext           — GET /plaintext
///
/// Requirements:
///   - Port 8080
///   - Server and Date headers on every response
///   - No disk logging
///   - Production-grade configuration
///
/// Database: MySQL (TFB World / Fortune tables)
///   CREATE TABLE world (id INT PRIMARY KEY, randomNumber INT NOT NULL);
///   CREATE TABLE fortune (id INT PRIMARY KEY, message VARCHAR(2048) NOT NULL);
///   -- 10000 rows in world (id 1-10000, randomNumber 1-10000)
///   -- 12 rows in fortune

#include <cnetmod/config.hpp>

import std;
import nlohmann.json;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.tcp;
import cnetmod.protocol.http;
import cnetmod.protocol.mysql;

namespace cn = cnetmod;
namespace http = cnetmod::http;
using json = nlohmann::json;

constexpr std::uint16_t PORT = 8080;

// =============================================================================
// Helpers
// =============================================================================

/// Clamp queries parameter to [1, 500]
static auto parse_queries(std::string_view qs) -> int {
    auto val_str = http::parse_query_param(qs, "queries");
    if (val_str.empty()) return 1;
    int n = 1;
    auto [ptr, ec] = std::from_chars(val_str.data(), val_str.data() + val_str.size(), n);
    if (ec != std::errc{}) return 1;
    if (n < 1) return 1;
    if (n > 500) return 500;
    return n;
}

/// Thread-local random engine
static thread_local std::mt19937 rng{std::random_device{}()};

static auto random_world_id() -> int {
    std::uniform_int_distribution<int> dist(1, 10000);
    return dist(rng);
}

/// HTML-escape for Fortunes
static auto html_escape(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;        break;
        }
    }
    return out;
}

// =============================================================================
// Test 1: JSON Serialization — GET /json
// =============================================================================

auto handle_json(http::request_context& ctx) -> cn::task<void> {
    ctx.json(http::status::ok, json{{"message", "Hello, World!"}}.dump());
    co_return;
}

// =============================================================================
// Test 6: Plaintext — GET /plaintext
// =============================================================================

auto handle_plaintext(http::request_context& ctx) -> cn::task<void> {
    ctx.text(http::status::ok, "Hello, World!");
    co_return;
}

// =============================================================================
// Test 2: Single Database Query — GET /db
// =============================================================================

/// Requires a mysql::connection_pool* passed via closure
auto make_db_handler(cn::mysql::connection_pool& pool) -> http::handler_fn {
    return [&pool](http::request_context& ctx) -> cn::task<void> {
        auto conn = co_await pool.async_get_connection();
        if (!conn.valid()) {
            ctx.text(http::status::internal_server_error, "DB connection failed");
            co_return;
        }

        int id = random_world_id();
        auto result = co_await conn->query(
            std::format("SELECT id, randomNumber FROM world WHERE id = {}", id));
        if (result.is_err() || result.rows.empty()) {
            ctx.text(http::status::internal_server_error, "Query failed");
            co_return;
        }

        auto& row = result.rows[0];
        ctx.json(http::status::ok, json{
            {"id",           row[0].get_int64()},
            {"randomNumber", row[1].get_int64()},
        }.dump());
        co_return;
    };
}

// =============================================================================
// Test 3:
// =============================================================================

auto make_queries_handler(cn::mysql::connection_pool& pool) -> http::handler_fn {
    return [&pool](http::request_context& ctx) -> cn::task<void> {
        int n = parse_queries(ctx.query_string());

        auto conn = co_await pool.async_get_connection();
        if (!conn.valid()) {
            ctx.text(http::status::internal_server_error, "DB connection failed");
            co_return;
        }

        json arr = json::array();
        for (int i = 0; i < n; ++i) {
            int id = random_world_id();
            auto result = co_await conn->query(
                std::format("SELECT id, randomNumber FROM world WHERE id = {}", id));
            if (result.is_err() || result.rows.empty()) continue;

            auto& row = result.rows[0];
            arr.push_back({{"id", row[0].get_int64()}, {"randomNumber", row[1].get_int64()}});
        }

        ctx.json(http::status::ok, arr.dump());
        co_return;
    };
}

// =============================================================================
// Test 4: Fortunes — GET /fortunes
// =============================================================================

auto make_fortunes_handler(cn::mysql::connection_pool& pool) -> http::handler_fn {
    return [&pool](http::request_context& ctx) -> cn::task<void> {
        auto conn = co_await pool.async_get_connection();
        if (!conn.valid()) {
            ctx.text(http::status::internal_server_error, "DB connection failed");
            co_return;
        }

        auto result = co_await conn->query("SELECT id, message FROM fortune");
        if (result.is_err()) {
            ctx.text(http::status::internal_server_error, "Query failed");
            co_return;
        }

        // Collect fortunes
        struct fortune {
            int id;
            std::string message;
        };
        std::vector<fortune> fortunes;
        fortunes.reserve(result.rows.size() + 1);
        for (auto& row : result.rows) {
            fortunes.push_back({static_cast<int>(row[0].get_int64()), std::string(row[1].get_string())});
        }

        // Add additional fortune
        fortunes.push_back({0, "Additional fortune added at request time."});

        // Sort by message
        std::ranges::sort(fortunes, [](const fortune& a, const fortune& b) {
            return a.message < b.message;
        });

        // Render HTML
        std::string html;
        html.reserve(4096);
        html += "<!DOCTYPE html><html><head><title>Fortunes</title></head><body>"
                "<table><tr><th>id</th><th>message</th></tr>";
        for (auto& f : fortunes) {
            html += std::format("<tr><td>{}</td><td>{}</td></tr>",
                                f.id, html_escape(f.message));
        }
        html += "</table></body></html>";

        ctx.html(http::status::ok, html);
        co_return;
    };
}

// =============================================================================
// Test 5: Database Updates — GET /updates?queries=N
// =============================================================================

auto make_updates_handler(cn::mysql::connection_pool& pool) -> http::handler_fn {
    return [&pool](http::request_context& ctx) -> cn::task<void> {
        int n = parse_queries(ctx.query_string());

        auto conn = co_await pool.async_get_connection();
        if (!conn.valid()) {
            ctx.text(http::status::internal_server_error, "DB connection failed");
            co_return;
        }

        struct world_row { int id; int random_number; };
        std::vector<world_row> worlds;
        worlds.reserve(n);

        // Read
        for (int i = 0; i < n; ++i) {
            int id = random_world_id();
            auto result = co_await conn->query(
                std::format("SELECT id, randomNumber FROM world WHERE id = {}", id));
            if (result.is_err() || result.rows.empty()) continue;

            auto& row = result.rows[0];
            worlds.push_back({static_cast<int>(row[0].get_int64()), random_world_id()});  // New random number
        }

        // Update
        for (auto& w : worlds) {
            co_await conn->query(
                std::format("UPDATE world SET randomNumber = {} WHERE id = {}",
                            w.random_number, w.id));
        }

        // Build JSON response
        json arr = json::array();
        for (auto& w : worlds)
            arr.push_back({{"id", w.id}, {"randomNumber", w.random_number}});

        ctx.json(http::status::ok, arr.dump());
        co_return;
    };
}

// =============================================================================
// main
// =============================================================================

int main(int argc, char* argv[]) {
    cn::net_init net;

    // Parse DB config from environment or defaults (TFB convention)
    std::string db_host = "tfb-database";
    std::uint16_t db_port = 3306;
    std::string db_user = "benchmarkdbuser";
    std::string db_pass = "benchmarkdbpass";
    std::string db_name = "hello_world";
    unsigned workers = std::thread::hardware_concurrency();
    if (workers == 0) workers = 4;

    // Allow override via env vars
    if (auto* v = std::getenv("DBHOST"))    db_host = v;
    if (auto* v = std::getenv("DBPORT"))    db_port = static_cast<std::uint16_t>(std::atoi(v));
    if (auto* v = std::getenv("DBUSER"))    db_user = v;
    if (auto* v = std::getenv("DBPASS"))    db_pass = v;
    if (auto* v = std::getenv("DBNAME"))    db_name = v;
    if (auto* v = std::getenv("WORKERS"))   workers = static_cast<unsigned>(std::atoi(v));

    // Multi-core server context
    cn::server_context sctx(workers, workers);

    // MySQL connection pool
    cn::mysql::pool_params pool_opts{
        .host = db_host,
        .port = db_port,
        .username = db_user,
        .password = db_pass,
        .database = db_name,
        .max_size = workers * 4,
    };
    cn::mysql::connection_pool db_pool(sctx.accept_io(), pool_opts);
    cn::spawn(sctx.accept_io(), db_pool.async_run());

    // Build router — minimal, no unnecessary middleware for max perf
    http::router router;
    router.get("/json", handle_json);
    router.get("/plaintext", handle_plaintext);
    router.get("/db", make_db_handler(db_pool));
    router.get("/queries", make_queries_handler(db_pool));
    router.get("/fortunes", make_fortunes_handler(db_pool));
    router.get("/updates", make_updates_handler(db_pool));

    // Create server
    http::server srv(sctx);
    auto listen_r = srv.listen("0.0.0.0", PORT);
    if (!listen_r) {
        std::println(std::cerr, "Listen failed: {}", listen_r.error().message());
        return 1;
    }

    srv.set_router(std::move(router));

    // Start
    cn::spawn(sctx.accept_io(), srv.run());
    sctx.run();

    return 0;
}
