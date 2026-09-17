#include "test_framework.hpp"
#include <cnetmod/orm.hpp>

import std;
import cnetmod.application.mysql;
import cnetmod.application.service_registry;
import cnetmod.application.managed_service;
import cnetmod.application.recovery_policy;
import cnetmod.application.task_supervisor;
import cnetmod.observability;
import cnetmod.protocol.mysql;
import cnetmod.orm;
import cnetmod.core.net_init;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;

struct sharded_order_live
{
    std::int64_t id{};
    std::string description;
};

CNETMOD_MODEL(sharded_order_live, "orders",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(description, "description", varchar))

namespace {
std::uint16_t mysql_port = 3306;
} // namespace

TEST(mysql_live_named_pools_route_sharded_orm_transactions)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    cnetmod::application::task_supervisor supervisor{*io};
    cnetmod::application::service_registry registry;

    auto make_service = [&](std::string instance, const char* database)
    {
        cnetmod::mysql::pool_params options;
        options.host = "127.0.0.1";
        options.port = mysql_port;
        options.username = std::getenv("CNETMOD_MYSQL_SHARD_TEST_USER");
        options.password = std::getenv("CNETMOD_MYSQL_SHARD_TEST_PASSWORD");
        options.database = database;
        options.ssl = cnetmod::mysql::ssl_mode::disable;
        options.initial_size = options.max_size = 1;
        return std::make_shared<cnetmod::application::mysql_service>(*io,
            std::move(options), std::move(instance),
            cnetmod::application::service_requirement::required,
            cnetmod::application::recovery_policy{});
    };

    auto first = make_service("orders-0",
        std::getenv("CNETMOD_MYSQL_SHARD_TEST_DATABASE_0"));
    auto second = make_service("orders-1",
        std::getenv("CNETMOD_MYSQL_SHARD_TEST_DATABASE_1"));
    ASSERT_TRUE(registry.add_managed_named<cnetmod::application::mysql_service>(
                            "orders-0", first)
            .has_value());
    ASSERT_TRUE(registry.add_managed_named<cnetmod::application::mysql_service>(
                            "orders-1", second)
            .has_value());
    registry.freeze();

    auto catalog = std::make_shared<cnetmod::orm::shard_catalog>();
    ASSERT_TRUE(catalog->add_database("orders-0").has_value());
    ASSERT_TRUE(catalog->add_database("orders-1").has_value());
    ASSERT_TRUE(catalog->freeze("orders", 4,
                           std::make_shared<cnetmod::orm::hash_shard_strategy>())
            .has_value());
    auto gateway = cnetmod::application::make_mysql_sharded_session_gateway(
        registry, catalog);
    ASSERT_TRUE(gateway.has_value());

    auto exercise = [&]() -> cnetmod::task<void>
    {
        cnetmod::cancel_token cancellation;
        cnetmod::application::service_context context{*io, telemetry,
            supervisor, cancellation,
            cnetmod::deadline::after(std::chrono::seconds{10})};
        ASSERT_TRUE((co_await first->start(context)).has_value());
        ASSERT_TRUE((co_await second->start(context)).has_value());

        std::array<std::optional<std::uint64_t>, 2> keys;
        for (std::uint64_t key = 1; key < 1000 &&
            (!keys[0].has_value() || !keys[1].has_value());
            ++key)
        {
            auto route = catalog->route(cnetmod::orm::shard_key{key});
            ASSERT_TRUE(route.has_value());
            if (route)
                keys[route->database_shard] = key;
        }
        ASSERT_TRUE(keys[0].has_value());
        ASSERT_TRUE(keys[1].has_value());

        for (std::size_t shard = 0; shard < keys.size(); ++shard)
        {
            const auto id = static_cast<std::int64_t>(*keys[shard]);
            const auto description = std::format("database-shard-{}", shard);
            auto written = co_await gateway->write<int>(
                cnetmod::orm::shard_key{*keys[shard]},
                [id, description](auto& session)
                    -> cnetmod::task<std::expected<int, std::string>>
                {
                    sharded_order_live order{id, description};
                    auto result = co_await session.insert(order);
                    if (result.is_err())
                        co_return std::unexpected(result.error_msg);
                    co_return 1;
                });
            ASSERT_TRUE(written.has_value());

            auto read = co_await gateway->read<std::string>(
                cnetmod::orm::shard_key{*keys[shard]},
                [id](auto& session)
                    -> cnetmod::task<std::expected<std::string, std::string>>
                {
                    auto result = co_await session.template find_by_id<
                        sharded_order_live>(cnetmod::orm::param_value::from_int(id));
                    if (result.is_err())
                        co_return std::unexpected(result.error_msg);
                    auto order = result.first();
                    if (!order)
                        co_return std::unexpected("routed row is missing");
                    co_return order->description;
                });
            ASSERT_TRUE(read.has_value());
            if (read)
                ASSERT_EQ(*read, description);
        }

        const std::array transaction_keys{
            cnetmod::orm::shard_key{*keys[0]},
            cnetmod::orm::shard_key{*keys[1]},
        };
        auto distributed = co_await gateway->distributed_transaction<int>(
            transaction_keys,
            [&](auto& sessions)
                -> cnetmod::task<std::expected<int, std::string>>
            {
                for (auto& entry : sessions.entries())
                {
                    const auto shard = entry.route.database_shard;
                    sharded_order_live order{
                        static_cast<std::int64_t>(*keys[shard]),
                        std::format("xa-database-shard-{}", shard),
                    };
                    auto updated = co_await entry.session.update(order);
                    if (updated.is_err())
                        co_return std::unexpected(updated.error_msg);
                }
                co_return static_cast<int>(sessions.entries().size());
            });
        ASSERT_TRUE(distributed.has_value());
        if (distributed)
            ASSERT_EQ(*distributed, 2);

        auto total = co_await gateway->scatter_gather<std::size_t, std::size_t>(
            [](const cnetmod::orm::shard_route&, auto& session)
                -> cnetmod::task<std::expected<std::size_t, std::string>>
            {
                co_return co_await session.template count<sharded_order_live>(
                    cnetmod::orm::query_wrapper<sharded_order_live>{});
            },
            [](std::vector<cnetmod::orm::shard_read_result<std::size_t>> values)
                -> std::expected<std::size_t, std::string>
            {
                std::size_t result{};
                for (const auto& value : values)
                {
                    if (!value.result)
                        return std::unexpected(value.result.error());
                    result += *value.result;
                }
                return result;
            });
        ASSERT_TRUE(total.has_value());
        if (total)
            ASSERT_EQ(*total, 2U);

        supervisor.request_stop();
        ASSERT_TRUE((co_await supervisor.join()).has_value());
        ASSERT_TRUE((co_await second->stop(context)).has_value());
        ASSERT_TRUE((co_await first->stop(context)).has_value());
        io->stop();
    };
    auto operation = exercise();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(operation.handle().done());
    operation.handle().promise().result();
}

int main()
{
    const auto* enabled = std::getenv("CNETMOD_MYSQL_SHARD_INTEGRATION");
    if (!enabled || std::string_view{enabled} != "1")
        return 77;
    for (const auto* name : {"CNETMOD_MYSQL_SHARD_TEST_USER",
             "CNETMOD_MYSQL_SHARD_TEST_PASSWORD",
             "CNETMOD_MYSQL_SHARD_TEST_DATABASE_0",
             "CNETMOD_MYSQL_SHARD_TEST_DATABASE_1"})
        if (!std::getenv(name))
            return EXIT_FAILURE;
    return cnetmod::test::run_all();
}
