namespace {
thread_local cnetmod::redis::pooled_connection* redis_return_lease{};
thread_local cnetmod::redis::connection_pool* redis_return_pool{};
thread_local bool redis_return_queued{};
thread_local bool redis_return_allocation_free{};
} // namespace

static void verify_redis_contended_return(bool stop_before_dispatch)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    cnetmod::redis::connection_pool pool{*io,
        {.host = "127.0.0.1", .port = listener->local_endpoint()->port(), .resp3 = false, .initial_size = 1, .max_size = 3, .ping_interval = std::chrono::hours{1}}};
    auto run = pool.async_run();
    run.handle().resume();
    cnetmod::cancel_token first_token;
    auto first = pool.async_get_connection(first_token);
    first.handle().resume();
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (!first.handle().done() && std::chrono::steady_clock::now() < limit)
        (void)io->poll();
    ASSERT_TRUE(first.handle().done());
    if (!first.handle().done())
        std::terminate();
    auto lease = first.handle().promise().result();
    ASSERT_TRUE(lease.has_value());
    auto* original_client = &lease->get();
    cnetmod::cancel_token second_token, third_token;
    auto second = pool.async_get_connection(second_token);
    second.handle().resume();
    ASSERT_EQ(pool.waiter_count(), 1U);
    auto third = pool.async_get_connection(third_token);
    redis_return_lease = &*lease;
    redis_return_pool = &pool;
    redis_return_queued = false;
    redis_return_allocation_free = false;
    before_allocation = []() noexcept
    {
        // Growing the third node holds the pool lock with an existing waiter.
        const auto before = redis_return_pool->pending_maintenance();
        fail_after = 0;
        *redis_return_lease = cnetmod::redis::pooled_connection{};
        redis_return_allocation_free = fail_after == 0;
        fail_after.reset();
        redis_return_queued = redis_return_pool->pending_maintenance() == before + 1;
    };
    third.handle().resume();
    before_allocation = nullptr;
    redis_return_lease = nullptr;
    redis_return_pool = nullptr;
    ASSERT_TRUE(redis_return_queued);
    ASSERT_TRUE(redis_return_allocation_free);
    ASSERT_FALSE(lease->valid());
    third_token.cancel();
    if (!stop_before_dispatch)
    {
        const auto handoff_limit = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (!second.handle().done() && std::chrono::steady_clock::now() < handoff_limit)
            (void)io->poll();
        ASSERT_TRUE(second.handle().done());
        if (!second.handle().done())
            std::terminate();
        auto handed_off = second.handle().promise().result();
        ASSERT_TRUE(handed_off.has_value());
        ASSERT_TRUE(&handed_off->get() == original_client);
        ASSERT_TRUE(handed_off->get().is_open());
    }
    // The other variant stops before dispatching the registered notification.
    second_token.cancel();
    pool.request_stop();
    auto stop = pool.cancel();
    stop.handle().resume();
    if (stop_before_dispatch)
        ASSERT_FALSE(stop.handle().done());
    const auto stop_limit = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while ((!stop.handle().done() || !run.handle().done() ||
               !second.handle().done() || !third.handle().done()) &&
        std::chrono::steady_clock::now() < stop_limit)
        (void)io->poll();
    const bool settled = stop.handle().done() && run.handle().done() &&
        second.handle().done() && third.handle().done();
    ASSERT_TRUE(settled);
    if (!settled)
        std::terminate();
    stop.handle().promise().result();
    run.handle().promise().result();
    if (stop_before_dispatch)
        ASSERT_FALSE(second.handle().promise().result().has_value());
    ASSERT_FALSE(third.handle().promise().result().has_value());
    ASSERT_EQ(pool.pending_maintenance(), 0);
    ASSERT_EQ(pool.waiter_count(), 0U);
    ASSERT_FALSE(original_client->is_open());
}

TEST(redis_contended_return_is_owned_and_allocation_free)
{
    verify_redis_contended_return(true);
}

TEST(redis_contended_return_hands_the_original_client_to_the_waiter)
{
    verify_redis_contended_return(false);
}
