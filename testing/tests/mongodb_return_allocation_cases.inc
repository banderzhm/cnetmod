namespace {
thread_local cnetmod::mongodb::pooled_connection* mongodb_contended_lease{};
thread_local bool mongodb_contended_return_allocation_free{};
thread_local cnetmod::io_context* mongodb_contended_io{};
thread_local std::size_t mongodb_contended_dispatches{};
} // namespace

enum class mongodb_timeout_scenario
{
    none,
    allocation_failure,
    elapsed,
    caller_cancelled,
    foreign_thread_cancelled,
    active_handoff,
    concurrent_handoff
};

enum class mongodb_stream_failure_scenario
{
    none,
    immediate,
    after_suspend
};

static void verify_mongodb_command_failure(cnetmod::mongodb::connection& client,
    cnetmod::io_context& io, std::size_t allocation)
{
    auto operation = client.ping();
    fail_after = allocation;
    operation.handle().resume();
    fail_after.reset();
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!operation.handle().done() && std::chrono::steady_clock::now() < limit)
    {
        client.cancel_active_command();
        (void)io.poll();
    }
    if (!operation.handle().done())
    {
        logger::error{"MongoDB command allocation fault {} did not settle", allocation};
        std::terminate();
    }
    bool allocation_failed = false;
    try
    {
        auto result = operation.handle().promise().result();
        ASSERT_FALSE(result.has_value());
    }
    catch (const std::bad_alloc&)
    {
        allocation_failed = true;
    }
    catch (const std::system_error& error)
    {
        ASSERT_TRUE(error.code() == std::errc::not_enough_memory);
        allocation_failed = true;
    }
    if (allocation == 0)
        ASSERT_TRUE(allocation_failed);
}

static void verify_mongodb_stream_consumer_failure(cnetmod::mongodb::connection& client,
    cnetmod::io_context& io, cnetmod::socket& peer, mongodb_stream_failure_scenario scenario)
{
    bool called = false;
    bool resumed = false;
    auto stream = client.command_stream("admin", {{"ping", std::int32_t{1}}},
        [&](cnetmod::mongodb::bson_document) -> cnetmod::task<cnetmod::mongodb::result<void>>
        {
            called = true;
            if (scenario == mongodb_stream_failure_scenario::after_suspend)
            {
                auto waited = co_await cnetmod::async_timer_wait(io, std::chrono::milliseconds{1});
                ASSERT_TRUE(waited.has_value());
                resumed = true;
            }
            throw std::runtime_error{"stream-consumer-failure"};
            co_return cnetmod::mongodb::result<void>{};
        });
    stream.handle().resume();
    auto reply = cnetmod::mongodb::encode_command_message(1, {{"ok", 1.0}}, 65536);
    ASSERT_TRUE(reply.has_value());
    // This fresh client used request 1 for hello; the stream is request 2.
    cnetmod::buffer_writer header{cnetmod::mutable_buffer{reply->data() + 8, 4}};
    ASSERT_TRUE(header.write_u32_le(2));
    (*reply)[16] = std::byte{cnetmod::mongodb::op_message_more_to_come};
    auto sending = cnetmod::async_write_all(io, peer,
        cnetmod::const_buffer{reply->data(), reply->size()});
    sending.handle().resume();
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while ((!stream.handle().done() || !sending.handle().done()) &&
        std::chrono::steady_clock::now() < limit)
        (void)io.poll();
    if (!stream.handle().done() || !sending.handle().done())
        std::terminate();
    ASSERT_TRUE(sending.handle().promise().result().has_value());
    bool propagated = false;
    try
    {
        (void)stream.handle().promise().result();
    }
    catch (const std::runtime_error& error)
    {
        propagated = std::string_view{error.what()} == "stream-consumer-failure";
    }
    ASSERT_TRUE(called);
    ASSERT_TRUE(resumed == (scenario == mongodb_stream_failure_scenario::after_suspend));
    ASSERT_TRUE(propagated);
    ASSERT_FALSE(client.is_open());
}

static void verify_mongodb_return_without_allocation(bool discard,
    std::optional<std::size_t> health_failure = {},
    std::optional<std::size_t> creation_failure = {},
    mongodb_timeout_scenario timeout_scenario = mongodb_timeout_scenario::none,
    bool contended_return = false, bool close_before_return_dispatch = false,
    std::optional<std::size_t> command_failure = {},
    std::function<void(cnetmod::mongodb::connection_pool&, cnetmod::mongodb::connection&,
        cnetmod::io_context&, cnetmod::socket&)>
        inspect_lease = {})
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    std::optional<cnetmod::socket> peer;
    auto handshake = [&]() -> cnetmod::task<bool>
    {
        auto accepted = co_await cnetmod::async_accept(*io, *listener);
        if (!accepted)
            co_return false;
        peer.emplace(std::move(*accepted));
        std::vector<std::byte> request(16);
        std::size_t received = 0;
        for (unsigned phase = 0; phase < 2; ++phase)
        {
            while (received < request.size())
            {
                auto count = co_await cnetmod::async_read(*io, *peer,
                    cnetmod::mutable_buffer{request.data() + received, request.size() - received});
                if (!count || *count == 0)
                    co_return false;
                received += *count;
            }
            if (phase == 0)
            {
                cnetmod::buffer_reader reader{cnetmod::const_buffer{request.data(), request.size()}};
                const auto length = reader.read_u32_le();
                if (!length || *length < 16 || *length > 65536)
                    co_return false;
                request.resize(*length);
            }
        }
        auto response = cnetmod::mongodb::encode_command_message(1,
            {{"ok", 1.0}, {"maxWireVersion", std::int32_t{17}}, {"isWritablePrimary", true}}, 65536);
        if (!response)
            co_return false;
        std::copy_n(request.begin() + 4, 4, response->begin() + 8);
        co_return (co_await cnetmod::async_write_all(*io, *peer,
                       cnetmod::const_buffer{response->data(), response->size()}))
            .has_value();
    };
    auto server = handshake();
    server.handle().resume();
    cnetmod::mongodb::connection_pool_options options;
    options.connection.port = listener->local_endpoint()->port();
    options.connection.connect_timeout = std::chrono::seconds{1};
    options.connection.command_timeout = std::chrono::seconds{1};
    if (command_failure)
        options.connection.command_timeout = std::chrono::seconds{30};
    options.maximum_size = options.maximum_connecting = 1;
    options.wait_queue_timeout = std::chrono::seconds{2};
    if (timeout_scenario == mongodb_timeout_scenario::elapsed)
        options.wait_queue_timeout = std::chrono::milliseconds{30};
    cnetmod::mongodb::connection_pool pool{*io, options};
    if (creation_failure)
    {
        auto failing = pool.acquire(std::stop_token{});
        fail_after = *creation_failure;
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
        ASSERT_EQ(pool.size(), 0U);
        ASSERT_EQ(pool.checked_out_count(), 0U);
    }
    auto first = pool.acquire();
    first.handle().resume();
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while ((!first.handle().done() || !server.handle().done()) && std::chrono::steady_clock::now() < limit)
        (void)io->poll();
    if (!first.handle().done() || !server.handle().done())
    {
        ASSERT_TRUE(first.handle().done());
        ASSERT_TRUE(server.handle().done());
        std::terminate();
    }
    ASSERT_TRUE(server.handle().promise().result());
    auto lease = first.handle().promise().result();
    ASSERT_TRUE(lease.has_value());
    auto* original = &lease->get();
    if (inspect_lease)
    {
        inspect_lease(pool, *original, *io, *peer);
        lease->discard();
        auto closing = pool.async_close();
        closing.handle().resume();
        ASSERT_TRUE(closing.handle().done());
        closing.handle().promise().result();
        ASSERT_EQ(pool.checked_out_count(), 0U);
        return;
    }
    if (command_failure)
    {
        verify_mongodb_command_failure(*original, *io, *command_failure);
        lease->discard();
        auto closing = pool.async_close();
        closing.handle().resume();
        ASSERT_TRUE(closing.handle().done());
        closing.handle().promise().result();
        ASSERT_EQ(pool.checked_out_count(), 0U);
        return;
    }
    if (health_failure)
    {
        *lease = cnetmod::mongodb::pooled_connection{};
        auto health = pool.health_check();
        fail_after = *health_failure;
        health.handle().resume();
        fail_after.reset();
        if (!health.handle().done())
            std::terminate();
        bool failed = false;
        try
        {
            health.handle().promise().result();
        }
        catch (const std::bad_alloc&)
        {
            failed = true;
        }
        ASSERT_TRUE(failed);
        ASSERT_EQ(pool.checked_out_count(), 0U);
        if (*health_failure != 0)
        {
            ASSERT_EQ(pool.size(), 0U);
            auto close = pool.async_close();
            close.handle().resume();
            ASSERT_TRUE(close.handle().done());
            close.handle().promise().result();
            return;
        }
        ASSERT_EQ(pool.idle_count(), 1U);
        auto reacquire = pool.acquire();
        reacquire.handle().resume();
        if (!reacquire.handle().done())
            std::terminate();
        lease = reacquire.handle().promise().result();
        ASSERT_TRUE(lease.has_value());
        ASSERT_TRUE(&lease->get() == original);
    }
    std::stop_source borrower_stop;
    auto second = pool.acquire(borrower_stop.get_token());
    second.handle().resume();
    ASSERT_EQ(pool.waiter_count(), 1U);
    if (timeout_scenario == mongodb_timeout_scenario::active_handoff ||
        timeout_scenario == mongodb_timeout_scenario::concurrent_handoff)
    {
        (void)io->poll();
        ASSERT_FALSE(second.handle().done());
        ASSERT_EQ(pool.waiter_count(), 1U);
    }
    else if (timeout_scenario != mongodb_timeout_scenario::none)
    {
        if (timeout_scenario == mongodb_timeout_scenario::allocation_failure)
            fail_after = 0;
        (void)io->poll();
        fail_after.reset();
        if (timeout_scenario == mongodb_timeout_scenario::caller_cancelled)
        {
            ASSERT_FALSE(second.handle().done());
            borrower_stop.request_stop();
        }
        if (timeout_scenario == mongodb_timeout_scenario::foreign_thread_cancelled)
        {
            ASSERT_FALSE(second.handle().done());
            bool allocation_free = false;
            std::jthread requester{[&]()
                {
                    fail_after = 0;
                    borrower_stop.request_stop();
                    allocation_free = fail_after == 0;
                    fail_after.reset();
                }};
            requester.join();
            ASSERT_TRUE(allocation_free);
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (!second.handle().done() && std::chrono::steady_clock::now() < deadline)
            (void)io->poll();
        if (!second.handle().done())
        {
            ASSERT_TRUE(second.handle().done());
            std::terminate();
        }
        bool failed = false;
        try
        {
            auto outcome = second.handle().promise().result();
            ASSERT_FALSE(outcome.has_value());
            ASSERT_TRUE(timeout_scenario != mongodb_timeout_scenario::allocation_failure);
            ASSERT_TRUE(outcome.error().code ==
                (timeout_scenario == mongodb_timeout_scenario::elapsed
                        ? cnetmod::mongodb::error_code::pool_exhausted
                        : cnetmod::mongodb::error_code::operation_cancelled));
        }
        catch (const std::bad_alloc&)
        {
            failed = true;
        }
        ASSERT_EQ(failed, timeout_scenario == mongodb_timeout_scenario::allocation_failure);
        ASSERT_EQ(pool.waiter_count(), 0U);
        ASSERT_EQ(pool.checked_out_count(), 1U);
        second = pool.acquire();
        second.handle().resume();
        ASSERT_EQ(pool.waiter_count(), 1U);
    }
    if (contended_return)
    {
        // Start the timeout before installing allocation faults in dispatch.
        (void)io->poll();
        mongodb_contended_lease = &*lease;
        mongodb_contended_return_allocation_free = false;
        auto health = pool.health_check();
        // Task construction must precede the hook; reserve runs under the lock.
        before_allocation = []() noexcept
        {
            fail_after = 0;
            *mongodb_contended_lease = cnetmod::mongodb::pooled_connection{};
            mongodb_contended_return_allocation_free = fail_after == 0;
            fail_after.reset();
        };
        health.handle().resume();
        before_allocation = nullptr;
        mongodb_contended_lease = nullptr;
        ASSERT_TRUE(health.handle().done());
        health.handle().promise().result();
        ASSERT_TRUE(mongodb_contended_return_allocation_free);
        ASSERT_EQ(pool.checked_out_count(), 1U);
        for (unsigned retry = 0; retry < 3; ++retry)
        {
            auto still_locked = pool.health_check();
            mongodb_contended_io = io.get();
            mongodb_contended_dispatches = 0;
            before_allocation = []() noexcept
            {
                fail_after = 0;
                mongodb_contended_dispatches = mongodb_contended_io->poll();
                mongodb_contended_return_allocation_free = fail_after == 0;
                fail_after.reset();
            };
            still_locked.handle().resume();
            before_allocation = nullptr;
            mongodb_contended_io = nullptr;
            ASSERT_TRUE(still_locked.handle().done());
            still_locked.handle().promise().result();
            ASSERT_TRUE(mongodb_contended_dispatches > 0);
            ASSERT_TRUE(mongodb_contended_return_allocation_free);
            ASSERT_EQ(pool.checked_out_count(), 1U);
            ASSERT_EQ(pool.waiter_count(), 1U);
            ASSERT_FALSE(second.handle().done());
        }
    }
    if (discard)
        lease->discard();
    std::atomic_bool race_started{false};
    bool cancellation_allocation_free = false;
    std::optional<std::jthread> racing_requester;
    if (timeout_scenario == mongodb_timeout_scenario::concurrent_handoff)
    {
        racing_requester.emplace([&]()
            {
                while (!race_started.load(std::memory_order_acquire))
                    std::this_thread::yield();
                fail_after = 0;
                borrower_stop.request_stop();
                cancellation_allocation_free = fail_after == 0;
                fail_after.reset();
            });
        race_started.store(true, std::memory_order_release);
        (void)io->poll();
    }
    fail_after = 0;
    *lease = cnetmod::mongodb::pooled_connection{};
    const bool allocation_free = fail_after == 0;
    fail_after.reset();
    ASSERT_TRUE(allocation_free);
    if (racing_requester)
    {
        racing_requester->join();
        ASSERT_TRUE(cancellation_allocation_free);
    }
    ASSERT_EQ(pool.waiter_count(), contended_return ? 1U : 0U);
    cnetmod::task<void> pending_close;
    if (close_before_return_dispatch)
    {
        pending_close = pool.async_close();
        pending_close.handle().resume();
        ASSERT_FALSE(pending_close.handle().done());
    }
    if (discard)
    {
        auto closing = pool.async_close();
        closing.handle().resume();
        ASSERT_TRUE(closing.handle().done());
        closing.handle().promise().result();
    }
    const auto handoff_limit = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (!second.handle().done() && std::chrono::steady_clock::now() < handoff_limit)
        (void)io->poll();
    if (!second.handle().done())
    {
        ASSERT_TRUE(second.handle().done());
        std::terminate();
    }
    auto result = second.handle().promise().result();
    if (discard || close_before_return_dispatch)
    {
        ASSERT_FALSE(result.has_value());
        ASSERT_TRUE(result.error().code == cnetmod::mongodb::error_code::connection_closed);
    }
    else if (timeout_scenario == mongodb_timeout_scenario::concurrent_handoff && !result)
    {
        ASSERT_TRUE(result.error().code == cnetmod::mongodb::error_code::operation_cancelled);
    }
    else
    {
        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(&result->get() == original);
        ASSERT_TRUE(result->get().is_open());
        *result = cnetmod::mongodb::pooled_connection{};
    }
    if (timeout_scenario == mongodb_timeout_scenario::concurrent_handoff)
    {
        ASSERT_EQ(pool.checked_out_count(), 0U);
        ASSERT_EQ(pool.waiter_count(), 0U);
        auto next = pool.acquire();
        next.handle().resume();
        if (!next.handle().done())
            std::terminate();
        auto reused = next.handle().promise().result();
        ASSERT_TRUE(reused.has_value());
        ASSERT_TRUE(&reused->get() == original);
        ASSERT_TRUE(reused->get().is_open());
    }
    auto closing = pool.async_close();
    closing.handle().resume();
    ASSERT_TRUE(closing.handle().done());
    closing.handle().promise().result();
    if (close_before_return_dispatch)
    {
        ASSERT_TRUE(pending_close.handle().done());
        pending_close.handle().promise().result();
    }
    (void)io->poll();
    ASSERT_EQ(pool.checked_out_count(), 0U);
}

TEST(mongodb_return_handoff_and_discard_do_not_allocate)
{
    verify_mongodb_return_without_allocation(false);
    verify_mongodb_return_without_allocation(true);
}

TEST(mongodb_health_allocation_failure_releases_claimed_slots)
{
    verify_mongodb_return_without_allocation(false, 0);
    verify_mongodb_return_without_allocation(false, 1);
}

TEST(mongodb_creation_allocation_failure_restores_admission)
{
    for (std::size_t failure = 0; failure < 4; ++failure)
        verify_mongodb_return_without_allocation(false, {}, failure);
}

TEST(mongodb_owned_timeout_propagates_execution_allocation_failure)
{
    verify_mongodb_return_without_allocation(false, {}, {}, mongodb_timeout_scenario::allocation_failure);
}

TEST(mongodb_owned_timeout_settles_after_elapsed_or_caller_cancel)
{
    verify_mongodb_return_without_allocation(false, {}, {}, mongodb_timeout_scenario::elapsed);
    verify_mongodb_return_without_allocation(false, {}, {}, mongodb_timeout_scenario::caller_cancelled);
    verify_mongodb_return_without_allocation(false, {}, {}, mongodb_timeout_scenario::active_handoff);
}

TEST(mongodb_foreign_thread_cancel_is_allocation_free_and_recoverable)
{
    verify_mongodb_return_without_allocation(false, {}, {}, mongodb_timeout_scenario::foreign_thread_cancelled);
}

TEST(mongodb_concurrent_cancel_and_handoff_preserve_single_lease)
{
    for (unsigned attempt = 0; attempt < 32; ++attempt)
        verify_mongodb_return_without_allocation(false, {}, {}, mongodb_timeout_scenario::concurrent_handoff);
}

TEST(mongodb_contended_return_is_owned_and_close_joins_dispatch)
{
    verify_mongodb_return_without_allocation(false, {}, {}, mongodb_timeout_scenario::none, true, false);
    verify_mongodb_return_without_allocation(false, {}, {}, mongodb_timeout_scenario::none, true, true);
}

TEST(mongodb_command_allocation_failures_settle_watchdog_and_io)
{
    for (std::size_t allocation = 0; allocation < 48; ++allocation)
        verify_mongodb_return_without_allocation(false, {}, {},
            mongodb_timeout_scenario::none, false, false, allocation);
}

TEST(mongodb_stream_consumer_exception_retires_transport)
{
    verify_mongodb_return_without_allocation(false, {}, {},
        mongodb_timeout_scenario::none, false, false, {},
        [](auto&, auto& client, auto& io, auto& peer)
        {
            verify_mongodb_stream_consumer_failure(client, io, peer,
                mongodb_stream_failure_scenario::immediate);
        });
}

TEST(mongodb_suspended_stream_consumer_exception_retires_transport)
{
    verify_mongodb_return_without_allocation(false, {}, {},
        mongodb_timeout_scenario::none, false, false, {},
        [](auto&, auto& client, auto& io, auto& peer)
        {
            verify_mongodb_stream_consumer_failure(client, io, peer,
                mongodb_stream_failure_scenario::after_suspend);
        });
}

TEST(mongodb_pool_close_preserves_borrowed_transport_until_return)
{
    verify_mongodb_return_without_allocation(false, {}, {},
        mongodb_timeout_scenario::none, false, false, {},
        [](auto& pool, auto& client, auto&, auto&)
        {
            auto closing = pool.async_close();
            closing.handle().resume();
            ASSERT_TRUE(closing.handle().done());
            closing.handle().promise().result();
            ASSERT_TRUE(client.is_open());
            ASSERT_EQ(pool.checked_out_count(), 1U);
        });
}

TEST(mongodb_pool_close_cancels_borrowed_command_before_transport_teardown)
{
    verify_mongodb_return_without_allocation(false, {}, {},
        mongodb_timeout_scenario::none, false, false, {},
        [](auto& pool, auto& client, auto& io, auto&)
        {
            auto command = client.ping();
            command.handle().resume();
            ASSERT_FALSE(command.handle().done());
            auto closing = pool.async_close();
            closing.handle().resume();
            ASSERT_TRUE(closing.handle().done());
            closing.handle().promise().result();
            ASSERT_EQ(pool.checked_out_count(), 1U);
            const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            while (!command.handle().done() && std::chrono::steady_clock::now() < limit)
                (void)io.poll();
            if (!command.handle().done())
                std::terminate();
            auto result = command.handle().promise().result();
            ASSERT_FALSE(result.has_value());
            ASSERT_TRUE(result.error().code == cnetmod::mongodb::error_code::operation_cancelled);
            ASSERT_FALSE(client.is_open());
        });
}
