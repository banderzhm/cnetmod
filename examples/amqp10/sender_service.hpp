#pragma once

namespace amqp10_example {

class sender_service
{
public:
    sender_service(cnetmod::io_context& context, const configuration& config,
        cnetmod::amqp10::client& client)
        : context_(context), config_(config), client_(client)
    {}

    auto send_many() -> cnetmod::task<bool>
    {
        cnetmod::async_wait_group workers;
        workers.add(static_cast<int>(config_.sender_concurrency));
        for (std::size_t worker = 0; worker < config_.sender_concurrency; ++worker)
            cnetmod::spawn(context_, sender_worker(worker, workers));
        co_await workers.wait();
        logger::info("AMQP 1.0 sender completed: accepted={} failed={}",
            accepted_.load(), failed_.load());
        co_return failed_.load() == 0;
    }

private:
    auto sender_worker(std::size_t worker, cnetmod::async_wait_group& group)
        -> cnetmod::task<void>
    {
        namespace amqp = cnetmod::amqp10;
        cnetmod::cancel_token token;
        auto session_result = client_.make_session();
        if (!session_result) {
            ++failed_;
            group.done();
            co_return;
        }
        auto session = std::move(*session_result);
        if (!(co_await session.begin(token))) {
            ++failed_;
            group.done();
            co_return;
        }
        amqp::sender_options options;
        options.name = "orders-sender-" + std::to_string(worker);
        options.target_terminus.address = config_.address;
        auto link_result = session.make_sender(std::move(options));
        if (!link_result) {
            ++failed_;
            group.done();
            co_return;
        }
        auto link = std::move(*link_result);
        if (!(co_await link.attach(token))) {
            ++failed_;
            group.done();
            co_return;
        }

        while (true) {
            const auto sequence = next_.fetch_add(1);
            if (sequence >= config_.message_count)
                break;
            amqp::message message;
            message.properties.emplace();
            message.properties->to = config_.address;
            message.properties->content_type = "application/json";
            message.properties->message_id = amqp::value{
                std::string("order-") + std::to_string(sequence)};
            message.application.emplace("eventType", amqp::value{std::string("OrderCreated")});
            message.body = amqp::value{std::string("{\"orderId\":") +
                std::to_string(sequence) + "}"};
            amqp::send_options send_options;
            send_options.settled = false;
            auto sent = co_await link.send(message, std::move(send_options), token);
            if (!sent || sent->outcome.kind != amqp::outcome_kind::accepted) {
                ++failed_;
                logger::error("Sender {} delivery {} was not accepted", worker, sequence);
            } else {
                ++accepted_;
            }
        }
        (void)co_await link.detach(true, token);
        (void)co_await session.end(token);
        group.done();
    }

    cnetmod::io_context& context_;
    const configuration& config_;
    cnetmod::amqp10::client& client_;
    std::atomic_size_t next_{0}, accepted_{0}, failed_{0};
};

} // namespace amqp10_example
