TEST(mongodb_contended_close_posts_without_allocation_and_is_joined)
{
    static thread_local cnetmod::mongodb::connection_pool* closing_pool = nullptr;
    static thread_local bool notified = false;
    static thread_local bool allocation_free = false;
    verify_mongodb_return_without_allocation(false, {}, {},
        mongodb_timeout_scenario::none, false, false, {},
        [&](auto& pool, auto& client, auto& io, auto&)
        {
            auto checking = pool.health_check();
            closing_pool = &pool;
            notified = allocation_free = false;
            before_allocation = []() noexcept
            {
                fail_after = 0;
                closing_pool->close();
                allocation_free = fail_after == 0;
                fail_after.reset();
                notified = true;
            };
            checking.handle().resume();
            before_allocation = nullptr;
            ASSERT_TRUE(checking.handle().done());
            checking.handle().promise().result();
            ASSERT_TRUE(notified);
            ASSERT_TRUE(allocation_free);
            auto closing = pool.async_close();
            closing.handle().resume();
            ASSERT_FALSE(closing.handle().done());
            const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            while (!closing.handle().done() && std::chrono::steady_clock::now() < limit)
                (void)io.poll();
            if (!closing.handle().done())
                std::terminate();
            closing.handle().promise().result();
            ASSERT_TRUE(client.is_open());
            ASSERT_EQ(pool.checked_out_count(), 1U);
            closing_pool = nullptr;
        });
}

TEST(mongodb_queued_health_faults_preserve_borrower_and_remove_waiter)
{
    for (unsigned phase = 0; phase < 3; ++phase)
    {
        unsigned failures = 0;
        for (std::size_t position = 0; position < 32; ++position)
            verify_mongodb_return_without_allocation(false, {}, {},
                mongodb_timeout_scenario::none, false, false, {},
                [&](auto& pool, auto& client, auto& io, auto&)
                {
                    cnetmod::cancel_token cancellation;
                    auto operation = pool.health_check(cancellation);
                    if (phase == 0)
                    {
                        fail_after = position;
                        operation.handle().resume();
                    }
                    else
                    {
                        operation.handle().resume();
                        ASSERT_FALSE(operation.handle().done());
                        ASSERT_EQ(pool.waiter_count(), 1U);
                        if (phase == 2)
                        {
                            (void)io.poll();
                            cancellation.cancel();
                        }
                        fail_after = position;
                        (void)io.poll();
                        if (phase == 2)
                        {
                            const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
                            while (!operation.handle().done() && std::chrono::steady_clock::now() < limit)
                                (void)io.poll();
                            if (!operation.handle().done())
                                std::terminate();
                        }
                    }
                    fail_after.reset();
                    if (!operation.handle().done())
                    {
                        fail_after = 0;
                        cancellation.cancel();
                        ASSERT_TRUE(fail_after == 0);
                        fail_after.reset();
                    }
                    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
                    while (!operation.handle().done() && std::chrono::steady_clock::now() < limit)
                        (void)io.poll();
                    if (!operation.handle().done())
                        std::terminate();
                    try
                    {
                        auto result = operation.handle().promise().result();
                        ASSERT_FALSE(result.has_value());
                        ASSERT_TRUE(result.error().code == cnetmod::mongodb::error_code::operation_cancelled);
                    }
                    catch (const std::bad_alloc&)
                    {
                        ++failures;
                    }
                    catch (const std::system_error& error)
                    {
                        ASSERT_TRUE(error.code() == std::errc::not_enough_memory);
                        ++failures;
                    }
                    ASSERT_FALSE(cancellation.pending_.load());
                    ASSERT_EQ(pool.waiter_count(), 0U);
                    ASSERT_EQ(pool.connecting_count(), 0U);
                    ASSERT_EQ(pool.checked_out_count(), 1U);
                    ASSERT_TRUE(client.is_open());
                    cancellation.reset();
                    cancellation.cancel();
                    (void)io.poll();
                    ASSERT_TRUE(client.is_open());
                });
        ASSERT_TRUE(failures > 0);
    }
}

TEST(mongodb_cancellable_ping_faults_retire_transport_and_registration)
{
    unsigned failures = 0;
    for (std::size_t position = 0; position < 48; ++position)
        verify_mongodb_return_without_allocation(false, {}, {},
            mongodb_timeout_scenario::none, false, false, {},
            [&](auto&, auto& client, auto& io, auto&)
            {
                cnetmod::cancel_token cancellation;
                auto operation = client.ping(cancellation);
                fail_after = position;
                operation.handle().resume();
                fail_after.reset();
                if (!operation.handle().done())
                {
                    fail_after = 0;
                    cancellation.cancel();
                    ASSERT_TRUE(fail_after == 0);
                    fail_after.reset();
                }
                const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
                while (!operation.handle().done() && std::chrono::steady_clock::now() < limit)
                    (void)io.poll();
                if (!operation.handle().done())
                    std::terminate();
                try
                {
                    auto result = operation.handle().promise().result();
                    ASSERT_FALSE(result.has_value());
                }
                catch (const std::bad_alloc&)
                {
                    ++failures;
                }
                catch (const std::system_error& error)
                {
                    ASSERT_TRUE(error.code() == std::errc::not_enough_memory);
                    ++failures;
                }
                ASSERT_FALSE(cancellation.pending_.load());
                ASSERT_FALSE(client.is_open());
                cancellation.reset();
                cancellation.cancel();
                (void)io.poll();
                ASSERT_FALSE(cancellation.pending_.load());
            });
    ASSERT_TRUE(failures > 0);
}
