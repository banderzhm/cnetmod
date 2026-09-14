namespace {
thread_local cnetmod::postgresql::pooled_connection* postgresql_return_lease{};
thread_local bool postgresql_return_injected{};
thread_local bool postgresql_return_allocation_free{};
} // namespace

static void verify_postgresql_waiter_allocation(bool contended_return)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    cnetmod::postgresql::connection_pool_options options;
    options.minimum_connections = 0;
    options.maximum_connections = 1;
    options.connection.host = "127.0.0.1";
    options.connection.port = listener->local_endpoint()->port();
    options.connection.tls = cnetmod::postgresql::tls_mode::disable;
    options.connection.maximum_connect_attempts = 1;
    cnetmod::postgresql::connection_pool pool{*io, options};
    unsigned finished = 0;
    auto finish = [&]
    {
        if (++finished == 2)
            io->stop();
    };
    auto peer = [&]() -> cnetmod::task<void>
    {
        auto connection = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(connection.has_value());
        std::array<std::byte, 4096> bytes{};
        ASSERT_TRUE((co_await cnetmod::async_read(*io, *connection,
                         cnetmod::mutable_buffer{bytes.data(), bytes.size()}))
                .has_value());
        const std::array<std::uint8_t, 15> ready{
            'R', 0, 0, 0, 8, 0, 0, 0, 0, 'Z', 0, 0, 0, 5, 'I'};
        ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *connection,
                         cnetmod::const_buffer{ready.data(), ready.size()}))
                .has_value());
        for (;;)
        {
            auto received = co_await cnetmod::async_read(*io, *connection,
                cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            if (!received || *received == 0)
                break;
        }
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        cnetmod::cancel_token first_token;
        auto first = co_await pool.acquire(first_token);
        ASSERT_TRUE(first.has_value());
        using acquisition = cnetmod::task<std::expected<cnetmod::postgresql::pooled_connection, std::error_code>>;
        std::vector<std::unique_ptr<cnetmod::cancel_token>> tokens;
        std::vector<acquisition> queued;
        tokens.reserve(1024);
        queued.reserve(1024);
        bool injected = false;
        for (unsigned index = 0; index != 1024; ++index)
        {
            auto token = std::make_unique<cnetmod::cancel_token>();
            auto pending = pool.acquire(*token);
            const auto before = pool.waiter_count();
            if (contended_return && index >= 4)
            {
                postgresql_return_lease = &*first;
                postgresql_return_injected = false;
                before_allocation = []() noexcept
                {
                    // The waiter deque grows while the pool state lock is held.
                    fail_after = 0;
                    *postgresql_return_lease = cnetmod::postgresql::pooled_connection{};
                    postgresql_return_allocation_free = fail_after == 0;
                    fail_after.reset();
                    postgresql_return_injected = true;
                };
            }
            else if (index >= 4)
                fail_after = 0;
            pending.handle().resume();
            before_allocation = nullptr;
            postgresql_return_lease = nullptr;
            injected = index >= 4 && (contended_return ? postgresql_return_injected : !fail_after.has_value());
            fail_after.reset();
            if (injected && !contended_return)
            {
                ASSERT_TRUE(pending.handle().done());
                auto result = pending.handle().promise().result();
                ASSERT_FALSE(result.has_value());
                ASSERT_EQ(result.error(), std::make_error_code(std::errc::not_enough_memory));
                ASSERT_EQ(pool.waiter_count(), before);
                break;
            }
            ASSERT_FALSE(pending.handle().done());
            tokens.push_back(std::move(token));
            queued.push_back(std::move(pending));
            if (injected)
                break;
        }
        ASSERT_TRUE(injected);
        ASSERT_TRUE(queued.size() >= 4);
        if (contended_return)
        {
            ASSERT_TRUE(postgresql_return_allocation_free);
            ASSERT_FALSE(first->valid());
            while (!queued.front().handle().done())
                co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        }
        for (auto& token : tokens)
        {
            fail_after = 0;
            token->cancel();
            const bool allocation_free = fail_after == 0;
            fail_after.reset();
            ASSERT_TRUE(allocation_free);
        }
        std::size_t granted = 0;
        for (auto& pending : queued)
        {
            while (!pending.handle().done())
                co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
            auto result = pending.handle().promise().result();
            if (result)
                ++granted;
            else
                ASSERT_EQ(result.error(), std::make_error_code(std::errc::operation_canceled));
        }
        ASSERT_EQ(granted, contended_return ? std::size_t{1} : std::size_t{0});
        ASSERT_EQ(pool.waiter_count(), std::size_t{0});
        ASSERT_EQ(pool.checked_out_count(), contended_return ? std::size_t{0} : std::size_t{1});
        *first = cnetmod::postgresql::pooled_connection{};
        auto reused = co_await pool.acquire(first_token);
        ASSERT_TRUE(reused.has_value());
        cnetmod::cancel_token handoff_token;
        auto handoff = pool.acquire(handoff_token);
        handoff.handle().resume();
        ASSERT_EQ(pool.waiter_count(), std::size_t{1});
        fail_after = 0;
        *reused = cnetmod::postgresql::pooled_connection{};
        const bool handoff_allocation_free = fail_after == 0;
        fail_after.reset();
        ASSERT_TRUE(handoff_allocation_free);
        while (!handoff.handle().done())
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        auto handed_off = handoff.handle().promise().result();
        ASSERT_TRUE(handed_off.has_value());
        if (handed_off)
            *handed_off = cnetmod::postgresql::pooled_connection{};
        auto failed_close = pool.close();
        fail_after = 0;
        failed_close.handle().resume();
        const bool close_failure_injected = !fail_after.has_value();
        fail_after.reset();
        ASSERT_TRUE(close_failure_injected);
        while (!failed_close.handle().done())
            co_await cnetmod::async_sleep(*io, std::chrono::milliseconds{1});
        bool allocation_error = false;
        try
        {
            failed_close.handle().promise().result();
        }
        catch (const std::bad_alloc&)
        {
            allocation_error = true;
        }
        ASSERT_TRUE(allocation_error);
        ASSERT_EQ(pool.size(), std::size_t{1});
        ASSERT_EQ(pool.checked_out_count(), std::size_t{0});
        auto rejected_after_close = co_await pool.acquire(first_token);
        ASSERT_FALSE(rejected_after_close.has_value());
        if (!rejected_after_close)
            ASSERT_EQ(rejected_after_close.error(), std::make_error_code(std::errc::operation_canceled));
        co_await pool.close();
        ASSERT_EQ(pool.size(), std::size_t{0});
        finish();
    };
    auto peer_task = peer();
    auto exercise_task = exercise();
    io->post(peer_task.handle());
    io->post(exercise_task.handle());
    io->run();
    ASSERT_TRUE(peer_task.handle().done());
    ASSERT_TRUE(exercise_task.handle().done());
    peer_task.handle().promise().result();
    exercise_task.handle().promise().result();
}

TEST(postgresql_waiter_growth_failure_preserves_existing_waiters)
{
    verify_postgresql_waiter_allocation(false);
}

TEST(postgresql_contended_return_uses_slot_owned_notification)
{
    verify_postgresql_waiter_allocation(true);
}

TEST(postgresql_empty_close_requires_no_staging_allocation)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::postgresql::connection_pool_options options;
    options.minimum_connections = 0;
    cnetmod::postgresql::connection_pool pool{*io, options};
    auto closing = pool.close();
    fail_after = 0;
    closing.handle().resume();
    const bool allocation_free = fail_after == 0;
    fail_after.reset();
    ASSERT_TRUE(allocation_free);
    ASSERT_TRUE(closing.handle().done());
    closing.handle().promise().result();
    ASSERT_EQ(pool.size(), std::size_t{0});
}
