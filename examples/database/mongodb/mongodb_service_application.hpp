#pragma once

namespace mongodb_example {

class service_application
{
public:
    service_application(cnetmod::io_context& context, service_config config)
        : context_(context), config_(std::move(config)), pool_(context_, config_.topology_pool_options()), repository_(pool_, config_.database), jobs_(config_.queue_capacity), transactions_(context_, config_, pool_), changes_(context_, pool_, config_.database, "cnetmod_service_events", config_.change_stream_maximum_await), health_(context_, pool_, config_.database, config_.health_interval) {}

    auto start() -> cnetmod::task<void>
    {
        shutdown_.install();
        auto refreshed = co_await pool_.refresh();
        if (!refreshed)
            throw std::runtime_error(refreshed.error().message);

        cnetmod::async_wait_group background;
        background.add(3);
        spawn_background(background, "topology monitor", pool_.run_monitoring(stop_.get_token(), config_.heartbeat_interval));
        spawn_background(background, "health indicator", health_.run(stop_.get_token()));
        spawn_background(background, "change stream", changes_.run(stop_.get_token()));

        cnetmod::async_wait_group workers;
        workers.add(static_cast<int>(config_.worker_concurrency));
        workers_.reserve(config_.worker_concurrency);
        for (std::size_t id = 0; id < config_.worker_concurrency; ++id)
        {
            workers_.push_back(std::make_unique<worker_service>(repository_, jobs_, id,
                config_.test_run_id, completed_));
            cnetmod::spawn(context_, run_worker(id, workers));
        }
        for (std::size_t sequence = 0; sequence < config_.request_count; ++sequence)
        {
            if (shutdown_.is_requested())
                break;
            if (!(co_await jobs_.send(event_job{sequence})))
                break;
        }
        jobs_.close();
        co_await workers.wait();

        const bool committed = co_await transactions_.transfer_event(config_.test_run_id, 1);
        stop_.request_stop();
        (void)co_await changes_.interrupt_cursor_for_verification();
        co_await background.wait();

        scenario_metrics metrics;
        metrics.created = static_cast<std::int64_t>(completed_.load());
        metrics.committed = committed ? 1 : 0;
        metrics.aborted = committed ? 0 : 1;
        metrics.events = static_cast<std::int64_t>(changes_.event_count());
        metrics.resumed = static_cast<std::int64_t>(changes_.resume_count());
        const bool all_processed = completed_.load() == config_.request_count;
        auto recorded = co_await repository_.record_scenario_result(config_.test_run_id,
            "serve", all_processed && committed, true, true,
            static_cast<std::int64_t>(completed_.load()), metrics,
            all_processed ? std::string{} : "worker drain incomplete", true);
        if (!recorded)
            logger::error("MongoDB serve result could not be persisted");
        pool_.close();
        logger::info("MongoDB production service drained: workers={}, completed={}, transaction_committed={}, change_events={}, resumes={}",
            config_.worker_concurrency, completed_.load(), committed,
            changes_.event_count(), changes_.resume_count());
        context_.stop();
    }

private:
    void spawn_background(cnetmod::async_wait_group& group, std::string name,
        cnetmod::task<void> operation)
    {
        cnetmod::spawn(context_, run_background(std::move(name), std::move(operation), group));
    }

    auto run_worker(std::size_t id, cnetmod::async_wait_group& workers)
        -> cnetmod::task<void>
    {
        try
        {
            co_await workers_[id]->run();
        }
        catch (const std::exception& error)
        {
            logger::error("MongoDB worker crashed: worker={}, error={}",
                id, error.what());
        }
        workers.done();
    }

    static auto run_background(std::string name, cnetmod::task<void> operation,
        cnetmod::async_wait_group& group) -> cnetmod::task<void>
    {
        try
        {
            co_await std::move(operation);
        }
        catch (const std::exception& error)
        {
            logger::error("MongoDB background service failed: service={}, error={}",
                name, error.what());
        }
        group.done();
    }

    cnetmod::io_context& context_;
    service_config config_;
    cnetmod::mongodb::topology_connection_pool pool_;
    event_repository repository_;
    cnetmod::channel<event_job> jobs_;
    transaction_service transactions_;
    change_stream_listener changes_;
    health_indicator health_;
    std::stop_source stop_;
    shutdown_signal shutdown_;
    std::atomic_size_t completed_{0};
    std::vector<std::unique_ptr<worker_service>> workers_;
};

} // namespace mongodb_example
