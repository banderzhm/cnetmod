static auto verify_mongodb_close_preparation_failure(std::size_t allocation,
    std::optional<std::size_t> queue_failure = {}, bool synchronous = false) -> bool
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    cnetmod::mongodb::connection_pool_options options;
    options.connection.port = listener->local_endpoint()->port();
    options.connection.connect_timeout = std::chrono::milliseconds{30};
    options.connection.command_timeout = std::chrono::milliseconds{30};
    options.maximum_size = options.maximum_connecting = 1;
    options.wait_queue_timeout = std::chrono::milliseconds{100};
    cnetmod::mongodb::connection_pool pool{*io, options};
    auto connecting = pool.acquire();
    connecting.handle().resume();
    if (queue_failure)
    {
        auto failing = pool.acquire(std::stop_token{});
        fail_after = *queue_failure;
        failing.handle().resume();
        fail_after.reset();
        if (!failing.handle().done())
            std::terminate();
        bool failed = false;
        try
        {
            (void)failing.handle().promise().result();
        }
        catch (const std::bad_alloc&)
        {
            failed = true;
        }
        ASSERT_TRUE(failed);
        ASSERT_EQ(pool.waiter_count(), 0U);
    }
    auto queued = pool.acquire();
    queued.handle().resume();
    ASSERT_EQ(pool.waiter_count(), 1U);
    cnetmod::task<void> closing;
    if (!synchronous)
        closing = pool.async_close();
    fail_after = allocation;
    if (synchronous)
        pool.close();
    else
        closing.handle().resume();
    if (synchronous)
        ASSERT_TRUE(fail_after == allocation);
    fail_after.reset();
    if (synchronous)
    {
        closing = pool.async_close();
        closing.handle().resume();
    }
    ASSERT_TRUE(closing.handle().done());
    bool failed = false;
    try
    {
        closing.handle().promise().result();
    }
    catch (const std::bad_alloc&)
    {
        failed = true;
    }
    if (synchronous)
        ASSERT_FALSE(failed);
    ASSERT_EQ(pool.waiter_count(), 0U);
    auto retry = pool.async_close();
    retry.handle().resume();
    ASSERT_TRUE(retry.handle().done());
    retry.handle().promise().result();
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while ((!connecting.handle().done() || !queued.handle().done()) &&
        std::chrono::steady_clock::now() < limit)
        (void)io->poll();
    const bool settled = connecting.handle().done() && queued.handle().done();
    if (!settled)
        logger::error{"MongoDB close fault {}: connecting_done={}, queued_done={}, waiters={}",
            allocation, connecting.handle().done(), queued.handle().done(), pool.waiter_count()};
    ASSERT_TRUE(settled);
    if (!settled)
        std::terminate();
    ASSERT_FALSE(connecting.handle().promise().result().has_value());
    auto result = queued.handle().promise().result();
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(result.error().code == cnetmod::mongodb::error_code::connection_closed);
    ASSERT_EQ(pool.waiter_count(), 0U);
    return !failed;
}

TEST(mongodb_close_needs_no_staging_allocation_for_queued_borrowers)
{
    std::size_t completed_without_allocation_failure = 0;
    for (std::size_t allocation = 0; allocation < 12; ++allocation)
        completed_without_allocation_failure += verify_mongodb_close_preparation_failure(allocation);
    ASSERT_TRUE(completed_without_allocation_failure > 0);
    ASSERT_TRUE(verify_mongodb_close_preparation_failure(0, {}, true));
}

TEST(mongodb_waiter_task_construction_failure_rolls_back_queue)
{
    for (std::size_t failure = 0; failure < 2; ++failure)
        (void)verify_mongodb_close_preparation_failure(0, failure);
}
