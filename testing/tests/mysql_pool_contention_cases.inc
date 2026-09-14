namespace {

struct mysql_return_probe
{
    cnetmod::mysql::connection_pool* pool;
    std::optional<cnetmod::mysql::pooled_connection>* lease;
    bool destroy_lease;
    bool invoked = false;
    bool contended = false;
    bool allocation_free = false;
};

thread_local mysql_return_probe* active_mysql_return_probe = nullptr;

} // namespace

static void verify_mysql_contended_return(bool destroy_lease)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    if (!listener)
        return;
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!endpoint)
        return;
    cnetmod::mysql::pool_params options;
    options.port = endpoint->port();
    options.ssl = cnetmod::mysql::ssl_mode::disable;
    options.initial_size = 1;
    options.max_size = 3;
    cnetmod::mysql::connection_pool pool{*io, options};
    std::optional<cnetmod::socket> peer;
    bool reset_received = false;
    auto server = [&]() -> cnetmod::task<void>
    {
        auto accepted = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(accepted.has_value());
        if (!accepted)
            co_return;
        peer.emplace(std::move(*accepted));
        std::vector<std::uint8_t> greeting{10, '8', '.', '0', 0,
            1, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0,
            0, 2, 45, 2, 0, 0, 0, 0};
        greeting.resize(greeting.size() + 10, 0);
        greeting.insert(greeting.begin(), {static_cast<std::uint8_t>(greeting.size()), 0, 0, 0});
        ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *peer,
                         cnetmod::const_buffer{greeting.data(), greeting.size()}))
                .has_value());
        std::array<std::uint8_t, 4096> login{};
        std::size_t used = 0;
        std::size_t required = 4;
        while (used < required)
        {
            auto received = co_await cnetmod::async_read(*io, *peer,
                cnetmod::mutable_buffer{login.data() + used, required - used});
            ASSERT_TRUE(received && *received > 0);
            if (!received || *received == 0)
                co_return;
            used += *received;
            if (used == 4)
            {
                required = 4 + login[0] + (std::size_t{login[1]} << 8) +
                    (std::size_t{login[2]} << 16);
                ASSERT_TRUE(required <= login.size());
                if (required > login.size())
                    co_return;
            }
        }
        const std::array<std::uint8_t, 11> authenticated{7, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0};
        ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *peer,
                         cnetmod::const_buffer{authenticated.data(), authenticated.size()}))
                .has_value());
        if (destroy_lease)
        {
            std::array<std::uint8_t, 5> reset{};
            used = 0;
            while (used < reset.size())
            {
                auto received = co_await cnetmod::async_read(*io, *peer,
                    cnetmod::mutable_buffer{reset.data() + used, reset.size() - used});
                ASSERT_TRUE(received && *received > 0);
                if (!received || *received == 0)
                    co_return;
                used += *received;
            }
            reset_received = reset == std::array<std::uint8_t, 5>{1, 0, 0, 0, 0x1f};
            ASSERT_TRUE(reset_received);
            ASSERT_EQ(pool.idle_count(), 0U);
            const std::array<std::uint8_t, 11> reset_ok{7, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0};
            ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *peer,
                             cnetmod::const_buffer{reset_ok.data(), reset_ok.size()}))
                    .has_value());
        }
    };
    auto maintenance = pool.async_run();
    auto workload = [&]() -> cnetmod::task<void>
    {
        auto lease = co_await pool.async_get_connection();
        ASSERT_TRUE(lease.has_value());
        if (lease)
        {
            std::optional<cnetmod::mysql::pooled_connection> held{std::move(*lease)};
            cnetmod::cancel_token first_token;
            cnetmod::cancel_token second_token;
            auto first = pool.async_get_connection(first_token);
            auto second = pool.async_get_connection(second_token);
            first.handle().resume();
            ASSERT_EQ(pool.waiter_count(), 1U);
            mysql_return_probe probe{&pool, &held, destroy_lease};
            active_mysql_return_probe = &probe;
            before_allocation = []() noexcept
            {
                auto& probe = *active_mysql_return_probe;
                probe.invoked = true;
                const auto unavailable = probe.pool->try_get_connection();
                probe.contended = !unavailable && unavailable.error() == std::errc::resource_unavailable_try_again;
                fail_after = 0;
                try
                {
                    if (probe.destroy_lease)
                        probe.lease->reset();
                    else
                        probe.lease->value().return_without_reset();
                    probe.allocation_free = fail_after && *fail_after == 0;
                }
                catch (...)
                {
                    probe.allocation_free = false;
                }
                fail_after.reset();
            };
            second.handle().resume();
            before_allocation = nullptr;
            active_mysql_return_probe = nullptr;
            ASSERT_TRUE(probe.invoked);
            ASSERT_TRUE(probe.contended);
            ASSERT_TRUE(probe.allocation_free);
            // Restore ownership on an old implementation that failed the return.
            if (held && held->valid())
                held->return_without_reset();
            second_token.cancel();
            while (!first.handle().done() || !second.handle().done())
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
            auto handed_off = first.handle().promise().result();
            ASSERT_TRUE(handed_off.has_value());
            if (handed_off)
                handed_off->return_without_reset();
            auto cancelled = second.handle().promise().result();
            ASSERT_FALSE(cancelled.has_value());
            ASSERT_EQ(pool.waiter_count(), 0U);
        }
        pool.request_stop();
        while (!maintenance.handle().done())
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
        maintenance.handle().promise().result();
        io->stop();
    };
    auto server_task = server();
    auto work = workload();
    io->post(server_task.handle());
    io->post(maintenance.handle());
    io->post(work.handle());
    io->run();
    ASSERT_TRUE(server_task.handle().done());
    ASSERT_TRUE(work.handle().done());
    server_task.handle().promise().result();
    work.handle().promise().result();
    ASSERT_EQ(reset_received, destroy_lease);
}

TEST(mysql_pool_contended_return_needs_no_nested_allocation)
{
    verify_mysql_contended_return(false);
}

TEST(mysql_pool_contended_destructor_defers_reset_without_allocating)
{
    verify_mysql_contended_return(true);
}
