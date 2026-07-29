#pragma once

namespace amqp091_example {

class listener_container
{
public:
    listener_container(cnetmod::io_context& context, const configuration& config,
        cnetmod::amqp091::amqp091_client& client)
        : context_(context), config_(config), client_(client)
    {}

    auto start(std::atomic_size_t& processed) -> cnetmod::task<bool>
    {
        for (std::size_t worker = 0; worker < config_.consumer_concurrency; ++worker) {
            auto opened = co_await client_.async_open_channel();
            if (!opened) {
                logger::error("Listener {} channel open failed: {}", worker,
                    opened.error().message);
                co_return false;
            }
            auto channel = *opened;
            if (auto qos = co_await channel->async_set_qos(
                    {.prefetch_count = config_.prefetch});
                !qos) {
                logger::error("Listener {} QoS failed: {}", worker, qos.error().message);
                co_return false;
            }
            auto tag = "orders-listener-" + std::to_string(worker);
            auto result = co_await channel->async_consume(
                {.queue = config_.queue, .consumer_tag = tag},
                [this, channel, worker, &processed](const cnetmod::amqp091::delivery& delivery) {
                    cnetmod::spawn(context_, process_delivery(
                        channel, delivery, worker, processed));
                });
            if (!result) {
                logger::error("Listener {} consume failed: {}", worker, result.error().message);
                co_return false;
            }
            channels_.push_back(std::move(channel));
        }
        logger::info("AMQP listener container started: consumers={} prefetch={}",
            channels_.size(), config_.prefetch);
        co_return true;
    }

    auto wait_until_complete(const std::atomic_size_t& processed) -> cnetmod::task<void>
    {
        while (processed.load() < config_.message_count)
            co_await cnetmod::async_sleep(context_, std::chrono::milliseconds{10});
    }

    auto stop() -> cnetmod::task<void>
    {
        for (auto& channel : channels_)
            (void)co_await channel->async_close();
        channels_.clear();
    }

private:
    auto process_delivery(std::shared_ptr<cnetmod::amqp091::logical_channel> channel,
        cnetmod::amqp091::delivery delivery, std::size_t worker,
        std::atomic_size_t& processed) -> cnetmod::task<void>
    {
        // Spring @RabbitListener equivalent. ACK only after the domain operation.
        co_await cnetmod::async_sleep(context_, std::chrono::milliseconds{1});
        if (auto acked = co_await channel->async_ack(delivery.delivery_tag); !acked) {
            logger::error("Listener {} ACK failed for {}: {}", worker,
                delivery.message.message_id, acked.error().message);
            co_return;
        }
        const auto count = ++processed;
        if (count % 1000 == 0)
            logger::info("AMQP listener processed {} messages", count);
    }

    cnetmod::io_context& context_;
    const configuration& config_;
    cnetmod::amqp091::amqp091_client& client_;
    std::vector<std::shared_ptr<cnetmod::amqp091::logical_channel>> channels_;
};

} // namespace amqp091_example
