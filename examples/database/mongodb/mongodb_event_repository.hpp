#pragma once

namespace mongodb_example {

struct scenario_metrics
{
    std::int64_t created = 0;
    std::int64_t read = 0;
    std::int64_t updated = 0;
    std::int64_t deleted = 0;
    std::int64_t committed = 0;
    std::int64_t aborted = 0;
    std::int64_t events = 0;
    std::int64_t resumed = 0;
    std::int64_t successful_during_failover = 0;
    std::int64_t aborted_visible = 0;
    std::int64_t duplicates = 0;
    std::int64_t observed_primaries = 0;
};

class event_repository
{
public:
    event_repository(cnetmod::mongodb::topology_connection_pool& pool,
        std::string database) : pool_(pool), database_(std::move(database)) {}

    auto record_processed(std::size_t worker_id, std::size_t sequence_number,
        std::string_view run_id) -> cnetmod::task<bool>
    {
        namespace mongo = cnetmod::mongodb;
        mongo::bson_document selector{{"run_id", run_id},
            {"worker_id", static_cast<std::int64_t>(worker_id)}};
        mongo::bson_document update{{"$inc", mongo::bson_document{{"processed", std::int64_t{1}}}},
            {"$set", mongo::bson_document{{"last_sequence", static_cast<std::int64_t>(sequence_number)}, {"updated_at", mongo::bson_datetime{std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()}}}}};
        mongo::bson_document entry{{"q", std::move(selector)}, {"u", std::move(update)}, {"upsert", true}};
        mongo::bson_document command{{"update", "cnetmod_service_events"},
            {"updates", mongo::bson_array{std::move(entry)}}, {"ordered", false},
            {"writeConcern", mongo::bson_document{{"w", "majority"}}}};
        auto reply = co_await mongo::execute_retryable_command(pool_, database_,
            std::move(command), mongo::operation_kind::write);
        if (!reply)
            logger::error("Mongo repository write failed: worker={}, sequence={}, error={}",
                worker_id, sequence_number, reply.error().message);
        co_return reply.has_value();
    }

    auto record_scenario_result(std::string_view run_id, std::string_view scenario,
        bool passed, bool ready, bool completed, std::int64_t completed_count,
        const scenario_metrics& metrics,
        std::string error = {}, bool graceful_shutdown = false)
        -> cnetmod::task<bool>
    {
        namespace mongo = cnetmod::mongodb;
        mongo::bson_document metric_document{{"created", metrics.created},
            {"read", metrics.read}, {"updated", metrics.updated},
            {"deleted", metrics.deleted}, {"committed", metrics.committed},
            {"aborted", metrics.aborted}, {"events", metrics.events},
            {"resumed", metrics.resumed},
            {"successful_during_failover", metrics.successful_during_failover},
            {"aborted_visible", metrics.aborted_visible},
            {"duplicates", metrics.duplicates},
            {"observed_primaries", metrics.observed_primaries}};
        mongo::bson_document result_document{{"run_id", run_id}, {"scenario", scenario},
            {"status", passed ? "passed" : "failed"}, {"ready", ready},
            {"completed", completed}, {"completed_count", completed_count},
            {"metrics", std::move(metric_document)},
            {"error", std::move(error)}, {"graceful_shutdown", graceful_shutdown},
            {"completed_at", mongo::bson_datetime{std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()}}};
        mongo::bson_document command{{"update", "cnetmod_example_test_results"},
            {"updates", mongo::bson_array{mongo::bson_document{{"q", mongo::bson_document{{"run_id", run_id}, {"scenario", scenario}}}, {"u", mongo::bson_document{{"$set", std::move(result_document)}}}, {"upsert", true}}}},
            {"writeConcern", mongo::bson_document{{"w", "majority"}}}};
        auto reply = co_await mongo::execute_retryable_command(pool_, database_,
            std::move(command), mongo::operation_kind::write);
        co_return reply.has_value();
    }

private:
    cnetmod::mongodb::topology_connection_pool& pool_;
    std::string database_;
};

} // namespace mongodb_example
