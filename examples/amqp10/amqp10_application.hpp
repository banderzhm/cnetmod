#pragma once

namespace amqp10_example {

class application
{
public:
    application(cnetmod::io_context& context, configuration config)
        : context_(context), config_(std::move(config)), client_(context_),
          senders_(context_, config_, client_), receivers_(context_, config_, client_)
    {}

    auto start() -> cnetmod::task<void>
    {
        if (auto connected = co_await client_.connect(config_.client_options(), connection_token_);
            !connected) {
            logger::error("AMQP 1.0 connection failed: {}", connected.error().message);
            context_.stop();
            co_return;
        }
        client_.on_disconnect([](const cnetmod::amqp10::error& error) {
            logger::error("AMQP 1.0 disconnected at {}: {}",
                cnetmod::amqp10::to_string(error.stage), error.message);
        });
        receivers_.start(processed_);
        cnetmod::spawn(context_, [this]() -> cnetmod::task<void> {
            send_succeeded_ = co_await senders_.send_many();
            send_complete_ = true;
        }());

        while (processed_.load() < config_.message_count)
            co_await cnetmod::async_sleep(context_, std::chrono::milliseconds{10});
        while (!send_complete_)
            co_await cnetmod::async_sleep(context_, std::chrono::milliseconds{5});
        if (!send_succeeded_)
            logger::error("AMQP 1.0 senders completed with failures");

        co_await receivers_.stop();
        (void)co_await client_.close(connection_token_);
        logger::info("AMQP 1.0 service stopped cleanly: processed={}", processed_.load());
        context_.stop();
    }

private:
    cnetmod::io_context& context_;
    configuration config_;
    cnetmod::amqp10::client client_;
    sender_service senders_;
    receiver_container receivers_;
    cnetmod::cancel_token connection_token_;
    std::atomic_size_t processed_{0};
    std::atomic_bool send_complete_{false};
    std::atomic_bool send_succeeded_{false};
};

} // namespace amqp10_example
