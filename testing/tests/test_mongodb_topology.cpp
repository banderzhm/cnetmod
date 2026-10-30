#include "test_framework.hpp"
import std;
import cnetmod.protocol.mongodb;
using namespace cnetmod::mongodb;

namespace {
auto server(std::string host, server_kind kind, std::int64_t write = 1000000) -> server_description
{
    server_description description;
    description.address = {std::move(host), 27017};
    description.kind = kind;
    description.replica_set_name = "rs";
    description.last_update = std::chrono::steady_clock::time_point{std::chrono::seconds{10}};
    description.last_write_date = std::chrono::milliseconds{write};
    description.round_trip_time = std::chrono::milliseconds{1};
    return description;
}
} // namespace

TEST(mongodb_secondary_preferred_falls_back_after_filtering)
{
    topology_monitor monitor;
    monitor.update(server("primary", server_kind::replica_primary));
    monitor.update(server("secondary", server_kind::replica_secondary));
    server_selection_options options;
    options.preference = read_preference::secondary_preferred;
    options.tag_sets = {{{"dc", "missing"}}};
    auto selected = monitor.select_server(options);
    ASSERT_TRUE(selected.has_value());
    ASSERT_EQ(selected->address.host, std::string{"primary"});
    options.preference = read_preference::secondary;
    ASSERT_FALSE(monitor.select_server(options).has_value());
    options.preference = read_preference::primary_preferred;
    ASSERT_TRUE(monitor.select_server(options).has_value());
}

TEST(mongodb_tag_sets_are_ordered_fallbacks)
{
    topology_monitor monitor;
    auto first = server("preferred", server_kind::replica_secondary);
    first.tags = {{"dc", "a"}};
    auto second = server("fallback", server_kind::replica_secondary);
    second.tags = {{"dc", "b"}};
    monitor.update(first);
    monitor.update(second);
    server_selection_options options;
    options.preference = read_preference::secondary;
    options.tag_sets = {{{"dc", "a"}}, {{"dc", "b"}}};
    for (unsigned repeat{}; repeat < 4; ++repeat)
    {
        const auto selected = monitor.select_server(options);
        ASSERT_TRUE(selected.has_value());
        ASSERT_EQ(selected->address.host, std::string{"preferred"});
    }
}

TEST(mongodb_staleness_uses_last_write_not_recent_heartbeat)
{
    topology_monitor monitor;
    monitor.update(server("primary", server_kind::replica_primary));
    monitor.update(server("lagging", server_kind::replica_secondary, 800000));
    server_selection_options options;
    options.preference = read_preference::secondary;
    options.maximum_staleness = std::chrono::seconds{90};
    ASSERT_FALSE(monitor.select_server(options).has_value());
    monitor.update(server("caught-up", server_kind::replica_secondary, 990000));
    const auto selected = monitor.select_server(options);
    ASSERT_TRUE(selected.has_value());
    ASSERT_EQ(selected->address.host, std::string{"caught-up"});
    options.maximum_staleness = std::chrono::seconds{89};
    ASSERT_FALSE(monitor.select_server(options).has_value());
}

TEST(mongodb_staleness_without_primary_uses_freshest_secondary)
{
    topology_monitor monitor;
    monitor.update(server("fresh", server_kind::replica_secondary));
    monitor.update(server("old", server_kind::replica_secondary, 800000));
    server_selection_options options;
    options.preference = read_preference::secondary;
    options.maximum_staleness = std::chrono::seconds{90};
    const auto selected = monitor.select_server(options);
    ASSERT_TRUE(selected.has_value());
    ASSERT_EQ(selected->address.host, std::string{"fresh"});
}

TEST(mongodb_describe_server_reads_last_write_date)
{
    auto description = describe_server({"host", 27017}, bson_document{{"setName", "rs"}, {"secondary", true}, {"lastWrite", bson_document{{"lastWriteDate", bson_datetime{123456}}}}},
        std::chrono::milliseconds{2});
    ASSERT_TRUE(description.has_value());
    ASSERT_TRUE(description->last_write_date.has_value());
    ASSERT_EQ(description->last_write_date->count(), 123456);
}

TEST(mongodb_primary_rejects_secondary_filters_and_routers_ignore_local_tags)
{
    topology_monitor monitor;
    auto router = server("router", server_kind::mongos);
    monitor.update(router);
    server_selection_options options;
    options.tag_sets = {{{"dc", "other"}}};
    ASSERT_FALSE(monitor.select_server(options).has_value());
    options.preference = read_preference::secondary;
    ASSERT_TRUE(monitor.select_server(options).has_value());
}

TEST(mongodb_retry_does_not_resume_lost_history_or_replay_one_transaction_command)
{
    retryable_operation_policy policy;
    auto error = make_error(error_code::command_failed, "history lost");
    for (const auto code : {136, 280, 286})
    {
        error.server_code = code;
        ASSERT_FALSE(policy.should_retry(operation_kind::change_stream_get_more, error, 1));
    }
    error.server_code = 43;
    ASSERT_TRUE(policy.should_retry(operation_kind::change_stream_get_more, error, 1));
    error.server_code = 0;
    error.labels["TransientTransactionError"] = "";
    ASSERT_FALSE(policy.should_retry(operation_kind::commit_transaction, error, 1));
    error.labels.clear();
    error.labels["UnknownTransactionCommitResult"] = "";
    ASSERT_TRUE(policy.should_retry(operation_kind::commit_transaction, error, 1));
    ASSERT_FALSE(policy.should_retry(operation_kind::read, error, 1));
}
RUN_TESTS()
