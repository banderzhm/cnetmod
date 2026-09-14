enum class amqp091_dispatch_scope
{
    independent_recovery,
    service_start,
    lifecycle_start,
    owned_session,
    subscription,
    subscription_consume_ok
};

static void verify_amqp091_dispatch_allocation(amqp091_dispatch_scope scope)
{
    const bool lifecycle_owned = scope == amqp091_dispatch_scope::lifecycle_start;
    const bool application = lifecycle_owned || scope == amqp091_dispatch_scope::service_start;
    const bool owned_session = scope == amqp091_dispatch_scope::owned_session;
    const bool subscription = scope == amqp091_dispatch_scope::subscription ||
        scope == amqp091_dispatch_scope::subscription_consume_ok;
    const bool subscription_consume_ok = scope == amqp091_dispatch_scope::subscription_consume_ok;
    namespace amqp = cnetmod::amqp091;
    cnetmod::net_init network;
    unsigned injected = 0;
    for (std::size_t position = 0; position != (subscription ? 128U : owned_session ? 32U
                                                       : application                ? 16U
                                                                                    : 7U);
        ++position)
    {
        auto io = cnetmod::make_io_context();
        auto peer_io = cnetmod::make_io_context();
        auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
        ASSERT_TRUE(listener);
        ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}));
        ASSERT_TRUE(listener->listen());
        amqp::connection_options options;
        options.endpoint.port = listener->local_endpoint()->port();
        namespace app = cnetmod::application;
        auto owned_service = std::make_shared<app::amqp091_service>(*io, options, "allocation",
            app::service_requirement::required, app::recovery_policy{});
        auto& service = *owned_service;
        auto& client = service.client();
        cnetmod::observability::telemetry_hub telemetry{*io, {.export_traces = false, .export_metrics = false, .export_logs = false}};
        app::task_supervisor supervisor{*io};
        app::service_registry services;
        app::health_registry health;
        ASSERT_TRUE(services.manage(owned_service));
        ASSERT_TRUE(health.add(owned_service));
        services.freeze();
        app::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health, {}};

        struct observer final : amqp::connection_observer
        {
            std::size_t position;
            unsigned opened = 0;
            bool armed = false;

            void on_state_changed(amqp::connection_state state) override
            {
                if (state == amqp::connection_state::open && ++opened == 2)
                {
                    armed = true;
                    fail_after = position;
                }
            }

            void on_connection_error(const amqp::error&) override {}
        };

        auto watch = std::make_shared<observer>();
        watch->position = position;
        if ((!application && !owned_session && !subscription) || lifecycle_owned)
            client.connection()->observe(watch);
        auto finish = [&]
        {
            io->stop();
        };
        auto peer = [&]() -> cnetmod::task<void>
        {
            struct stop_on_exit
            {
                cnetmod::io_context& io;
                ~stop_on_exit()
                {
                    io.stop();
                }
            } completion{*peer_io};
            for (unsigned attempt = 0; attempt != 2; ++attempt)
            {
                auto connection = co_await cnetmod::async_accept(*peer_io, *listener);
                if (!connection)
                    throw std::system_error(connection.error());
                std::vector<std::byte> wire;
                auto append = [&](std::uint16_t method, std::initializer_list<unsigned char> arguments)
                {
                    amqp::method_frame reply{.channel = 0, .class_id = 10, .method_id = method};
                    for (auto byte : arguments)
                        reply.arguments.push_back(static_cast<std::byte>(byte));
                    auto frame = amqp::encode_method(reply);
                    auto bytes = amqp::encode_frame(*frame);
                    wire.insert(wire.end(), bytes->begin(), bytes->end());
                };
                append(10, {0, 9, 0, 0, 0, 0, 0, 0, 0, 5, 'P', 'L', 'A', 'I', 'N', 0, 0, 0, 5, 'e', 'n', '_', 'U', 'S'});
                append(30, {0, 0, 0, 0, 16, 0, 0, 0});
                append(41, {0});
                if (subscription && attempt == 1)
                {
                    auto opened = amqp::encode_method({.channel = 1, .class_id = 20, .method_id = 11, .arguments = std::vector<std::byte>(4)});
                    auto encoded = amqp::encode_frame(*opened);
                    wire.insert(wire.end(), encoded->begin(), encoded->end());
                }
                auto written = co_await cnetmod::async_write_all(*peer_io, *connection, cnetmod::const_buffer{wire.data(), wire.size()});
                if (!written)
                    throw std::system_error(written.error());
                std::array<std::byte, 4096> bytes{};
                std::size_t protocol_remaining = 8;
                amqp::frame_parser parser;
                for (;;)
                {
                    auto read = co_await cnetmod::async_read(*peer_io, *connection, cnetmod::mutable_buffer{bytes.data(), bytes.size()});
                    if (!read || *read == 0)
                        break;
                    if (!subscription || attempt != 1)
                        continue;
                    const auto skipped = std::min(protocol_remaining, *read);
                    protocol_remaining -= skipped;
                    auto frames = parser.feed(std::span<const std::byte>{bytes.data() + skipped, *read - skipped});
                    if (!frames)
                        throw std::runtime_error("invalid subscription test frame");
                    for (const auto& frame : *frames)
                    {
                        auto method = amqp::decode_method(frame);
                        if (!method || method->class_id != 60 || method->method_id != 20 || !subscription_consume_ok)
                            continue;
                        auto reply = amqp::encode_method({.channel = method->channel, .class_id = 60, .method_id = 21, .arguments = {std::byte{19}, std::byte{'a'}, std::byte{'l'}, std::byte{'l'}, std::byte{'o'}, std::byte{'c'}, std::byte{'a'}, std::byte{'t'}, std::byte{'i'}, std::byte{'o'}, std::byte{'n'}, std::byte{'-'}, std::byte{'c'}, std::byte{'o'}, std::byte{'n'}, std::byte{'s'}, std::byte{'u'}, std::byte{'m'}, std::byte{'e'}, std::byte{'r'}}});
                        if (!reply)
                            throw std::runtime_error("cannot encode ConsumeOk");
                        auto encoded = amqp::encode_frame(*reply);
                        if (!encoded || !(co_await cnetmod::async_write_all(*peer_io, *connection, cnetmod::const_buffer{encoded->data(), encoded->size()})))
                            throw std::runtime_error("cannot write ConsumeOk");
                    }
                }
            }
            peer_io->stop();
        };
        auto exercise = [&]() -> cnetmod::task<void>
        {
            ASSERT_TRUE(co_await client.async_connect(options));
            ASSERT_TRUE(co_await client.async_close());
            if (subscription)
            {
                ASSERT_TRUE(co_await client.async_connect(options));
                auto channel = co_await client.async_open_channel();
                ASSERT_TRUE(channel);
                auto owner = std::make_shared<int>(42);
                std::weak_ptr<int> lifetime = owner;
                amqp::acknowledged_delivery_handler callback = [owner = std::move(owner)](const amqp::delivery&, amqp::delivery_acknowledgement) {};
                amqp::result<std::string> result;
                bool threw = false;
                const auto failures_before = injected_allocation_failures;
                fail_after = position;
                try
                {
                    result = co_await (*channel)->async_consume_acknowledged(
                        {.queue = "allocation", .consumer_tag = "allocation-consumer", .no_wait = !subscription_consume_ok}, std::move(callback));
                }
                catch (const std::bad_alloc&)
                {
                    threw = true;
                }
                const auto failures = injected_allocation_failures - failures_before;
                fail_after.reset();
                ASSERT_TRUE(failures <= 1);
                if (failures != 0)
                {
                    ++injected;
                    ASSERT_TRUE(threw || !result);
                    if (!threw && !result)
                        ASSERT_TRUE(result.error().code == amqp::error_code::not_enough_memory);
                }
                else
                    ASSERT_TRUE(result);
                callback = {};
                ASSERT_TRUE(co_await client.async_close());
                ASSERT_TRUE(client.state() == amqp::connection_state::disconnected);
                if (failures != 0)
                    ASSERT_TRUE(lifetime.expired());
                finish();
                co_return;
            }
            if (application || owned_session)
            {
                if (!lifecycle_owned)
                    ASSERT_TRUE(co_await client.async_connect(options));
                cnetmod::cancel_token token;
                app::service_context context{*io, telemetry, supervisor, token,
                    cnetmod::deadline::after(std::chrono::seconds{1})};
                std::expected<void, std::error_code> started;
                bool threw = false;
                const auto failures_before = injected_allocation_failures;
                if (!lifecycle_owned)
                    fail_after = position;
                try
                {
                    if (owned_session)
                    {
                        unsigned ready = 0;
                        auto result = co_await client.connection()->async_run_session(token, [&]
                            {
                                ++ready;
                                token.cancel();
                            });
                        if (injected_allocation_failures != failures_before)
                        {
                            ASSERT_FALSE(result.has_value());
                            if (!result)
                            {
                                ASSERT_TRUE(result.error().code == amqp::error_code::not_enough_memory);
                                started = std::unexpected(std::make_error_code(std::errc::not_enough_memory));
                            }
                            ASSERT_TRUE(ready <= 1U);
                        }
                        else
                        {
                            ASSERT_EQ(ready, 1U);
                            ASSERT_TRUE(token.is_cancelled());
                        }
                    }
                    else if (lifecycle_owned)
                        started = co_await lifecycle.start();
                    else
                        started = co_await service.start(context);
                }
                catch (const std::bad_alloc&)
                {
                    threw = true;
                }
                const auto failures = injected_allocation_failures - failures_before;
                const bool failed = failures != 0;
                fail_after.reset();
                ASSERT_TRUE(failures <= 1);
                if (lifecycle_owned)
                    ASSERT_TRUE(watch->armed);
                if (failed)
                {
                    ++injected;
                    ASSERT_TRUE(threw || (!started && started.error() == std::errc::not_enough_memory));
                    if (lifecycle_owned)
                    {
                        ASSERT_FALSE(threw);
                        ASSERT_TRUE(lifecycle.started_services().empty());
                        ASSERT_FALSE(service.shutdown_required());
                        ASSERT_TRUE(client.state() == amqp::connection_state::disconnected);
                    }
                }
                if (lifecycle_owned && started)
                    ASSERT_TRUE(co_await lifecycle.stop());
                supervisor.request_stop();
                ASSERT_TRUE(co_await supervisor.join());
                ASSERT_TRUE(co_await service.stop(context));
                ASSERT_FALSE(service.shutdown_required());
                ASSERT_TRUE(client.state() == amqp::connection_state::disconnected);
                finish();
                co_return;
            }
            client.connection()->set_recovery_strategy(std::make_shared<amqp::automatic_recovery_strategy>(
                std::make_shared<amqp::exponential_backoff>(std::chrono::milliseconds{1}, std::chrono::milliseconds{1}), false));
            cnetmod::cancel_token token;
            bool threw = false;
            const auto failures_before = injected_allocation_failures;
            try
            {
                (void)co_await client.async_recover(token);
            }
            catch (const std::bad_alloc&)
            {
                threw = true;
            }
            const auto failures = injected_allocation_failures - failures_before;
            const bool failed = failures != 0;
            fail_after.reset();
            ASSERT_TRUE(watch->armed);
            ASSERT_TRUE(failures <= 1);
            if (failed)
            {
                ++injected;
                ASSERT_TRUE(threw);
                ASSERT_TRUE(client.state() == amqp::connection_state::disconnected);
            }
            try
            {
                (void)co_await client.async_close();
            }
            catch (const std::bad_alloc&)
            {
                ASSERT_TRUE(failed);
            }
            ASSERT_TRUE(client.state() == amqp::connection_state::disconnected);
            ASSERT_EQ(client.connection().use_count(), 2L);
            finish();
        };
        auto server = peer();
        auto operation = exercise();
        std::jthread peer_thread{[&]
            {
                server.handle().resume();
                peer_io->run();
            }};
        operation.handle().resume();
        io->run();
        peer_thread.join();
        server.handle().promise().result();
        operation.handle().promise().result();
    }
    if (subscription)
    {
        ASSERT_TRUE(injected >= 8);
        ASSERT_TRUE(injected < 128);
    }
    else if (owned_session)
        ASSERT_TRUE(injected >= 16);
    else if (application)
        ASSERT_EQ(injected, 16U);
    else
        ASSERT_TRUE(injected >= 5);
}

TEST(amqp091_recovery_dispatch_allocation_failure_retires_transport)
{
    verify_amqp091_dispatch_allocation(amqp091_dispatch_scope::independent_recovery);
}

TEST(application_amqp091_start_allocation_failure_can_be_cleaned_up)
{
    verify_amqp091_dispatch_allocation(amqp091_dispatch_scope::service_start);
}

TEST(application_amqp091_post_handshake_allocation_failure_rolls_back)
{
    verify_amqp091_dispatch_allocation(amqp091_dispatch_scope::lifecycle_start);
}

TEST(amqp091_owned_session_allocation_failure_joins_children)
{
    verify_amqp091_dispatch_allocation(amqp091_dispatch_scope::owned_session);
}

TEST(amqp091_subscription_allocation_failure_releases_callback)
{
    verify_amqp091_dispatch_allocation(amqp091_dispatch_scope::subscription);
}

TEST(amqp091_consume_ok_allocation_failure_retires_subscription)
{
    verify_amqp091_dispatch_allocation(amqp091_dispatch_scope::subscription_consume_ok);
}
