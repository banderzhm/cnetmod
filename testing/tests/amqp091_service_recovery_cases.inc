namespace {

enum class replay_completion
{
    acknowledged,
    application_stop,
    deadline_expired,
    retry_after_timeout
};

enum class delivery_settlement
{
    none,
    ack,
    nack_drop,
    nack_requeue_multiple
};

void verify_amqp091_lifecycle_reconnect(cnetmod::application::service_requirement requirement, bool restore = true,
    replay_completion completion = replay_completion::acknowledged, delivery_settlement settlement = delivery_settlement::none,
    bool close_delivery_channel = false, bool close_in_callback = false)
{
    const bool acknowledged_consumer = settlement != delivery_settlement::none;
    const bool negative = settlement == delivery_settlement::nack_drop || settlement == delivery_settlement::nack_requeue_multiple;
    const auto settlement_flags = settlement == delivery_settlement::nack_requeue_multiple ? 3U : 0U;
    const bool stop_during_replay = completion == replay_completion::application_stop;
    const bool timeout_during_replay = completion == replay_completion::deadline_expired;
    const bool retry_after_timeout = completion == replay_completion::retry_after_timeout;
    namespace app = cnetmod::application;
    namespace amqp = cnetmod::amqp091;
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener);
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}));
    ASSERT_TRUE(listener->listen());
    amqp::connection_options options;
    options.endpoint.port = listener->local_endpoint()->port();
    auto service = std::make_shared<app::amqp091_service>(*io, options, "reconnect", requirement,
        app::recovery_policy{.initial_delay = std::chrono::milliseconds{1}, .maximum_delay = std::chrono::milliseconds{1}, .budget = timeout_during_replay ? std::chrono::milliseconds{200} : restore ? std::chrono::milliseconds{1000}
                                                                                                                                                                                                      : std::chrono::milliseconds{50},
            .jitter = 0});
    cnetmod::observability::telemetry_hub telemetry{*io, {.export_traces = false, .export_metrics = false, .export_logs = false}};
    app::task_supervisor supervisor{*io};
    unsigned escalations = 0;
    supervisor.on_recovery_exhausted([&](std::string_view, std::error_code)
        {
            ++escalations;
        });
    app::service_registry services;
    app::health_registry health;
    ASSERT_TRUE(services.manage(service));
    ASSERT_TRUE(health.add(service));
    services.freeze();
    app::lifecycle_policy lifecycle_policy;
    if (retry_after_timeout)
        lifecycle_policy.service_start_timeout = std::chrono::milliseconds{200};
    app::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health, lifecycle_policy};
    unsigned accepted = 0;
    unsigned declarations = 0;
    unsigned subscriptions = 0;
    unsigned deliveries = 0;
    unsigned acknowledgements = 0;
    bool channel_close_confirmed = false;
    std::optional<amqp::delivery_acknowledgement> acknowledgement;
    std::optional<amqp::delivery_acknowledgement> previous_acknowledgement;
    std::optional<cnetmod::task<amqp::result<void>>> deferred_ack;
    std::optional<cnetmod::task<amqp::result<void>>> callback_close;
    std::vector<std::byte> original_declaration;
    bool initial_topology_ready = false;
    bool replay_pending = false;
    bool release_replay = false;
    unsigned finished = 0;
    auto finish = [&]
    {
        if (++finished == 2)
            io->stop();
    };
    auto peer = [&]() -> cnetmod::task<void>
    {
        for (unsigned session = 0; session != (retry_after_timeout ? 3U : restore ? 2U
                                                                                  : 1U);
            ++session)
        {
            auto connection = co_await cnetmod::async_accept(*io, *listener);
            ASSERT_TRUE(connection);
            ++accepted;
            if (session == 2)
                ASSERT_TRUE(supervisor.last_error("service-recovery:amqp091:reconnect") == std::errc::timed_out);
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
            ASSERT_TRUE(co_await cnetmod::async_write_all(*io, *connection, cnetmod::const_buffer{wire.data(), wire.size()}));
            std::array<std::byte, 4096> bytes{};
            std::size_t protocol_bytes = 8;
            amqp::frame_parser parser;
            bool disconnect = false;
            while (!disconnect)
            {
                auto read = co_await cnetmod::async_read(*io, *connection, cnetmod::mutable_buffer{bytes.data(), bytes.size()});
                if (!read || *read == 0)
                    break;
                const auto skip = std::min(protocol_bytes, *read);
                protocol_bytes -= skip;
                auto frames = parser.feed(std::span<const std::byte>{bytes.data() + skip, *read - skip});
                ASSERT_TRUE(frames);
                for (const auto& frame : *frames)
                {
                    auto method = amqp::decode_method(frame);
                    ASSERT_TRUE(method);
                    if (method->class_id == 60 && (method->method_id == 80 || method->method_id == 120))
                    {
                        ++acknowledgements;
                        ASSERT_EQ(method->method_id, negative ? 120U : 80U);
                        ASSERT_EQ(method->channel, 1U);
                        ASSERT_TRUE(method->arguments == std::vector<std::byte>({std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{7}, static_cast<std::byte>(settlement_flags)}));
                        if (close_delivery_channel && acknowledgements == 1)
                        {
                            auto reply = amqp::encode_method({.channel = method->channel, .class_id = 20, .method_id = 40, .arguments = {std::byte{1}, std::byte{150}, std::byte{0}, std::byte{0}, std::byte{60}, std::byte{0}, std::byte{80}}});
                            auto encoded = amqp::encode_frame(*reply);
                            ASSERT_TRUE(co_await cnetmod::async_write_all(*io, *connection,
                                cnetmod::const_buffer{encoded->data(), encoded->size()}));
                        }
                    }
                    if (method->class_id == 20 && method->method_id == 41)
                        channel_close_confirmed = true;
                    if (method->class_id == 20 && method->method_id == 10)
                    {
                        auto reply = amqp::encode_method({.channel = method->channel, .class_id = 20, .method_id = 11, .arguments = {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}}});
                        auto encoded = amqp::encode_frame(*reply);
                        ASSERT_TRUE(co_await cnetmod::async_write_all(*io, *connection,
                            cnetmod::const_buffer{encoded->data(), encoded->size()}));
                    }
                    if (method->class_id == 40 && method->method_id == 10)
                    {
                        ++declarations;
                        if (session == 0)
                            original_declaration = method->arguments;
                        else
                            ASSERT_TRUE(method->arguments == original_declaration);
                        if (session != 0)
                        {
                            replay_pending = true;
                            const auto limit = cnetmod::deadline::after(std::chrono::seconds{2});
                            while (!release_replay && !limit.expired())
                                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
                            ASSERT_TRUE(release_replay);
                            if (stop_during_replay || timeout_during_replay || (retry_after_timeout && session == 1))
                                continue;
                        }
                        auto reply = amqp::encode_method({.channel = method->channel, .class_id = 40, .method_id = 11});
                        auto encoded = amqp::encode_frame(*reply);
                        ASSERT_TRUE(co_await cnetmod::async_write_all(*io, *connection,
                            cnetmod::const_buffer{encoded->data(), encoded->size()}));
                    }
                    if ((method->class_id == 50 && (method->method_id == 10 || method->method_id == 20)) ||
                        (method->class_id == 60 && method->method_id == 20))
                    {
                        const bool queue = method->class_id == 50 && method->method_id == 10;
                        const bool consumer = method->class_id == 60;
                        ASSERT_TRUE(method->arguments.size() >= 3);
                        ASSERT_EQ(std::to_integer<unsigned>(method->arguments[2]), queue ? 0U : 1U);
                        if (!queue)
                            ASSERT_TRUE(method->arguments[3] == static_cast<std::byte>('a' + session));
                        amqp::method_frame response{.channel = method->channel, .class_id = method->class_id, .method_id = static_cast<std::uint16_t>(method->method_id + 1)};
                        if (queue)
                            response.arguments = {std::byte{1}, static_cast<std::byte>('a' + session),
                                std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                                std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
                        if (consumer)
                        {
                            ++subscriptions;
                            response.arguments = {std::byte{1}, std::byte{'c'}};
                        }
                        auto reply = amqp::encode_method(response);
                        auto encoded = amqp::encode_frame(*reply);
                        ASSERT_TRUE(co_await cnetmod::async_write_all(*io, *connection,
                            cnetmod::const_buffer{encoded->data(), encoded->size()}));
                        if (consumer && (session != 0 || acknowledged_consumer))
                        {
                            amqp::method_frame delivery{.channel = method->channel, .class_id = 60, .method_id = 60, .arguments = {std::byte{1}, std::byte{'c'}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{7}, std::byte{1}, std::byte{0}, std::byte{0}}};
                            auto delivered = amqp::encode_method(delivery);
                            auto header = amqp::encode_content_header({.channel = method->channel, .body_size = 2});
                            std::vector<std::byte> payload;
                            for (const auto& part : {*delivered, *header,
                                     amqp::frame{.type = amqp::frame_type::body, .channel = method->channel, .payload = {std::byte{'o'}, std::byte{'k'}}}})
                            {
                                auto bytes = amqp::encode_frame(part);
                                payload.insert(payload.end(), bytes->begin(), bytes->end());
                            }
                            ASSERT_TRUE(co_await cnetmod::async_write_all(*io, *connection,
                                cnetmod::const_buffer{payload.data(), payload.size()}));
                        }
                        if (consumer && session == 0)
                        {
                            const auto limit = cnetmod::deadline::after(std::chrono::seconds{2});
                            while (!initial_topology_ready && !limit.expired())
                                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
                            ASSERT_TRUE(initial_topology_ready);
                            disconnect = true;
                        }
                    }
                }
            }
            if (!restore)
                listener->close();
            connection->shutdown_both();
        }
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE(co_await lifecycle.start());
        auto channel = co_await service->client().async_open_channel();
        ASSERT_TRUE(channel);
        ASSERT_TRUE(co_await (*channel)->async_declare_exchange({.name = "application-recovery"}));
        auto queue = co_await (*channel)->async_declare_queue({});
        ASSERT_TRUE(queue);
        ASSERT_TRUE(co_await (*channel)->async_bind_queue({.queue = queue->name, .exchange = "application-recovery", .routing_key = "created"}));
        auto on_delivery = [&](const amqp::delivery& delivery)
        {
            ++deliveries;
            ASSERT_EQ(delivery.consumer_tag, std::string{"c"});
            ASSERT_EQ(delivery.delivery_tag, 7U);
            ASSERT_TRUE(delivery.redelivered);
            ASSERT_TRUE(delivery.message.body == std::vector<std::byte>({std::byte{'o'}, std::byte{'k'}}));
        };
        if (acknowledged_consumer)
            ASSERT_TRUE(co_await (*channel)->async_consume_acknowledged({.queue = queue->name, .consumer_tag = "c"},
                [&](const amqp::delivery& delivery, amqp::delivery_acknowledgement receipt)
                {
                    if (deliveries == 0)
                    {
                        previous_acknowledgement = receipt;
                        deferred_ack.emplace(receipt.ack());
                        if (close_in_callback)
                        {
                            callback_close.emplace((*channel)->async_close());
                            callback_close->handle().resume();
                        }
                    }
                    else
                        acknowledgement = std::move(receipt);
                    on_delivery(delivery);
                }));
        else
            ASSERT_TRUE(co_await (*channel)->async_consume({.queue = queue->name, .consumer_tag = "c"}, on_delivery));
        const auto limit = cnetmod::deadline::after(std::chrono::seconds{2});
        if (acknowledged_consumer)
        {
            while (deliveries == 0 && !limit.expired())
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
            ASSERT_TRUE(previous_acknowledgement.has_value());
        }
        initial_topology_ready = true;
        while (supervisor.state("amqp091-pump:reconnect") != app::supervised_task_state::failed && !limit.expired())
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
        ASSERT_TRUE(supervisor.state("amqp091-pump:reconnect") == app::supervised_task_state::failed);
        if (close_in_callback)
        {
            ASSERT_TRUE(callback_close.has_value());
            ASSERT_TRUE(callback_close->handle().done());
            if (callback_close->handle().done())
                ASSERT_FALSE(callback_close->handle().promise().result());
        }
        ASSERT_EQ(escalations, 0U);
        ASSERT_EQ(accepted, 1U);
        health.mark_running();
        for (unsigned check = 0; check != 3; ++check)
            health.update(service->key(), {.status = app::service_health::down});
        ASSERT_FALSE(health.ready());
        ASSERT_TRUE(lifecycle.reconcile_health(health.snapshots().front()));
        if (!restore)
        {
            while (supervisor.state("service-recovery:amqp091:reconnect") != app::supervised_task_state::failed && !limit.expired())
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
            ASSERT_TRUE(supervisor.state("service-recovery:amqp091:reconnect") == app::supervised_task_state::failed);
            ASSERT_TRUE(supervisor.last_error("service-recovery:amqp091:reconnect"));
            const bool required = requirement == app::service_requirement::required;
            ASSERT_EQ(escalations, required ? 1U : 0U);
            ASSERT_EQ(accepted, 1U);
            ASSERT_TRUE(health.live());
            ASSERT_FALSE(health.ready());
            ASSERT_TRUE(service->client().state() == amqp::connection_state::disconnected);
            if (required)
            {
                ASSERT_TRUE(lifecycle.reconcile_health(health.snapshots().front()));
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{5});
                ASSERT_EQ(escalations, 1U);
            }
            auto stopped = co_await lifecycle.stop();
            ASSERT_EQ(stopped.has_value(), !required);
            ASSERT_TRUE(lifecycle.started_services().empty());
            ASSERT_FALSE(service->shutdown_required());
            finish();
            co_return;
        }
        while (!replay_pending && !limit.expired())
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
        ASSERT_TRUE(replay_pending);
        ASSERT_EQ(declarations, 2U);
        cnetmod::cancel_token probe_cancellation;
        app::service_context context{*io, telemetry, supervisor, probe_cancellation, limit};
        ASSERT_TRUE(service->client().state() == amqp::connection_state::open);
        ASSERT_TRUE((co_await service->probe(context)).status == app::service_health::down);
        ASSERT_TRUE(supervisor.state("service-recovery:amqp091:reconnect") == app::supervised_task_state::running);
        ASSERT_FALSE(health.ready());
        release_replay = true;
        if (timeout_during_replay)
        {
            while (supervisor.state("service-recovery:amqp091:reconnect") != app::supervised_task_state::failed && !limit.expired())
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
            ASSERT_TRUE(supervisor.state("service-recovery:amqp091:reconnect") == app::supervised_task_state::failed);
            ASSERT_TRUE(supervisor.last_error("service-recovery:amqp091:reconnect") == std::errc::timed_out);
            ASSERT_TRUE(supervisor.state("amqp091-pump:reconnect") == app::supervised_task_state::failed);
            ASSERT_TRUE(service->client().state() == amqp::connection_state::disconnected);
            ASSERT_TRUE((co_await service->probe(context)).status == app::service_health::down);
            ASSERT_FALSE(health.ready());
            const bool required = requirement == app::service_requirement::required;
            ASSERT_EQ(escalations, required ? 1U : 0U);
            auto stopped = co_await lifecycle.stop();
            ASSERT_EQ(stopped.has_value(), !required);
            auto joined = co_await supervisor.join();
            ASSERT_EQ(joined.has_value(), !required);
            ASSERT_TRUE(lifecycle.started_services().empty());
            ASSERT_FALSE(service->shutdown_required());
            ASSERT_EQ(accepted, 2U);
            finish();
            co_return;
        }
        if (stop_during_replay)
        {
            ASSERT_TRUE(co_await lifecycle.stop());
            ASSERT_TRUE(co_await supervisor.join());
            ASSERT_TRUE(supervisor.state("amqp091-pump:reconnect") == app::supervised_task_state::stopped);
            ASSERT_TRUE(supervisor.state("service-recovery:amqp091:reconnect") == app::supervised_task_state::stopped);
            ASSERT_TRUE(service->client().state() == amqp::connection_state::disconnected);
            ASSERT_TRUE((co_await service->probe(context)).status == app::service_health::down);
            ASSERT_TRUE(lifecycle.started_services().empty());
            ASSERT_FALSE(service->shutdown_required());
            ASSERT_EQ(escalations, 0U);
            ASSERT_EQ(accepted, 2U);
            finish();
            co_return;
        }
        while ((service->client().state() != amqp::connection_state::open ||
                   supervisor.state("service-recovery:amqp091:reconnect") != app::supervised_task_state::stopped) &&
            !limit.expired())
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
        ASSERT_EQ(accepted, retry_after_timeout ? 3U : 2U);
        ASSERT_EQ(declarations, retry_after_timeout ? 3U : 2U);
        while (deliveries < (acknowledged_consumer ? 2U : 1U) && !limit.expired())
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
        ASSERT_EQ(subscriptions, 2U);
        ASSERT_EQ(deliveries, acknowledged_consumer ? 2U : 1U);
        if (acknowledged_consumer)
        {
            ASSERT_TRUE(acknowledgement.has_value());
            ASSERT_TRUE(deferred_ack.has_value());
            auto stale_deferred_ack = co_await std::move(*deferred_ack);
            auto stale_nack = co_await previous_acknowledgement->nack();
            ASSERT_FALSE(stale_deferred_ack.has_value());
            ASSERT_FALSE(stale_nack.has_value());
            if (!stale_deferred_ack)
                ASSERT_TRUE(stale_deferred_ack.error().code == amqp::error_code::channel_closed);
            if (!stale_nack)
                ASSERT_TRUE(stale_nack.error().code == amqp::error_code::channel_closed);
            if (negative)
                ASSERT_TRUE(co_await acknowledgement->nack(settlement_flags != 0, settlement_flags != 0));
            else
                ASSERT_TRUE(co_await acknowledgement->ack());
            while (acknowledgements == 0 && !limit.expired())
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
            ASSERT_EQ(acknowledgements, 1U);
            if (close_delivery_channel)
            {
                while (!channel_close_confirmed && !limit.expired())
                    (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
                ASSERT_TRUE(channel_close_confirmed);
                ASSERT_TRUE(service->client().state() == amqp::connection_state::open);
                auto closed_ack = co_await acknowledgement->ack();
                auto closed_nack = co_await acknowledgement->nack(false);
                ASSERT_FALSE(closed_ack.has_value());
                ASSERT_FALSE(closed_nack.has_value());
                if (!closed_ack)
                    ASSERT_TRUE(closed_ack.error().code == amqp::error_code::channel_closed);
                if (!closed_nack)
                    ASSERT_TRUE(closed_nack.error().code == amqp::error_code::channel_closed);
            }
        }
        auto stale_ack = co_await (*channel)->async_ack(7);
        ASSERT_FALSE(stale_ack.has_value());
        if (!stale_ack)
            ASSERT_TRUE(stale_ack.error().code == amqp::error_code::channel_closed);
        ASSERT_TRUE(service->client().state() == amqp::connection_state::open);
        ASSERT_TRUE(supervisor.state("amqp091-pump:reconnect") == app::supervised_task_state::running);
        ASSERT_EQ(escalations, 0U);
        ASSERT_FALSE(health.ready());
        co_await health.refresh(context);
        ASSERT_FALSE(health.ready());
        co_await health.refresh(context);
        ASSERT_TRUE(health.ready());
        ASSERT_TRUE(co_await lifecycle.stop());
        if (acknowledgement)
        {
            auto late_ack = co_await acknowledgement->ack();
            auto late_nack = co_await acknowledgement->nack();
            ASSERT_FALSE(late_ack.has_value());
            ASSERT_FALSE(late_nack.has_value());
            if (!late_ack)
                ASSERT_TRUE(late_ack.error().code == amqp::error_code::channel_closed);
            if (!late_nack)
                ASSERT_TRUE(late_nack.error().code == amqp::error_code::channel_closed);
        }
        ASSERT_TRUE(lifecycle.started_services().empty());
        finish();
    };
    auto server = peer();
    auto operation = exercise();
    server.handle().resume();
    operation.handle().resume();
    io->run();
    server.handle().promise().result();
    operation.handle().promise().result();
    ASSERT_EQ(acknowledgements, acknowledged_consumer ? 1U : 0U);
}

} // namespace

TEST(application_amqp091_restored_consumer_ack_uses_new_session)
{
    verify_amqp091_lifecycle_reconnect(cnetmod::application::service_requirement::required, true,
        replay_completion::acknowledged, delivery_settlement::ack);
}

TEST(application_amqp091_delivery_remains_valid_during_callback_close)
{
    verify_amqp091_lifecycle_reconnect(cnetmod::application::service_requirement::required, true,
        replay_completion::acknowledged, delivery_settlement::ack, false, true);
}

TEST(application_amqp091_consumer_receipt_rejects_closed_channel)
{
    verify_amqp091_lifecycle_reconnect(cnetmod::application::service_requirement::required, true,
        replay_completion::acknowledged, delivery_settlement::ack, true);
}

TEST(application_amqp091_consumer_ack_after_replay_timeout)
{
    verify_amqp091_lifecycle_reconnect(cnetmod::application::service_requirement::required, true,
        replay_completion::retry_after_timeout, delivery_settlement::ack);
}

TEST(application_amqp091_restored_consumer_nack_drops_delivery)
{
    verify_amqp091_lifecycle_reconnect(cnetmod::application::service_requirement::required, true,
        replay_completion::acknowledged, delivery_settlement::nack_drop);
}

TEST(application_amqp091_restored_consumer_nack_requeues_range)
{
    verify_amqp091_lifecycle_reconnect(cnetmod::application::service_requirement::optional, true,
        replay_completion::retry_after_timeout, delivery_settlement::nack_requeue_multiple);
}

TEST(application_amqp091_lifecycle_owns_required_reconnect)
{
    verify_amqp091_lifecycle_reconnect(cnetmod::application::service_requirement::required);
}

TEST(application_amqp091_lifecycle_owns_optional_reconnect)
{
    verify_amqp091_lifecycle_reconnect(cnetmod::application::service_requirement::optional);
}

TEST(application_amqp091_required_recovery_budget_exhausts_once)
{
    verify_amqp091_lifecycle_reconnect(cnetmod::application::service_requirement::required, false);
}

TEST(application_amqp091_optional_recovery_budget_keeps_application_live)
{
    verify_amqp091_lifecycle_reconnect(cnetmod::application::service_requirement::optional, false);
}

TEST(application_amqp091_required_stop_joins_pending_topology_replay)
{
    verify_amqp091_lifecycle_reconnect(cnetmod::application::service_requirement::required, true, replay_completion::application_stop);
}

TEST(application_amqp091_optional_stop_joins_pending_topology_replay)
{
    verify_amqp091_lifecycle_reconnect(cnetmod::application::service_requirement::optional, true, replay_completion::application_stop);
}

TEST(application_amqp091_required_replay_deadline_closes_session)
{
    verify_amqp091_lifecycle_reconnect(cnetmod::application::service_requirement::required, true, replay_completion::deadline_expired);
}

TEST(application_amqp091_optional_replay_deadline_closes_session)
{
    verify_amqp091_lifecycle_reconnect(cnetmod::application::service_requirement::optional, true, replay_completion::deadline_expired);
}

TEST(application_amqp091_required_retries_topology_after_stage_timeout)
{
    verify_amqp091_lifecycle_reconnect(cnetmod::application::service_requirement::required, true, replay_completion::retry_after_timeout);
}

TEST(application_amqp091_optional_retries_topology_after_stage_timeout)
{
    verify_amqp091_lifecycle_reconnect(cnetmod::application::service_requirement::optional, true, replay_completion::retry_after_timeout);
}
