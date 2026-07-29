#pragma once

namespace kafka_example {

class consumer_service
{
public:
    consumer_service(cnetmod::io_context& context, const configuration& config)
        : context_(context), config_(config)
    {}

    auto consume(std::atomic_size_t& processed) -> cnetmod::task<void>
    {
        cnetmod::async_wait_group workers;
        workers.add(static_cast<int>(config_.consumer_concurrency));
        for (std::size_t worker = 0; worker < config_.consumer_concurrency; ++worker)
            cnetmod::spawn(context_, consumer_worker(worker, processed, workers));
        co_await workers.wait();
    }

private:
    auto consumer_worker(std::size_t worker, std::atomic_size_t& processed,
        cnetmod::async_wait_group& group) -> cnetmod::task<void>
    {
        namespace kafka = cnetmod::kafka;
        kafka::client_facade client(context_,
            config_.client_options("orders-consumer-" + std::to_string(worker)));
        if (auto connected = co_await client.connect(); !connected) {
            logger::error("Consumer {} connect failed: {}", worker, connected.error().message);
            group.done();
            co_return;
        }
        kafka::consumer_options options;
        options.group_id = config_.group_id;
        options.enable_auto_commit = false;
        options.max_poll_records = 250;
        options.assignment_policy = kafka::consumer_assignment_policy::cooperative_sticky;
        options.auto_offset_reset = kafka::offset_reset_policy::earliest;
        auto created = client.make_consumer(std::move(options));
        if (!created) {
            logger::error("Consumer {} creation failed: {}", worker, created.error().message);
            client.close();
            group.done();
            co_return;
        }
        auto consumer = std::move(*created);
        if (auto subscribed = co_await consumer.subscribe({config_.topic}); !subscribed) {
            logger::error("Consumer {} subscribe failed: {}", worker, subscribed.error().message);
            client.close();
            group.done();
            co_return;
        }

        while (processed.load() < config_.message_count) {
            auto batch = co_await consumer.poll();
            if (!batch) {
                logger::error("Consumer {} poll failed: {}", worker, batch.error().message);
                continue;
            }
            for (const auto& record : *batch) {
                if (processed.load() >= config_.message_count)
                    break;
                if (!(co_await process_order(record, worker)))
                    continue; // no commit: Kafka will redeliver after recovery/rebalance
                if (auto committed = co_await consumer.commit(record); !committed) {
                    logger::error("Consumer {} commit failed at {}[{}]/{}: {}", worker,
                        record.source.topic, record.source.partition, record.offset,
                        committed.error().message);
                    continue;
                }
                ++processed;
            }
        }
        (void)co_await consumer.close();
        client.close();
        group.done();
    }

    auto process_order(const cnetmod::kafka::consumed_record& record,
        std::size_t worker) -> cnetmod::task<bool>
    {
        // This is the Spring @KafkaListener equivalent. Replace it with the
        // domain service/database transaction. Commit happens only after true.
        co_await cnetmod::async_sleep(context_, std::chrono::milliseconds{1});
        if (record.offset % 1000 == 0)
            logger::info("Consumer {} processed {}[{}]/{}", worker,
                record.source.topic, record.source.partition, record.offset);
        co_return true;
    }

    cnetmod::io_context& context_;
    const configuration& config_;
};

} // namespace kafka_example
