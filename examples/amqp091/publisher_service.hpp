#pragma once

namespace amqp091_example {

class confirmation_counter final : public cnetmod::amqp091::publisher_confirm_observer
{
public:
    void on_confirm(const cnetmod::amqp091::publisher_confirmation& value) override
    {
        if (value.acknowledged)
            ++acknowledged;
        else
            ++rejected;
    }
    void on_confirm_failure(const cnetmod::amqp091::error& error) override
    {
        logger::error("Publisher confirmation stream failed: {}", error.message);
        ++rejected;
    }
    std::atomic_size_t acknowledged{0};
    std::atomic_size_t rejected{0};
};

class publisher_service
{
public:
    publisher_service(cnetmod::io_context& context, const configuration& config,
        cnetmod::amqp091::amqp091_client& client)
        : context_(context), config_(config), client_(client),
          confirmations_(std::make_shared<confirmation_counter>())
    {}

    auto publish_many() -> cnetmod::task<bool>
    {
        cnetmod::async_wait_group workers;
        workers.add(static_cast<int>(config_.publisher_concurrency));
        for (std::size_t worker = 0; worker < config_.publisher_concurrency; ++worker)
            cnetmod::spawn(context_, publish_worker(worker, workers));
        co_await workers.wait();

        while (confirmations_->acknowledged.load() + confirmations_->rejected.load()
               < submitted_.load())
            co_await cnetmod::async_sleep(context_, std::chrono::milliseconds{5});
        logger::info("AMQP publisher completed: submitted={} confirmed={} rejected={}",
            submitted_.load(), confirmations_->acknowledged.load(),
            confirmations_->rejected.load());
        co_return failures_.load() == 0 && confirmations_->rejected.load() == 0;
    }

private:
    auto publish_worker(std::size_t worker, cnetmod::async_wait_group& group)
        -> cnetmod::task<void>
    {
        auto opened = co_await client_.async_open_channel();
        if (!opened) {
            logger::error("Publisher {} channel open failed: {}", worker,
                opened.error().message);
            ++failures_;
            group.done();
            co_return;
        }
        auto channel = *opened;
        channel->observe_confirms(confirmations_);
        if (auto enabled = co_await channel->async_enable_confirms(); !enabled) {
            logger::error("Publisher {} confirms failed: {}", worker, enabled.error().message);
            ++failures_;
            group.done();
            co_return;
        }
        while (true) {
            const auto sequence = next_.fetch_add(1);
            if (sequence >= config_.message_count)
                break;
            cnetmod::amqp091::message message;
            message.body = body("{\"orderId\":" + std::to_string(sequence) + "}");
            message.content_type = "application/json";
            message.message_id = "order-" + std::to_string(sequence);
            message.durable = true;
            auto result = co_await channel->async_publish(
                {.exchange = config_.exchange, .routing_key = config_.routing_key},
                std::move(message));
            if (!result) {
                ++failures_;
                logger::error("Publisher {} failed order {}: {}", worker, sequence,
                    result.error().message);
            } else {
                ++submitted_;
            }
        }
        (void)co_await channel->async_close();
        group.done();
    }

    cnetmod::io_context& context_;
    const configuration& config_;
    cnetmod::amqp091::amqp091_client& client_;
    std::shared_ptr<confirmation_counter> confirmations_;
    std::atomic_size_t next_{0}, submitted_{0}, failures_{0};
};

} // namespace amqp091_example
