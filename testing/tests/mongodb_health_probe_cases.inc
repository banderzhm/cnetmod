namespace {
auto reply_to_mongodb_ping(cnetmod::io_context& io, cnetmod::socket& peer,
    std::string_view command = "ping")
    -> cnetmod::task<bool>
{
    std::vector<std::byte> request(16);
    std::size_t received = 0;
    for (unsigned phase = 0; phase < 2; ++phase)
    {
        while (received < request.size())
        {
            auto count = co_await cnetmod::async_read(io, peer,
                cnetmod::mutable_buffer{request.data() + received, request.size() - received});
            if (!count || *count == 0)
                co_return false;
            received += *count;
        }
        if (phase == 0)
        {
            cnetmod::buffer_reader reader{cnetmod::const_buffer{request.data(), request.size()}};
            auto length = reader.read_u32_le();
            if (!length || *length < 16 || *length > 65536)
                co_return false;
            request.resize(*length);
        }
    }
    auto decoded = cnetmod::mongodb::decode_command_message(request, 65536);
    if (!decoded || !decoded->body.contains(command))
        co_return false;
    auto reply = cnetmod::mongodb::encode_command_message(1,
        {{"ok", 1.0}, {"maxWireVersion", std::int32_t{17}}, {"isWritablePrimary", true}}, 65536);
    if (!reply)
        co_return false;
    std::copy_n(request.begin() + 4, 4, reply->begin() + 8);
    co_return (co_await cnetmod::async_write_all(io, peer,
                   cnetmod::const_buffer{reply->data(), reply->size()}))
        .has_value();
}

void verify_successful_mongodb_probe(cnetmod::application::mongodb_service& service,
    cnetmod::io_context& io, cnetmod::socket& peer,
    cnetmod::observability::telemetry_hub& telemetry,
    cnetmod::application::task_supervisor& supervisor)
{
    std::array<cnetmod::cancel_token, 2> tokens;
    for (unsigned attempt = 0; attempt < 2; ++attempt)
    {
        auto borrowing = service.pool().acquire();
        borrowing.handle().resume();
        ASSERT_TRUE(borrowing.handle().done());
        auto borrowed = borrowing.handle().promise().result();
        ASSERT_TRUE(borrowed.has_value());
        cnetmod::cancel_token queued_cancellation;
        cnetmod::application::service_context queued_context{io, telemetry, supervisor,
            queued_cancellation, cnetmod::deadline::after(std::chrono::milliseconds{attempt == 0 ? 10 : 3000})};
        auto cancelled_probe = service.probe(queued_context);
        cancelled_probe.handle().resume();
        ASSERT_FALSE(cancelled_probe.handle().done());
        ASSERT_EQ(service.pool().waiter_count(), 1U);
        if (attempt != 0)
        {
            std::jthread cancelling{[&]()
                {
                    queued_cancellation.cancel();
                }};
            cancelling.join();
        }
        const auto cancellation_limit = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (!cancelled_probe.handle().done() && std::chrono::steady_clock::now() < cancellation_limit)
            (void)io.poll();
        if (!cancelled_probe.handle().done())
            std::terminate();
        auto cancelled = cancelled_probe.handle().promise().result();
        ASSERT_TRUE(cancelled.status == cnetmod::application::service_health::down);
        ASSERT_EQ(cancelled.error, std::make_error_code(attempt == 0 ? std::errc::timed_out : std::errc::operation_canceled));
        ASSERT_FALSE(queued_cancellation.pending_.load());
        ASSERT_EQ(service.pool().waiter_count(), 0U);
        ASSERT_TRUE(borrowed->get().is_open());
        auto replying = reply_to_mongodb_ping(io, peer);
        replying.handle().resume();
        auto& cancellation = tokens[attempt];
        cnetmod::application::service_context context{io, telemetry, supervisor,
            cancellation, cnetmod::deadline::after(std::chrono::seconds{3})};
        auto probing = service.probe(context);
        probing.handle().resume();
        ASSERT_FALSE(probing.handle().done());
        ASSERT_EQ(service.pool().waiter_count(), 1U);
        *borrowed = cnetmod::mongodb::pooled_connection{};
        if (attempt != 0)
        {
            std::jthread late_cancellation{[&]()
                {
                    tokens[attempt - 1].cancel();
                }};
            late_cancellation.join();
            ASSERT_FALSE(tokens[attempt - 1].pending_.load());
        }
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while ((!probing.handle().done() || !replying.handle().done()) &&
            std::chrono::steady_clock::now() < limit)
            (void)io.poll();
        if (!probing.handle().done() || !replying.handle().done())
            std::terminate();
        ASSERT_TRUE(replying.handle().promise().result());
        auto report = probing.handle().promise().result();
        ASSERT_TRUE(report.status == cnetmod::application::service_health::up);
        ASSERT_FALSE(static_cast<bool>(report.error));
        ASSERT_FALSE(cancellation.pending_.load());
        ASSERT_EQ(service.pool().idle_count(), 1U);
        ASSERT_EQ(service.pool().checked_out_count(), 0U);
    }
}
} // namespace

TEST(mongodb_empty_pool_health_creates_and_reuses_verified_connection)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    cnetmod::mongodb::connection_pool_options options;
    options.connection.port = listener->local_endpoint()->port();
    options.minimum_size = 0;
    options.maximum_size = 1;
    cnetmod::mongodb::connection_pool pool{*io, options};
    auto serve = [&]() -> cnetmod::task<bool>
    {
        auto peer = co_await cnetmod::async_accept(*io, *listener);
        if (!peer || !(co_await reply_to_mongodb_ping(*io, *peer, "hello")))
            co_return false;
        for (unsigned index = 0; index < 2; ++index)
            if (!(co_await reply_to_mongodb_ping(*io, *peer)))
                co_return false;
        co_return true;
    };
    auto serving = serve();
    serving.handle().resume();
    auto settle = [&](auto& operation)
    {
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (!operation.handle().done() && std::chrono::steady_clock::now() < limit)
            (void)io->poll();
        if (!operation.handle().done())
            std::terminate();
    };
    for (unsigned index = 0; index < 2; ++index)
    {
        cnetmod::cancel_token cancellation;
        auto checking = pool.health_check(cancellation);
        checking.handle().resume();
        settle(checking);
        ASSERT_TRUE(checking.handle().promise().result().has_value());
        ASSERT_FALSE(cancellation.pending_.load());
        ASSERT_EQ(pool.size(), 1U);
        ASSERT_EQ(pool.idle_count(), 1U);
        ASSERT_EQ(pool.connecting_count(), 0U);
        ASSERT_EQ(pool.checked_out_count(), 0U);
    }
    settle(serving);
    ASSERT_TRUE(serving.handle().promise().result());
    auto closing = pool.async_close();
    pool.close();
    ASSERT_EQ(pool.size(), 0U);
    closing.handle().resume();
    settle(closing);
    closing.handle().promise().result();
    ASSERT_EQ(pool.size(), 0U);
}
