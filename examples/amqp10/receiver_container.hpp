#pragma once

namespace amqp10_example {

class receiver_container
{
public:
    receiver_container(cnetmod::io_context& context, const configuration& config,
        cnetmod::amqp10::client& client)
        : context_(context), config_(config), client_(client)
    {
        tokens_.reserve(config_.receiver_concurrency);
        for (std::size_t i = 0; i < config_.receiver_concurrency; ++i)
            tokens_.push_back(std::make_unique<cnetmod::cancel_token>());
    }

    void start(std::atomic_size_t& processed)
    {
        workers_.add(static_cast<int>(config_.receiver_concurrency));
        for (std::size_t worker = 0; worker < config_.receiver_concurrency; ++worker)
            cnetmod::spawn(context_, receiver_worker(worker, processed));
    }

    auto stop() -> cnetmod::task<void>
    {
        for (auto& token : tokens_)
            token->cancel();
        co_await workers_.wait();
    }

private:
    auto receiver_worker(std::size_t worker, std::atomic_size_t& processed)
        -> cnetmod::task<void>
    {
        namespace amqp = cnetmod::amqp10;
        auto& token = *tokens_[worker];
        auto session_result = client_.make_session();
        if (!session_result) {
            workers_.done();
            co_return;
        }
        auto session = std::move(*session_result);
        if (!(co_await session.begin(token))) {
            workers_.done();
            co_return;
        }
        amqp::receiver_options options;
        options.name = "orders-receiver-" + std::to_string(worker);
        options.source_terminus.address = config_.address;
        auto link_result = session.make_receiver(std::move(options));
        if (!link_result) {
            workers_.done();
            co_return;
        }
        auto link = std::move(*link_result);
        if (!(co_await link.attach(config_.receiver_credit, token))) {
            workers_.done();
            co_return;
        }

        while (!token.is_cancelled()) {
            auto delivery = co_await link.receive(token);
            if (!delivery) {
                if (!token.is_cancelled())
                    logger::error("Receiver {} failed: {}", worker, delivery.error().message);
                break;
            }
            // Spring @JmsListener equivalent: settle only after domain work.
            co_await cnetmod::async_sleep(context_, std::chrono::milliseconds{1});
            amqp::delivery_outcome accepted;
            accepted.kind = amqp::outcome_kind::accepted;
            if (auto settled = co_await link.settle(
                    delivery->delivery_id, std::move(accepted), token);
                !settled) {
                logger::error("Receiver {} settlement failed: {}", worker,
                    settled.error().message);
                continue;
            }
            const auto count = ++processed;
            if (count % 1000 == 0)
                logger::info("AMQP 1.0 receivers processed {} messages", count);
            if (link.credit() < config_.receiver_credit / 2)
                (void)co_await link.add_credit(config_.receiver_credit, false, token);
        }
        if (!token.is_cancelled()) {
            (void)co_await link.detach(true, token);
            (void)co_await session.end(token);
        }
        workers_.done();
    }

    cnetmod::io_context& context_;
    const configuration& config_;
    cnetmod::amqp10::client& client_;
    std::vector<std::unique_ptr<cnetmod::cancel_token>> tokens_;
    cnetmod::async_wait_group workers_;
};

} // namespace amqp10_example
