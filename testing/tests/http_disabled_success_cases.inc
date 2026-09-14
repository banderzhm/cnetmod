TEST(disabled_http_inflight_cancellation_matches_original)
{
    cnetmod::net_init network;
    auto peer_io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    std::atomic<unsigned> requests_seen{};
    bool clean_headers = true;
    auto peer = [&]() -> cnetmod::task<void>
    {
        for (unsigned index = 0; index < 4; ++index)
        {
            auto connection = co_await cnetmod::async_accept(*peer_io, *listener);
            ASSERT_TRUE(connection.has_value());
            std::string received;
            std::array<std::byte, 4096> buffer{};
            while (!received.contains("\r\n\r\n"))
            {
                auto count = co_await cnetmod::async_read(*peer_io, *connection,
                    cnetmod::mutable_buffer{buffer.data(), buffer.size()});
                if (!count || *count == 0)
                    std::terminate();
                received.append(reinterpret_cast<const char*>(buffer.data()), *count);
            }
            std::ranges::transform(received, received.begin(), [](unsigned char ch)
                {
                    return static_cast<char>(std::tolower(ch));
                });
            clean_headers = clean_headers && !received.contains("traceparent:") && !received.contains("tracestate:");
            requests_seen.store(index + 1, std::memory_order_release);
            // Withhold the response until cancellation closes the client transport.
            auto ended = co_await cnetmod::async_read(*peer_io, *connection,
                cnetmod::mutable_buffer{buffer.data(), buffer.size()});
            ASSERT_TRUE(!ended || *ended == 0);
        }
        peer_io->stop();
    };
    auto peer_task = peer();
    peer_io->post(peer_task.handle());
    std::jthread peer_thread{[&]
        {
            peer_io->run();
        }};
    auto io = cnetmod::make_io_context();
    cnetmod::http::request request{cnetmod::http::http_method::GET,
        std::format("http://127.0.0.1:{}/cancel", listener->local_endpoint()->port())};
    auto parent = cnetmod::http::tracing::new_root_context();
    parent.tracestate = "vendor=" + std::string(4096, 'x');
    std::array<allocation_totals, 2> totals{};
    std::array<std::error_code, 2> errors{};
    for (unsigned index = 0; index < 4; ++index)
    {
        cnetmod::http::client raw{*io};
        cnetmod::observability::instrumented_http_client disabled{raw, {}, {}};
        cnetmod::cancel_token cancellation;
        allocation_totals warmup;
        {
            allocation_window window{index < 2 ? warmup : totals[index - 2]};
            auto operation = index % 2 ? disabled.send(request, parent, cancellation) : raw.send(request, cancellation);
            operation.handle().resume();
            const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{3};
            while (requests_seen.load(std::memory_order_acquire) <= index && std::chrono::steady_clock::now() < limit)
                (void)io->poll();
            if (requests_seen.load(std::memory_order_acquire) <= index || operation.handle().done())
                std::terminate();
            cancellation.cancel();
            while (!operation.handle().done() && std::chrono::steady_clock::now() < limit)
                (void)io->poll();
            if (!operation.handle().done())
                std::terminate();
            auto result = operation.handle().promise().result();
            ASSERT_FALSE(result.has_value());
            if (index >= 2)
                errors[index - 2] = result.error();
        }
        raw.close();
    }
    peer_thread.join();
    ASSERT_TRUE(peer_task.handle().done());
    peer_task.handle().promise().result();
    ASSERT_TRUE(clean_headers);
    ASSERT_TRUE(static_cast<bool>(errors[0]));
    ASSERT_EQ(errors[0], errors[1]);
    ASSERT_TRUE(totals[0].calls > 0);
    ASSERT_EQ(totals[0].calls, totals[1].calls);
    ASSERT_EQ(totals[0].bytes, totals[1].bytes);
}

TEST(disabled_http_success_matches_original_allocations_and_wire_headers)
{
    cnetmod::net_init network;
    auto peer_io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    const auto port = listener->local_endpoint()->port();
    bool clean_headers = true;
    auto peer = [&]() -> cnetmod::task<void>
    {
        try
        {
            auto connection = co_await cnetmod::async_accept(*peer_io, *listener);
            ASSERT_TRUE(connection.has_value());
            for (unsigned index = 0; index != 8; ++index)
            {
                std::string request;
                std::array<std::byte, 4096> buffer{};
                while (!request.contains("\r\n\r\n"))
                {
                    auto received = co_await cnetmod::async_read(*peer_io, *connection,
                        cnetmod::mutable_buffer{buffer.data(), buffer.size()});
                    ASSERT_TRUE(received.has_value());
                    ASSERT_TRUE(*received > 0);
                    request.append(reinterpret_cast<const char*>(buffer.data()), *received);
                }
                std::ranges::transform(request, request.begin(), [](unsigned char ch)
                    {
                        return static_cast<char>(std::tolower(ch));
                    });
                clean_headers = clean_headers && !request.contains("traceparent:") && !request.contains("tracestate:");
                constexpr std::string_view response = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: keep-alive\r\n\r\nhello";
                ASSERT_TRUE((co_await cnetmod::async_write_all(*peer_io, *connection,
                                 cnetmod::const_buffer{response.data(), response.size()}))
                        .has_value());
            }
        }
        catch (...)
        {
            peer_io->stop();
            throw;
        }
        peer_io->stop();
    };
    auto peer_task = peer();
    peer_io->post(peer_task.handle());
    std::jthread peer_thread{[&]
        {
            peer_io->run();
        }};
    auto io = cnetmod::make_io_context();
    cnetmod::http::client raw{*io};
    cnetmod::observability::instrumented_http_client disabled{raw, {}, {}};
    cnetmod::http::request request{cnetmod::http::http_method::GET,
        std::format("http://127.0.0.1:{}/allocation", port)};
    auto parent = cnetmod::http::tracing::new_root_context();
    parent.tracestate = "vendor=" + std::string(4096, 'x');
    std::array<allocation_totals, 4> totals{};
    bool valid_responses = true;
    auto exercise = [&]() -> cnetmod::task<void>
    {
        try
        {
            for (unsigned cancellable = 0; cancellable != 2; ++cancellable)
            {
                cnetmod::cancel_token token;
                for (unsigned iteration = 0; iteration != 4; ++iteration)
                {
                    const bool observed = iteration % 2 != 0;
                    allocation_totals warmup;
                    auto& sample = iteration < 2 ? warmup : totals[cancellable * 2 + iteration - 2];
                    allocation_window window{sample};
                    auto response = observed
                        ? (cancellable ? co_await disabled.send(request, parent, token) : co_await disabled.send(request, parent))
                        : (cancellable ? co_await raw.send(request, token) : co_await raw.send(request));
                    valid_responses = valid_responses && response && response->status_code() == 200 && response->body() == "hello";
                }
            }
        }
        catch (...)
        {
            io->stop();
            throw;
        }
        io->stop();
    };
    auto work = exercise();
    io->post(work.handle());
    io->run();
    raw.close();
    peer_thread.join();
    ASSERT_TRUE(work.handle().done());
    ASSERT_TRUE(peer_task.handle().done());
    work.handle().promise().result();
    peer_task.handle().promise().result();
    ASSERT_TRUE(valid_responses);
    ASSERT_TRUE(clean_headers);
    for (unsigned index : {0U, 2U})
    {
        ASSERT_TRUE(totals[index].calls > 0);
        ASSERT_EQ(totals[index].calls, totals[index + 1].calls);
        ASSERT_EQ(totals[index].bytes, totals[index + 1].bytes);
    }
}
