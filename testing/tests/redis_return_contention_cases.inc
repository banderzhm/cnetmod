namespace {
thread_local cnetmod::redis::pooled_connection* redis_return_lease{};
thread_local bool redis_return_allocation_free{};
} // namespace

TEST(redis_lease_return_resumes_maintenance_without_allocation)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(
        cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(
        {cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());

    cnetmod::redis::connection_pool pool{*io,
        {.host = "127.0.0.1",
            .port = listener->local_endpoint()->port(),
            .resp3 = false,
            .initial_size = 1,
            .max_size = 1,
            .ping_interval = std::chrono::hours{1}}};
    auto run = pool.async_run();
    run.handle().resume();
    cnetmod::cancel_token token;
    auto pending = pool.async_get_connection(token);
    pending.handle().resume();
    const auto acquire_limit =
        std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (!pending.handle().done() &&
        std::chrono::steady_clock::now() < acquire_limit)
        (void)io->poll();
    ASSERT_TRUE(pending.handle().done());
    if (!pending.handle().done())
        std::terminate();

    auto lease = pending.handle().promise().result();
    ASSERT_TRUE(lease.has_value());
    redis_return_lease = &*lease;
    redis_return_allocation_free = false;
    before_allocation = []() noexcept
    {
        fail_after = 0;
        *redis_return_lease = cnetmod::redis::pooled_connection{};
        redis_return_allocation_free = fail_after.has_value();
        fail_after.reset();
    };

    // Trigger the hook after the maintenance coroutine registered its wait on
    // the checked-out node. Returning the lease from this noexcept callback
    // must not allocate or terminate.
    auto trigger = std::string(256, 'x');
    (void)trigger;
    before_allocation = nullptr;
    redis_return_lease = nullptr;
    ASSERT_TRUE(redis_return_allocation_free);
    ASSERT_FALSE(lease->valid());

    pool.request_stop();
    auto stop = pool.cancel();
    stop.handle().resume();
    const auto stop_limit =
        std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while ((!stop.handle().done() || !run.handle().done()) &&
        std::chrono::steady_clock::now() < stop_limit)
        (void)io->poll();
    ASSERT_TRUE(stop.handle().done());
    ASSERT_TRUE(run.handle().done());
    if (!stop.handle().done() || !run.handle().done())
        std::terminate();
    stop.handle().promise().result();
    run.handle().promise().result();
    ASSERT_EQ(pool.pending_maintenance(), 0);
}
