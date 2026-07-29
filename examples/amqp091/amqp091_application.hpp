#pragma once

namespace amqp091_example {

class application
{
public:
    application(cnetmod::io_context& context, configuration config)
        : context_(context), config_(std::move(config)), client_(context_),
          publisher_(context_, config_, client_), listeners_(context_, config_, client_)
    {}

    auto start() -> cnetmod::task<void>
    {
        if (auto connected = co_await client_.async_connect(config_.connection_options());
            !connected) {
            logger::error("AMQP connection failed: {}", connected.error().message);
            context_.stop();
            co_return;
        }
        client_.connection()->set_recovery_strategy(
            std::make_shared<cnetmod::amqp091::automatic_recovery_strategy>(
                std::make_shared<cnetmod::amqp091::exponential_backoff>(
                    std::chrono::seconds{1}, std::chrono::seconds{30}, 2.0), true));
        cnetmod::spawn(context_, [this]() -> cnetmod::task<void> {
            auto result = co_await client_.async_run(read_cancellation_);
            if (!result && !read_cancellation_.is_cancelled())
                logger::error("AMQP read loop stopped: {}", result.error().message);
        }());

        if (!(co_await declare_topology()) || !(co_await listeners_.start(processed_))) {
            co_await shutdown();
            co_return;
        }
        cnetmod::spawn(context_, [this]() -> cnetmod::task<void> {
            publish_succeeded_ = co_await publisher_.publish_many();
            publish_complete_ = true;
        }());
        co_await listeners_.wait_until_complete(processed_);
        while (!publish_complete_)
            co_await cnetmod::async_sleep(context_, std::chrono::milliseconds{5});
        if (!publish_succeeded_)
            logger::error("AMQP publisher completed with delivery failures");
        logger::info("AMQP service processed {} messages", processed_.load());
        co_await shutdown();
    }

private:
    auto declare_topology() -> cnetmod::task<bool>
    {
        auto opened = co_await client_.async_open_channel();
        if (!opened)
            co_return false;
        auto channel = *opened;
        cnetmod::amqp091::exchange_declare_options exchange;
        exchange.name = config_.exchange;
        exchange.type = cnetmod::amqp091::exchange_type::direct;
        exchange.durable = true;
        if (!(co_await channel->async_declare_exchange(std::move(exchange))))
            co_return false;
        cnetmod::amqp091::queue_declare_options queue;
        queue.name = config_.queue;
        queue.durable = true;
        if (!(co_await channel->async_declare_queue(std::move(queue))))
            co_return false;
        if (!(co_await channel->async_bind_queue({.queue = config_.queue,
                .exchange = config_.exchange, .routing_key = config_.routing_key})))
            co_return false;
        (void)co_await channel->async_close();
        co_return true;
    }

    auto shutdown() -> cnetmod::task<void>
    {
        co_await listeners_.stop();
        read_cancellation_.cancel();
        (void)co_await client_.async_close();
        context_.stop();
    }

    cnetmod::io_context& context_;
    configuration config_;
    cnetmod::amqp091::amqp091_client client_;
    publisher_service publisher_;
    listener_container listeners_;
    cnetmod::cancel_token read_cancellation_;
    std::atomic_size_t processed_{0};
    std::atomic_bool publish_complete_{false};
    std::atomic_bool publish_succeeded_{false};
};

} // namespace amqp091_example
