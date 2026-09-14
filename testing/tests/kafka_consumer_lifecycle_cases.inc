namespace {
struct consumer_lifecycle_backend final : cnetmod::kafka::consumer_backend
{
    unsigned operations = 0;
    unsigned closes = 0;
    bool throw_on_close = false;
    bool fail_close = true;
    cnetmod::cancel_token* close_token = nullptr;
    std::coroutine_handle<> suspended_close{};
    bool suspend_first_close = false;

    struct close_gate
    {
        std::coroutine_handle<>& continuation;

        auto await_ready() const noexcept -> bool
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> value) noexcept
        {
            continuation = value;
        }

        void await_resume() const noexcept {}
    };

    auto subscribe(std::span<const std::string>, cnetmod::cancel_token*)
        -> cnetmod::task<cnetmod::kafka::result<void>> override
    {
        ++operations;
        co_return cnetmod::kafka::result<void>{};
    }

    auto assign(std::span<const cnetmod::kafka::topic_partition>, cnetmod::cancel_token*)
        -> cnetmod::task<cnetmod::kafka::result<void>> override
    {
        ++operations;
        co_return cnetmod::kafka::result<void>{};
    }

    auto poll(std::size_t, cnetmod::cancel_token*)
        -> cnetmod::task<cnetmod::kafka::result<std::vector<cnetmod::kafka::consumed_record>>> override
    {
        ++operations;
        co_return std::vector<cnetmod::kafka::consumed_record>{};
    }

    auto assignment() const -> std::vector<cnetmod::kafka::topic_partition> override
    {
        return {};
    }

    auto seek(const cnetmod::kafka::topic_partition&, std::int64_t, cnetmod::cancel_token*)
        -> cnetmod::task<cnetmod::kafka::result<void>> override
    {
        ++operations;
        co_return cnetmod::kafka::result<void>{};
    }

    auto commit(const std::map<cnetmod::kafka::topic_partition, cnetmod::kafka::offset_and_metadata>&,
        cnetmod::cancel_token*) -> cnetmod::task<cnetmod::kafka::result<void>> override
    {
        ++operations;
        co_return cnetmod::kafka::result<void>{};
    }

    auto close(cnetmod::cancel_token* token) -> cnetmod::task<cnetmod::kafka::result<void>> override
    {
        ++closes;
        close_token = token;
        if (suspend_first_close && closes == 1)
            co_await close_gate{suspended_close};
        if (throw_on_close)
            throw std::runtime_error("cleanup failure");
        if (fail_close)
            co_return std::unexpected(cnetmod::kafka::error{
                .code = cnetmod::kafka::error_code::request_timed_out,
                .message = "cleanup timeout",
                .retriable = true});
        co_return cnetmod::kafka::result<void>{};
    }
};
} // namespace

TEST(consumer_maintenance_completion_snapshot_preserves_failure)
{
    cnetmod::net_init network;
    for (const bool fails : {false, true})
    {
        auto io = cnetmod::make_io_context();
        cnetmod::task_group group{*io};
        const auto empty = group.completion_result();
        ASSERT_TRUE(empty && !*empty);
        ASSERT_TRUE(group.run([fails](cnetmod::cancel_token&) -> cnetmod::task<std::expected<void, std::error_code>>
            {
                if (fails)
                    co_return std::unexpected(std::make_error_code(std::errc::io_error));
                co_return std::expected<void, std::error_code>{};
            }));
        ASSERT_FALSE(group.completion_result().has_value());
        auto joining = [&]() -> cnetmod::task<void>
        {
            auto result = co_await group.join();
            ASSERT_EQ(result.has_value(), !fails);
            const auto completed = group.completion_result();
            ASSERT_TRUE(completed.has_value());
            if (completed)
                ASSERT_EQ(*completed, fails ? std::make_error_code(std::errc::io_error) : std::error_code{});
            io->stop();
        };
        auto running = joining();
        running.handle().resume();
        io->run();
        ASSERT_TRUE(running.handle().done());
        running.handle().promise().result();
    }
}

TEST(kafka_consumer_concurrent_close_performs_cleanup_once)
{
    auto backend = std::make_shared<consumer_lifecycle_backend>();
    backend->fail_close = false;
    backend->suspend_first_close = true;
    cnetmod::kafka::consumer consumer{backend, {}};
    auto first = consumer.close();
    first.handle().resume();
    ASSERT_FALSE(first.handle().done());
    auto second = consumer.close();
    second.handle().resume();
    ASSERT_FALSE(second.handle().done());
    ASSERT_EQ(backend->closes, 1u);
    if (backend->suspended_close)
        std::exchange(backend->suspended_close, {}).resume();
    ASSERT_TRUE(first.handle().done());
    ASSERT_TRUE(second.handle().done());
    ASSERT_TRUE(first.handle().promise().result().has_value());
    ASSERT_TRUE(second.handle().promise().result().has_value());
    ASSERT_EQ(backend->closes, 1u);
}

TEST(kafka_consumer_failed_close_seals_operations_and_allows_cleanup_retry)
{
    namespace kafka = cnetmod::kafka;
    for (const bool throws : {false, true})
    {
        auto backend = std::make_shared<consumer_lifecycle_backend>();
        backend->throw_on_close = throws;
        kafka::consumer consumer{backend, {}};
        cnetmod::cancel_token token;
        auto exercise = [&]() -> cnetmod::task<void>
        {
            auto deferred_poll = consumer.poll();
            auto closing = consumer.close(&token);
            ASSERT_TRUE((co_await consumer.assign({})).has_value());
            bool caught = false;
            try
            {
                auto result = co_await closing;
                ASSERT_FALSE(result.has_value());
                if (!result)
                {
                    ASSERT_TRUE(result.error().code == kafka::error_code::request_timed_out);
                    ASSERT_EQ(result.error().message, std::string{"cleanup timeout"});
                    ASSERT_TRUE(result.error().retriable);
                }
            }
            catch (const std::runtime_error& error)
            {
                caught = true;
                ASSERT_EQ(std::string{error.what()}, std::string{"cleanup failure"});
            }
            ASSERT_EQ(caught, throws);
            ASSERT_TRUE(backend->close_token == &token);
            ASSERT_FALSE((co_await deferred_poll).has_value());
            ASSERT_FALSE((co_await consumer.subscribe({})).has_value());
            ASSERT_FALSE((co_await consumer.assign({})).has_value());
            ASSERT_FALSE((co_await consumer.seek({"orders", 0}, 0)).has_value());
            kafka::consumed_record record;
            ASSERT_FALSE((co_await consumer.commit(record)).has_value());
            ASSERT_EQ(backend->operations, 1u);
            backend->throw_on_close = false;
            backend->fail_close = false;
            ASSERT_TRUE((co_await consumer.close(&token)).has_value());
            ASSERT_EQ(backend->closes, 2u);
            ASSERT_FALSE((co_await consumer.poll()).has_value());
            ASSERT_EQ(backend->operations, 1u);
        };
        auto running = exercise();
        running.handle().resume();
        ASSERT_TRUE(running.handle().done());
        running.handle().promise().result();
    }
}
