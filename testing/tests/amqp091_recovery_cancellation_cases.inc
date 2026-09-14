namespace {

void verify_amqp091_recovery_delay_cancellation(bool precancelled, bool close_connection = false)
{
    namespace amqp = cnetmod::amqp091;
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto connection = std::make_shared<amqp::protocol_connection>(*io);
    connection->set_recovery_strategy(std::make_shared<amqp::automatic_recovery_strategy>(
        std::make_shared<amqp::exponential_backoff>(std::chrono::seconds{30}, std::chrono::seconds{30}), false));
    cnetmod::cancel_token cancellation;
    if (precancelled)
        cancellation.cancel();
    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto recovered = co_await connection->async_recover(cancellation);
        ASSERT_FALSE(recovered);
        if (!recovered)
            ASSERT_TRUE(recovered.error().code == amqp::error_code::cancelled);
        ASSERT_TRUE(connection->state() == amqp::connection_state::disconnected);
        io->stop();
    };
    auto operation = exercise();
    operation.handle().resume();
    if (precancelled)
        ASSERT_TRUE(operation.handle().done());
    else
    {
        ASSERT_FALSE(operation.handle().done());
        ASSERT_TRUE(connection->state() == amqp::connection_state::recovering);
        cnetmod::cancel_token competing_token;
        auto competing = connection->async_connect({}, competing_token);
        competing.handle().resume();
        ASSERT_TRUE(competing.handle().done());
        if (competing.handle().done())
        {
            auto result = competing.handle().promise().result();
            ASSERT_FALSE(result);
            if (!result)
                ASSERT_TRUE(result.error().code == amqp::error_code::command_invalid);
        }
        auto competing_recovery = connection->async_recover(competing_token);
        competing_recovery.handle().resume();
        ASSERT_TRUE(competing_recovery.handle().done());
        if (competing_recovery.handle().done())
        {
            auto result = competing_recovery.handle().promise().result();
            ASSERT_FALSE(result);
            if (!result)
                ASSERT_TRUE(result.error().code == amqp::error_code::command_invalid);
        }
        ASSERT_TRUE(connection->state() == amqp::connection_state::recovering);
    }
    const auto started = std::chrono::steady_clock::now();
    auto closing = connection->async_close();
    auto second_closing = connection->async_close();
    if (close_connection)
    {
        closing.handle().resume();
        second_closing.handle().resume();
    }
    else
        cancellation.cancel();
    if (!operation.handle().done())
        io->run();
    operation.handle().promise().result();
    if (close_connection)
    {
        ASSERT_TRUE(closing.handle().done());
        if (closing.handle().done())
            ASSERT_TRUE(closing.handle().promise().result());
        ASSERT_TRUE(second_closing.handle().done());
        if (second_closing.handle().done())
            ASSERT_TRUE(second_closing.handle().promise().result());
        ASSERT_TRUE(cancellation.is_cancelled());
    }
    ASSERT_TRUE(std::chrono::steady_clock::now() - started < std::chrono::seconds{2});
}

} // namespace

TEST(amqp091_recovery_backoff_is_cancellable)
{
    verify_amqp091_recovery_delay_cancellation(false);
}

TEST(amqp091_recovery_rejects_precancellation)
{
    verify_amqp091_recovery_delay_cancellation(true);
}

TEST(amqp091_close_cancels_and_waits_for_recovery_backoff)
{
    verify_amqp091_recovery_delay_cancellation(false, true);
}
