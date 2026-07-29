#pragma once

namespace kafka_example {

class producer_service
{
public:
    producer_service(cnetmod::io_context& context, const configuration& config)
        : context_(context), config_(config), client_(context, config.client_options("orders-producer"))
    {}

    auto start() -> cnetmod::task<bool>
    {
        namespace kafka = cnetmod::kafka;
        if (auto connected = co_await client_.connect(); !connected) {
            logger::error("Kafka producer connect failed: {}", connected.error().message);
            co_return false;
        }
        kafka::producer_options options;
        options.acks = kafka::acknowledgement::all;
        options.compression_type = kafka::compression::gzip;
        options.idempotent = true;
        options.max_in_flight = 5;
        options.batch_bytes = 1024 * 1024;
        options.linger = std::chrono::milliseconds{10};
        auto created = client_.make_producer(std::move(options));
        if (!created) {
            logger::error("Create Kafka producer failed: {}", created.error().message);
            co_return false;
        }
        producer_.emplace(std::move(*created));
        co_return true;
    }

    auto publish_many() -> cnetmod::task<bool>
    {
        cnetmod::async_wait_group workers;
        workers.add(static_cast<int>(config_.producer_concurrency));
        for (std::size_t worker = 0; worker < config_.producer_concurrency; ++worker)
            cnetmod::spawn(context_, publish_worker(worker, workers));
        co_await workers.wait();

        if (failed_.load() != 0) {
            logger::error("Kafka producer completed with {} failed records", failed_.load());
            co_return false;
        }
        if (auto flushed = co_await producer_->flush(); !flushed) {
            logger::error("Kafka flush failed: {}", flushed.error().message);
            co_return false;
        }
        logger::info("Kafka producer delivered {} records", delivered_.load());
        co_return true;
    }

    void close()
    {
        if (producer_)
            producer_->close();
        client_.close();
    }

private:
    auto publish_worker(std::size_t worker, cnetmod::async_wait_group& group)
        -> cnetmod::task<void>
    {
        while (true) {
            const auto sequence = next_.fetch_add(1);
            if (sequence >= config_.message_count)
                break;
            cnetmod::kafka::record record;
            record.key = bytes("order-" + std::to_string(sequence));
            record.value = bytes("{\"orderId\":" + std::to_string(sequence) +
                ",\"source\":\"cnetmod\"}");
            auto result = co_await producer_->send(config_.topic, std::move(record));
            if (!result) {
                ++failed_;
                logger::error("Producer worker {} failed record {}: {}", worker,
                    sequence, result.error().message);
                continue;
            }
            ++delivered_;
        }
        group.done();
    }

    cnetmod::io_context& context_;
    const configuration& config_;
    cnetmod::kafka::client_facade client_;
    std::optional<cnetmod::kafka::producer> producer_;
    std::atomic_size_t next_{0};
    std::atomic_size_t delivered_{0};
    std::atomic_size_t failed_{0};
};

} // namespace kafka_example
