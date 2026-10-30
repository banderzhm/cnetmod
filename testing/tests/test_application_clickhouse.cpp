#include "test_framework.hpp"
#include <cnetmod/orm.hpp>
import std;
import cnetmod.core.net_init;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.observability;
import cnetmod.application;
import cnetmod.application.clickhouse;
import cnetmod.application.clickhouse_orm;
import cnetmod.application.data;
import cnetmod.application.task_supervisor;
import cnetmod.application.managed_service;
import cnetmod.protocol.clickhouse;
import cnetmod.protocol.clickhouse.orm;
import cnetmod.orm;
import cnetmod.orm.semantic_schema_migration;
import cnetmod.json;
namespace cn = cnetmod;
namespace ch = cn::clickhouse;
namespace app = cn::application;
namespace orm = cn::orm;

struct analytics_record
{
    std::uint64_t id{};
    std::string label;
    std::optional<std::int64_t> score;
    std::string amount;
};
CNETMOD_MODEL(analytics_record, "cnetmod_orm_events",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(label, "label", varchar),
    CNETMOD_FIELD(score, "score", int_, NULLABLE),
    CNETMOD_FIELD(amount, "amount", decimal))

static auto options() -> ch::connection_pool_options
{
    ch::connection_pool_options result;
    result.connection.port = static_cast<std::uint16_t>(std::stoul(std::getenv("CNETMOD_CLICKHOUSE_TEST_PORT")));
    result.minimum_connections = 1;
    result.maximum_connections = 2;
    return result;
}

template <class T> static auto checked(std::expected<T, ch::error> result) -> T
{
    if (!result)
        throw std::runtime_error(result.error().message);
    if constexpr (!std::is_void_v<T>)
        return std::move(*result);
}

template <class T> static void check_model(const orm::model_result<T>& result, std::source_location location = std::source_location::current())
{
    if (result.is_err())
        throw std::runtime_error(std::to_string(location.line()) + ": " + result.error_msg);
}

TEST(clickhouse_pool_and_repository_real_roundtrip)
{
    cn::net_init net;
    auto io = cn::make_io_context();
    ch::client admin(*io);
    ch::connection_pool pool(*io, options());
    std::exception_ptr failure;
    auto run = [&]() -> cn::task<void>
    {
        try
        {
            checked(co_await admin.connect(options().connection));
            checked(co_await admin.query("DROP TABLE IF EXISTS cnetmod_orm_events"));
            orm::table_definition schema;
            schema.name = "cnetmod_schema_events";
            schema.columns.push_back({.name = "id", .type = orm::relational_column_type::int64, .nullable = false, .primary_key = true});
            schema.columns.push_back({.name = "label", .type = orm::relational_column_type::text});
            orm::schema_migration migration{.version = {1, 0, 0}, .name = "clickhouse_schema", .operations = {orm::create_table_operation{schema}}};
            auto statements = orm::render_schema_migration(migration, orm::relational_dialect::clickhouse);
            ASSERT_TRUE(statements.has_value());
            ASSERT_EQ(statements->front(), std::string("CREATE TABLE `cnetmod_schema_events` (`id` Int64,`label` Nullable(String)) ENGINE=MergeTree ORDER BY (`id`)"));
            checked(co_await admin.query("DROP TABLE IF EXISTS cnetmod_schema_events"));
            checked(co_await admin.query(statements->front()));
            checked(co_await admin.query("DROP TABLE cnetmod_schema_events"));
            schema.columns[0].auto_generated = true;
            migration.operations = {orm::create_table_operation{schema}};
            ASSERT_FALSE(orm::render_schema_migration(migration, orm::relational_dialect::clickhouse).has_value());
            checked(co_await admin.query("CREATE TABLE cnetmod_orm_events (id UInt64,label String,score Nullable(Int32),amount Decimal(18,4)) ENGINE=MergeTree ORDER BY id"));
            checked(co_await pool.warm_up());
            ASSERT_EQ(pool.idle_count(), std::size_t{1});
            {
                auto first = checked(co_await pool.acquire());
                auto second = checked(co_await pool.acquire());
                auto exhausted = co_await pool.acquire();
                ASSERT_FALSE(exhausted.has_value());
                ASSERT_TRUE(exhausted.error().kind == ch::error_kind::busy);
                ASSERT_FALSE(pool.close().has_value());
            }
            ASSERT_TRUE(pool.close().has_value());
            // Use a fresh pool after shutdown; closed pools never silently reopen.
            ch::connection_pool repository_pool(*io, options());
            auto gateway = ch::make_session_gateway(repository_pool);
            orm::repository<analytics_record, decltype(gateway)> repository(gateway);
            analytics_record record{1, "quote'\\ and 中文", std::nullopt, "123.4567"};
            check_model(co_await repository.save(record));
            auto found = co_await repository.get_by_id(orm::param_value::from_uint(1));
            check_model(found);
            ASSERT_EQ(found.data.size(), std::size_t{1});
            ASSERT_EQ(found.data[0].label, record.label);
            ASSERT_EQ(found.data[0].amount, record.amount);
            ASSERT_FALSE(found.data[0].score.has_value());
            std::array<analytics_record, 2> batch{{{2, "second", 7, "-2.0000"}, {3, "third", 8, "0.0001"}}};
            check_model(co_await repository.save_batch(std::span{batch}, 2));
            auto page = co_await repository.page(1, 2);
            check_model(page.records);
            ASSERT_EQ(page.total, std::size_t{3});
            ASSERT_EQ(page.records.data.size(), std::size_t{2});
            record.label = "changed";
            record.score = 42;
            check_model(co_await repository.update_by_id(record));
            auto updated = co_await repository.get_by_id(orm::param_value::from_uint(1));
            check_model(updated);
            ASSERT_EQ(updated.data[0].label, record.label);
            ASSERT_EQ(*updated.data[0].score, std::int64_t{42});
            check_model(co_await repository.remove_by_id(orm::param_value::from_uint(2)));
            auto count = co_await repository.count();
            check_model(count);
            ASSERT_EQ(count.data[0], std::int64_t{2});
            bool invoked{};
            auto transaction = co_await gateway.transaction<int>([&](auto&) -> cn::task<std::expected<int, std::string>>
                {
                    invoked = true;
                    co_return 1;
                });
            ASSERT_FALSE(transaction.has_value());
            ASSERT_FALSE(invoked);
            auto upsert = co_await repository.upsert(record);
            ASSERT_TRUE(upsert.is_err());
            ASSERT_EQ(upsert.framework_error, std::make_error_code(std::errc::operation_not_supported));
            // Binding must preserve placeholders in literals and comments.
            auto lease = checked(co_await repository_pool.acquire());
            ch::orm_client adapter(lease.get());
            auto bound = co_await adapter.execute(cn::database::parameterized_query{"SELECT '{}' AS literal, {} AS actual /* {} */", {orm::param_value::from_string("safe' value")}});
            ASSERT_FALSE(bound.is_err());
            ASSERT_EQ(bound.rows[0][0].get_string(), std::string("{}"));
            ASSERT_EQ(bound.rows[0][1].get_string(), std::string("safe' value"));
            auto typed = co_await adapter.query("SELECT toUUID('00112233-4455-6677-8899-aabbccddeeff'), toDate('2026-10-29'), toDateTime64('2026-10-29 12:34:56.123456',6,'UTC')");
            ASSERT_FALSE(typed.is_err());
            ASSERT_EQ(typed.rows[0][0].get_string(), std::string("00112233-4455-6677-8899-aabbccddeeff"));
            ASSERT_EQ(typed.rows[0][1].date_val.to_string(), std::string("2026-10-29"));
            ASSERT_EQ(typed.rows[0][2].datetime_val.to_string(), std::string("2026-10-29 12:34:56.123456"));
            ch::orm_client custom_adapter(lease.get(), [](const ch::value& value, std::string_view type) -> std::optional<cn::database::field_value>
                {
                    if (!type.starts_with("Array("))
                        return std::nullopt;
                    return cn::database::field_value::from_uint64(std::get<ch::value::sequence>(value.data).size());
                });
            auto custom = co_await custom_adapter.query("SELECT [toUInt8(1),toUInt8(2)]");
            ASSERT_FALSE(custom.is_err());
            ASSERT_EQ(custom.rows[0][0].get_uint64(), std::uint64_t{2});
            for (const auto malformed : {"UPDATE", "UPDATE ", "DELETE", "DELETE FROM"})
            {
                auto rejected = co_await adapter.execute(malformed);
                ASSERT_TRUE(rejected.is_err());
                ASSERT_TRUE(adapter.native().is_open());
            }
            auto mismatch = co_await adapter.execute(cn::database::parameterized_query{"SELECT {}", {}});
            ASSERT_TRUE(mismatch.is_err());
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        if (admin.is_open())
            (void)co_await admin.query("DROP TABLE IF EXISTS cnetmod_orm_events");
        admin.close();
        io->stop();
    };
    cn::spawn(*io, run());
    io->run();
    if (failure)
        std::rethrow_exception(failure);
}

TEST(clickhouse_application_start_probe_restart_and_repository_factory)
{
    cn::net_init net;
    auto io = cn::make_io_context();
    cn::observability::telemetry_hub telemetry{*io, {.export_traces = false, .export_metrics = false, .export_logs = false}};
    app::task_supervisor supervisor{*io};
    cn::cancel_token cancellation;
    auto service = std::make_shared<app::clickhouse_service>(*io, options(), "analytics", app::service_requirement::required, app::recovery_policy{});
    app::service_context context{*io, telemetry, supervisor, cancellation, cn::deadline::after(std::chrono::seconds(5))};
    std::exception_ptr failure;
    auto run = [&]() -> cn::task<void>
    {
        try
        {
            ASSERT_TRUE((co_await service->start(context)).has_value());
            ASSERT_TRUE((co_await service->probe(context)).status == app::service_health::up);
            app::service_registry registry;
            ASSERT_TRUE(registry.add_managed_named("analytics", service).has_value());
            auto factory = app::repository_factory<analytics_record>::create(registry, {}, app::repository_options{.instance = "analytics", .provider = app::database_provider::clickhouse});
            ASSERT_TRUE(factory.has_value());
            auto automatic = app::repository_factory<analytics_record>::create(registry, {}, app::repository_options{.instance = "analytics"});
            ASSERT_TRUE(automatic.has_value());
            auto lease = checked(co_await service->pool().acquire());
            auto failed_stop = co_await service->stop(context);
            ASSERT_FALSE(failed_stop.has_value());
            ASSERT_TRUE(service->cleanup_required());
            lease = ch::pooled_connection{};
            ASSERT_TRUE((co_await service->stop(context)).has_value());
            ASSERT_FALSE(service->cleanup_required());
            ASSERT_TRUE((co_await service->start(context)).has_value());
            ASSERT_TRUE((co_await service->probe(context)).status == app::service_health::up);
            ASSERT_TRUE((co_await service->stop(context)).has_value());
            cn::cancel_token cancelled;
            cancelled.cancel();
            app::service_context rejected{*io, telemetry, supervisor, cancelled, cn::deadline::after(std::chrono::seconds(1))};
            auto refusal = co_await service->start(rejected);
            ASSERT_FALSE(refusal.has_value());
            ASSERT_EQ(refusal.error(), std::make_error_code(std::errc::operation_canceled));
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        io->stop();
    };
    cn::spawn(*io, run());
    io->run();
    if (failure)
        std::rethrow_exception(failure);
}

TEST(clickhouse_auto_configuration_validates_before_network_start)
{
    auto build = [](bool enabled, bool invalid, unsigned threads)
    {
        return app::application_builder{"clickhouse-configuration"}.enable_auto_configuration().configure([=](app::application_configuration& configuration)
                                                                                                   {
                                                                                                       configuration.logging.manage_lifecycle = false;
                                                                                                       configuration.management.enabled = false;
                                                                                                       configuration.install_signal_handlers = false;
                                                                                                       configuration.observability.tracing = false;
                                                                                                       configuration.observability.metrics = false;
                                                                                                       configuration.observability.logs = false;
                                                                                                       configuration.execution.io_threads = threads;
                                                                                                       app::configured_service service{.name = "clickhouse", .instance = "analytics", .enabled = enabled};
                                                                                                       service.properties["host"] = "127.0.0.1";
                                                                                                       service.properties["maximum_size"] = 4;
                                                                                                       if (invalid)
                                                                                                           service.properties["compression"] = "invalid";
                                                                                                       configuration.services.emplace("analytics", std::move(service));
                                                                                                   })
            .add_module(app::make_module("analytics", {.register_components = [enabled](app::registration_context& context) -> std::expected<void, std::string>
                                                          {
                                                              if (enabled)
                                                                  app::add_repository<analytics_record>(context.components, {.instance = "analytics", .provider = app::database_provider::clickhouse});
                                                              return {};
                                                          }}))
            .build();
    };
    ASSERT_TRUE(build(true, false, 1).has_value());
    auto multi = build(true, false, 2);
    if (!multi)
        throw std::runtime_error("multi-loop build: " + multi.error().message);
    ASSERT_TRUE(multi.has_value());
    ASSERT_FALSE(build(true, true, 1).has_value());
    ASSERT_TRUE(build(false, true, 1).has_value());
}

int main()
{
    if (!std::getenv("CNETMOD_CLICKHOUSE_TEST_PORT"))
        return 77;
    return cn::test::run_all();
}
