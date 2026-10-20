#include <cnetmod/config.hpp>

import std;
import cnetmod.core.net_init;
import cnetmod.core.log;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.task_group;
import cnetmod.coro.timer;
import cnetmod.protocol.postgresql;
import cnetmod.protocol.mysql;
import cnetmod.protocol.redis;

namespace cn = cnetmod;

// A single executor isolates client cost. Real servers run on separate CPUs.
// Pool-only runs perform no query; protocol runs keep a direct connection.
template <typename Operation>
auto measure(std::string_view database, std::string_view mode,
    unsigned seconds, Operation operation) -> cn::task<void>
{
    const auto start = std::chrono::steady_clock::now();
    const auto end = start + std::chrono::seconds(seconds);
    std::uint64_t count{};
    do
    {
        co_await operation();
        ++count;
    } while (std::chrono::steady_clock::now() < end);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    logger::info{"RESULT database={} mode={} operations={} seconds={:.6f} ops_per_second={:.3f}",
        database, mode, count, elapsed, count / elapsed};
}

void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

auto postgres(cn::io_context& io, std::string mode, unsigned seconds,
    unsigned capacity) -> cn::task<void>
{
    namespace db = cn::postgresql;
    db::connection_options options;
    options.host = "127.0.0.1";
    options.port = 15432;
    options.username = "bench";
    options.password = "bench";
    options.database = "benchmark";
    options.tls = db::tls_mode::disable;
    if (mode == "pool")
    {
        db::connection_pool pool{io, {.connection = options,
            .minimum_connections = capacity, .maximum_connections = capacity}};
        auto ready = co_await pool.warm_up();
        require(ready.ok(), ready.error_msg);
        co_await measure("postgresql", mode, seconds, [&]() -> cn::task<void>
        {
            auto lease = co_await pool.acquire();
            require(lease.has_value() && lease->valid(), "PostgreSQL acquire failed");
        });
        require(pool.idle_count() == capacity && pool.checked_out_count() == 0,
            "PostgreSQL pool accounting mismatch");
        co_await pool.close();
        co_return;
    }
    db::client connection{io};
    auto ready = co_await connection.connect(options);
    require(ready.ok(), ready.error_msg);
    const auto sql = mode == "rows" ? "SELECT i FROM generate_series(1,256) AS i" : "SELECT 42";
    co_await measure("postgresql", mode, seconds, [&]() -> cn::task<void>
    {
        auto result = co_await connection.query(sql);
        require(result.ok(), result.error_msg);
        const auto expected = mode == "rows" ? 256U : 1U;
        require(result.rows.size() == expected && result.rows.front().size() == 1,
            "PostgreSQL row count mismatch");
        require(result.rows.front()[0].as_int64() == (mode == "rows" ? 1 : 42),
            "PostgreSQL value mismatch");
    });
    co_await connection.terminate();
}

auto mysql(cn::io_context& io, std::string mode, unsigned seconds,
    unsigned capacity) -> cn::task<void>
{
    namespace db = cn::mysql;
    db::connect_options options;
    options.host = "127.0.0.1";
    options.port = 13306;
    options.username = "bench";
    options.password = "bench";
    options.database = "benchmark";
    options.ssl = db::ssl_mode::disable;
    if (mode == "pool" || mode == "reset")
    {
        db::pool_params parameters;
        parameters.host = options.host;
        parameters.port = options.port;
        parameters.username = options.username;
        parameters.password = options.password;
        parameters.database = options.database;
        parameters.ssl = options.ssl;
        parameters.initial_size = parameters.max_size = capacity;
        db::connection_pool pool{io, parameters};
        cn::task_group background{io};
        require(background.run([&](cn::cancel_token&) -> cn::task<std::expected<void, std::error_code>> { co_await pool.async_run(); co_return std::expected<void, std::error_code>{}; }),
            "MySQL pool start failed");
        while (pool.idle_count() != capacity)
            co_await cn::async_sleep(io, std::chrono::milliseconds(1));
        co_await measure("mysql", mode, seconds, [&]() -> cn::task<void>
        {
            auto lease = co_await pool.async_get_connection();
            require(lease.has_value() && lease->valid(), "MySQL acquire failed");
            if (mode == "pool")
                lease->return_without_reset();
        });
        co_await pool.cancel();
        (void)co_await background.join();
        co_return;
    }
    db::client connection{io};
    auto ready = co_await connection.connect(options);
    require(ready.ok(), ready.error_msg);
    co_await measure("mysql", mode, seconds, [&]() -> cn::task<void>
    {
        auto result = co_await connection.query("SELECT 42");
        require(result.ok(), result.error_msg);
        require(result.rows.size() == 1 && result.rows[0][0].as_int64() == 42,
            "MySQL value mismatch");
    });
    (void)co_await connection.quit();
}

auto redis(cn::io_context& io, std::string mode, unsigned seconds,
    unsigned capacity) -> cn::task<void>
{
    namespace db = cn::redis;
    if (mode == "pool")
    {
        db::pool_params parameters;
        parameters.port = 16379;
        parameters.initial_size = parameters.max_size = capacity;
        db::connection_pool pool{io, parameters};
        cn::task_group background{io};
        require(background.run([&](cn::cancel_token&) -> cn::task<std::expected<void, std::error_code>> { co_await pool.async_run(); co_return std::expected<void, std::error_code>{}; }),
            "Redis pool start failed");
        while (pool.idle_count() != capacity)
            co_await cn::async_sleep(io, std::chrono::milliseconds(1));
        co_await measure("redis", mode, seconds, [&]() -> cn::task<void>
        {
            auto lease = co_await pool.async_get_connection();
            require(lease.has_value() && lease->valid(), "Redis acquire failed");
        });
        require(pool.idle_count() == capacity && pool.checked_out_count() == 0,
            "Redis pool accounting mismatch");
        co_await pool.cancel();
        (void)co_await background.join();
        co_return;
    }
    db::client connection{io};
    auto ready = co_await connection.connect({.port = 16379});
    require(ready.has_value(), ready ? "" : ready.error());
    db::request batch;
    const auto commands = mode == "batch" ? 64U : 1U;
    for (unsigned i = 0; i < commands; ++i)
        batch.push("ECHO", "computed-payload");
    co_await measure("redis", mode, seconds, [&]() -> cn::task<void>
    {
        auto result = co_await connection.exec(batch);
        require(result.has_value(), result ? "" : result.error());
        require(result->size() == commands, "Redis reply count mismatch");
        for (const auto& node : *result)
            require(node.value == "computed-payload", "Redis reply value mismatch");
    });
    connection.close();
}

auto main(int argc, char** argv) -> int
{
    cn::net_init network;
    logger::init("database-bench", logger::level::info);
    if (argc != 5)
    {
        logger::error{"usage: bench_database <postgresql|mysql|redis> <pool|reset|query|rows|batch> <seconds> <capacity>"};
        return 2;
    }
    auto io = cn::make_io_context();
    bool success = false;
    auto run = [&]() -> cn::task<void>
    {
        try
        {
            const std::string database{argv[1]}, mode{argv[2]};
            const auto seconds = static_cast<unsigned>(std::stoul(argv[3]));
            const auto capacity = static_cast<unsigned>(std::stoul(argv[4]));
            require(seconds > 0 && capacity > 0 && capacity <= 256, "invalid benchmark bounds");
            if (database == "postgresql")
                co_await postgres(*io, mode, seconds, capacity);
            else if (database == "mysql")
                co_await mysql(*io, mode, seconds, capacity);
            else if (database == "redis")
                co_await redis(*io, mode, seconds, capacity);
            else
                require(false, "unknown database");
            success = true;
        }
        catch (const std::exception& error)
        {
            logger::error{"benchmark failed: {}", error.what()};
        }
        io->stop();
    };
    cn::spawn(*io, run());
    io->run();
    logger::shutdown();
    return success ? 0 : 1;
}
