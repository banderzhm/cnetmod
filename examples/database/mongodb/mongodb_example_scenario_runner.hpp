#pragma once

namespace mongodb_example {

class example_scenario_runner
{
public:
    example_scenario_runner(cnetmod::io_context& context, service_config config,
        std::string scenario)
        : context_(context), config_(std::move(config)), scenario_(std::move(scenario)), pool_(context_, config_.topology_pool_options()), repository_(pool_, config_.database), transactions_(context_, config_, pool_), changes_(context_, pool_, config_.database, "cnetmod_service_events", config_.change_stream_maximum_await), health_(context_, pool_, config_.database, config_.health_interval) {}

    auto run() -> cnetmod::task<void>
    {
        bool passed = false;
        bool ready = false;
        scenario_metrics metrics;
        std::string error;
        auto initial_health = co_await wait_until_ready();
        ready = initial_health.ready;
        if (!ready)
            error = initial_health.message;
        else if (scenario_ == "health")
            passed = true;
        else if (scenario_ == "repository")
            passed = co_await run_repository(metrics, error);
        else if (scenario_ == "transaction")
        {
            const bool committed = co_await transactions_.transfer_event(config_.test_run_id, 5);
            const bool abort_isolated = co_await transactions_.verify_abort_isolation(config_.test_run_id);
            metrics.committed = committed ? 1 : 0;
            metrics.aborted = abort_isolated ? 1 : 0;
            metrics.aborted_visible = abort_isolated ? 0 : 1;
            passed = committed && abort_isolated;
            if (!passed)
                error = "transaction commit or rollback isolation failed";
        }
        else if (scenario_ == "change-stream")
            passed = co_await run_change_stream(metrics, error);
        else if (scenario_ == "failover-watch")
            passed = co_await run_failover_watch(metrics, error);
        else
            error = "unknown scenario: " + scenario_;

        const bool recorded = co_await repository_.record_scenario_result(
            config_.test_run_id, scenario_, passed, ready, true,
            metrics.created, metrics, error);
        if (!recorded)
            logger::error("MongoDB scenario result could not be persisted: scenario={}", scenario_);
        pool_.close();
        context_.stop();
    }

private:
    auto wait_until_ready() -> cnetmod::task<health_report>
    {
        health_report snapshot;
        constexpr std::size_t maximum_attempts = 10;
        for (std::size_t attempt = 1; attempt <= maximum_attempts; ++attempt)
        {
            snapshot = co_await health_.check_once();
            if (snapshot.ready)
                co_return snapshot;
            logger::warn("MongoDB scenario readiness probe failed: " "scenario={}, attempt={}, error={}",
                scenario_, attempt, snapshot.message);
            co_await cnetmod::async_sleep(context_,
                std::chrono::milliseconds{100 * attempt});
        }
        co_return snapshot;
    }

    auto run_repository(scenario_metrics& metrics, std::string& error)
        -> cnetmod::task<bool>
    {
        const bool created = co_await repository_.record_processed(0, 1, config_.test_run_id);
        metrics.created = created ? 1 : 0;
        auto read = co_await cnetmod::mongodb::execute_retryable_command(pool_, config_.database,
            cnetmod::mongodb::bson_document{{"find", "cnetmod_service_events"},
                {"filter", cnetmod::mongodb::bson_document{{"run_id", config_.test_run_id}}},
                {"limit", std::int64_t{1}}},
            cnetmod::mongodb::operation_kind::read);
        metrics.read = read ? 1 : 0;
        auto updated = co_await cnetmod::mongodb::execute_retryable_command(pool_, config_.database,
            cnetmod::mongodb::bson_document{{"update", "cnetmod_service_events"},
                {"updates", cnetmod::mongodb::bson_array{cnetmod::mongodb::bson_document{{"q", cnetmod::mongodb::bson_document{{"run_id", config_.test_run_id}}}, {"u", cnetmod::mongodb::bson_document{{"$set", cnetmod::mongodb::bson_document{{"verified", true}}}}}}}}},
            cnetmod::mongodb::operation_kind::write);
        metrics.updated = updated ? 1 : 0;
        auto removed = co_await cnetmod::mongodb::execute_retryable_command(pool_, config_.database,
            cnetmod::mongodb::bson_document{{"delete", "cnetmod_service_events"},
                {"deletes", cnetmod::mongodb::bson_array{cnetmod::mongodb::bson_document{{"q", cnetmod::mongodb::bson_document{{"run_id", config_.test_run_id}}}, {"limit", std::int32_t{0}}}}}},
            cnetmod::mongodb::operation_kind::write);
        metrics.deleted = removed ? 1 : 0;
        const bool passed = created && read && updated && removed;
        if (!passed)
            error = "repository CRUD scenario failed";
        co_return passed;
    }

    auto run_change_stream(scenario_metrics& metrics, std::string& error)
        -> cnetmod::task<bool>
    {
        cnetmod::async_wait_group listener;
        listener.add(1);
        cnetmod::spawn(context_, run_change_listener(listener));
        co_await cnetmod::async_sleep(context_, std::chrono::milliseconds{500});
        for (std::size_t sequence = 0; sequence < 3; ++sequence)
            if (co_await repository_.record_processed(999, sequence, config_.test_run_id))
                ++metrics.created;
        const bool interrupted = co_await changes_.interrupt_cursor_for_verification();
        if (!interrupted)
            logger::warn("MongoDB change stream verification could not kill cursor");
        co_await cnetmod::async_sleep(context_, config_.change_stream_maximum_await * 2);
        if (co_await repository_.record_processed(999, 100, config_.test_run_id))
            ++metrics.created;
        co_await cnetmod::async_sleep(context_, config_.change_stream_maximum_await * 2);
        stop_.request_stop();
        (void)co_await changes_.interrupt_cursor_for_verification();
        co_await listener.wait();
        metrics.events = static_cast<std::int64_t>(changes_.event_count());
        metrics.resumed = static_cast<std::int64_t>(changes_.resume_count());
        if (metrics.events == 0 || metrics.resumed == 0)
            error = "change stream did not observe events and resume";
        co_return metrics.events > 0 && metrics.resumed > 0;
    }

    auto run_failover_watch(scenario_metrics& metrics, std::string& error)
        -> cnetmod::task<bool>
    {
        const auto duration = std::max(config_.scenario_duration,
            std::chrono::milliseconds{20000});
        const auto deadline = std::chrono::steady_clock::now() + duration;
        std::set<std::string> primaries;
        std::size_t sequence = 0;
        while (std::chrono::steady_clock::now() < deadline)
        {
            auto refreshed = co_await pool_.refresh();
            if (refreshed)
                if (auto selected = pool_.topology().select_server(); selected)
                    primaries.insert(std::format("{}:{}", selected->address.host, selected->address.port));
            if (co_await repository_.record_processed(777, sequence++, config_.test_run_id))
                ++metrics.successful_during_failover;
            co_await cnetmod::async_sleep(context_, std::chrono::milliseconds{100});
        }
        const bool passed = metrics.successful_during_failover > 0 && primaries.size() >= 2;
        metrics.observed_primaries = static_cast<std::int64_t>(primaries.size());
        if (!passed)
            error = std::format("failover evidence insufficient: writes={}, primaries={}",
                metrics.successful_during_failover, primaries.size());
        co_return passed;
    }

    auto run_change_listener(cnetmod::async_wait_group& listener)
        -> cnetmod::task<void>
    {
        co_await changes_.run(stop_.get_token());
        listener.done();
    }

    cnetmod::io_context& context_;
    service_config config_;
    std::string scenario_;
    cnetmod::mongodb::topology_connection_pool pool_;
    event_repository repository_;
    transaction_service transactions_;
    change_stream_listener changes_;
    health_indicator health_;
    std::stop_source stop_;
};

} // namespace mongodb_example
