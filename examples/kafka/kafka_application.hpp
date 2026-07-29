#pragma once

namespace kafka_example {

class application
{
public:
    application(cnetmod::io_context& context, configuration config)
        : context_(context), config_(std::move(config)), producer_(context_, config_),
          consumer_(context_, config_)
    {}

    auto start() -> cnetmod::task<void>
    {
        if (!(co_await producer_.start())) {
            context_.stop();
            co_return;
        }

        std::atomic_size_t processed{0};
        cnetmod::async_wait_group lifecycle;
        lifecycle.add(2);
        cnetmod::spawn(context_, [&]() -> cnetmod::task<void> {
            co_await consumer_.consume(processed);
            lifecycle.done();
        }());
        cnetmod::spawn(context_, [&]() -> cnetmod::task<void> {
            (void)co_await producer_.publish_many();
            lifecycle.done();
        }());
        co_await lifecycle.wait();

        producer_.close();
        logger::info("Kafka service stopped cleanly: processed={}", processed.load());
        context_.stop();
    }

private:
    cnetmod::io_context& context_;
    configuration config_;
    producer_service producer_;
    consumer_service consumer_;
};

} // namespace kafka_example
