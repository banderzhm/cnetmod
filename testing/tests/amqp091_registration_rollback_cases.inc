namespace {

class shutdown_confirmation_observer final : public cnetmod::amqp091::publisher_confirm_observer
{
public:
    unsigned failures = 0;

    void on_confirm(const cnetmod::amqp091::publisher_confirmation&) override
    {
        ASSERT_TRUE(false);
    }

    void on_confirm_failure(const cnetmod::amqp091::error& reason) override
    {
        ASSERT_TRUE(reason.code == cnetmod::amqp091::error_code::connection_closed);
        ++failures;
    }
};

void verify_amqp091_lifecycle_transport_cleanup(bool run_pump, bool fail_publication = false, bool preconnected = false)
{
    namespace app = cnetmod::application;
    namespace amqp = cnetmod::amqp091;
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
        cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    if (!listener)
        return;
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!endpoint)
        return;
    amqp::connection_options options;
    options.endpoint.port = endpoint->port();
    options.credentials.username = "test";
    options.credentials.password = "test";
    auto service = std::make_shared<app::amqp091_service>(*io, options, "registration",
        app::service_requirement::required, app::recovery_policy{});
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    app::task_supervisor supervisor{*io};
    if (!run_pump)
        supervisor.request_stop();
    app::service_registry services;
    app::health_registry health;
    ASSERT_TRUE(services.manage(service));
    ASSERT_TRUE(health.add(service));
    services.freeze();
    app::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health, {}};
    unsigned finished = 0;
    bool channel_requested = false;
    bool publication_received = false;
    unsigned completed_messages = 0;
    bool awaiting_content_header = false;
    std::uint64_t remaining_body = 0;
    auto finish = [&]
    {
        if (++finished == 2)
            io->stop();
    };
    auto peer = [&]() -> cnetmod::task<void>
    {
        auto connection = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(connection.has_value());
        if (connection)
        {
            // Script handshake responses; this fixture tests lifecycle rollback,
            // not broker interoperability or authentication correctness.
            std::vector<std::byte> wire;
            auto append = [&](std::uint16_t method, std::initializer_list<unsigned char> arguments)
            {
                amqp::method_frame response{.channel = 0, .class_id = 10, .method_id = method};
                for (auto byte : arguments)
                    response.arguments.push_back(static_cast<std::byte>(byte));
                auto frame = amqp::encode_method(response);
                ASSERT_TRUE(frame.has_value());
                if (!frame)
                    return;
                auto encoded = amqp::encode_frame(*frame);
                ASSERT_TRUE(encoded.has_value());
                if (encoded)
                    wire.insert(wire.end(), encoded->begin(), encoded->end());
            };
            append(10, {0, 9, 0, 0, 0, 0, 0, 0, 0, 5, 'P', 'L', 'A', 'I', 'N', 0, 0, 0, 5, 'e', 'n', '_', 'U', 'S'});
            append(30, {0, 0, 0, 0, 16, 0, 0, 0});
            append(41, {0});
            ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *connection,
                             cnetmod::const_buffer{wire.data(), wire.size()}))
                    .has_value());
            cnetmod::cancel_token cancellation;
            const auto limit = cnetmod::deadline::after(std::chrono::seconds{2});
            std::array<std::byte, 4096> bytes{};
            std::size_t header_remaining = 8;
            amqp::frame_parser parser;
            for (;;)
            {
                auto read = co_await cnetmod::with_deadline(*io, limit,
                    cnetmod::async_read(*io, *connection,
                        cnetmod::mutable_buffer{bytes.data(), bytes.size()}, cancellation),
                    cancellation);
                if (!read)
                {
                    ASSERT_EQ(read.error(), cnetmod::make_error_code(cnetmod::errc::end_of_file));
                    break;
                }
                if (*read == 0)
                    break;
                const auto skipped = std::min(header_remaining, *read);
                header_remaining -= skipped;
                auto frames = parser.feed(std::span<const std::byte>{bytes.data() + skipped, *read - skipped});
                ASSERT_TRUE(frames.has_value());
                if (frames)
                {
                    for (const auto& frame : *frames)
                    {
                        if (frame.type == amqp::frame_type::method)
                        {
                            auto method = amqp::decode_method(frame);
                            ASSERT_TRUE(method.has_value());
                            if (method && method->class_id == 20 && method->method_id == 10)
                            {
                                if (method->channel == 1)
                                {
                                    auto reply = amqp::encode_method({.channel = 1, .class_id = 20, .method_id = 11, .arguments = std::vector<std::byte>(4)});
                                    ASSERT_TRUE(reply.has_value());
                                    auto encoded = amqp::encode_frame(*reply);
                                    ASSERT_TRUE(encoded.has_value());
                                    ASSERT_TRUE((co_await cnetmod::async_write_all(*io, *connection,
                                                     cnetmod::const_buffer{encoded->data(), encoded->size()}))
                                            .has_value());
                                }
                                else
                                    channel_requested = true;
                            }
                            if (method && method->class_id == 60 && method->method_id == 40)
                            {
                                ASSERT_FALSE(awaiting_content_header);
                                ASSERT_EQ(remaining_body, 0U);
                                awaiting_content_header = true;
                                publication_received = true;
                            }
                        }
                        else if (frame.type == amqp::frame_type::header)
                        {
                            if (fail_publication && completed_messages == 3)
                            {
                                connection->shutdown_both();
                                finish();
                                co_return;
                            }
                            ASSERT_TRUE(awaiting_content_header);
                            auto header = amqp::decode_content_header(frame);
                            ASSERT_TRUE(header.has_value());
                            if (header)
                                remaining_body = header->body_size;
                            awaiting_content_header = false;
                            if (remaining_body == 0)
                                ++completed_messages;
                        }
                        else if (frame.type == amqp::frame_type::body)
                        {
                            ASSERT_FALSE(awaiting_content_header);
                            ASSERT_LE(frame.payload.size(), remaining_body);
                            remaining_body -= std::min<std::uint64_t>(remaining_body, frame.payload.size());
                            if (remaining_body == 0)
                                ++completed_messages;
                        }
                    }
                }
            }
        }
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        if (preconnected)
        {
            ASSERT_TRUE(co_await service->client().async_connect(options));
            cnetmod::cancel_token cancellation;
            app::service_context context{*io, telemetry, supervisor, cancellation,
                cnetmod::deadline::after(std::chrono::seconds{1})};
            ASSERT_TRUE((co_await service->probe(context)).status == app::service_health::down);
        }
        auto result = co_await lifecycle.start();
        ASSERT_EQ(result.has_value(), run_pump);
        if (!run_pump && !result)
            ASSERT_EQ(result.error(), std::make_error_code(std::errc::operation_canceled));
        if (run_pump)
        {
            ASSERT_TRUE((co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{10})).has_value());
            ASSERT_TRUE(supervisor.state("amqp091-pump:registration") == app::supervised_task_state::running);
            auto published_channel = co_await service->client().async_open_channel();
            ASSERT_TRUE(published_channel.has_value());
            auto confirmation = std::make_shared<shutdown_confirmation_observer>();
            (*published_channel)->observe_confirms(confirmation);
            ASSERT_TRUE(co_await (*published_channel)->async_enable_confirms(true));
            auto invalid = co_await (*published_channel)->async_publish({.routing_key = std::string(256, 'x')}, {});
            ASSERT_FALSE(invalid.has_value());
            if (!invalid)
                ASSERT_TRUE(invalid.error().code == amqp::error_code::invalid_field);
            amqp::message invalid_header;
            invalid_header.headers.emplace(std::string(256, 'k'), "value");
            auto rejected_header = co_await (*published_channel)->async_publish({}, std::move(invalid_header));
            ASSERT_FALSE(rejected_header.has_value());
            amqp::message oversized_header;
            oversized_header.headers.emplace("large", std::string(4096, 'v'));
            auto rejected_size = co_await (*published_channel)->async_publish({}, std::move(oversized_header));
            ASSERT_FALSE(rejected_size.has_value());
            if (!rejected_size)
                ASSERT_TRUE(rejected_size.error().code == amqp::error_code::frame_too_large);
            auto published = co_await (*published_channel)->async_publish({}, {});
            ASSERT_TRUE(published.has_value());
            if (published)
                ASSERT_EQ(*published, 1U);
            amqp::message first_message;
            first_message.body.resize(16000, std::byte{1});
            amqp::message second_message;
            second_message.body.resize(16000, std::byte{2});
            auto [first_publish, second_publish] = co_await cnetmod::when_all(
                (*published_channel)->async_publish({}, std::move(first_message)),
                (*published_channel)->async_publish({}, std::move(second_message)));
            ASSERT_TRUE(first_publish.has_value());
            ASSERT_TRUE(second_publish.has_value());
            if (fail_publication)
            {
                amqp::message interrupted;
                interrupted.body.resize(8 * 1024 * 1024, std::byte{3});
                auto [failed, queued_ack, queued_rpc] = co_await cnetmod::when_all(
                    (*published_channel)->async_publish({}, std::move(interrupted)),
                    (*published_channel)->async_ack(1),
                    (*published_channel)->async_set_qos({.prefetch_count = 1}));
                ASSERT_FALSE(failed.has_value());
                ASSERT_FALSE(queued_ack.has_value());
                ASSERT_FALSE(queued_rpc.has_value());
                ASSERT_FALSE((*published_channel)->is_open());
                auto subsequent = co_await (*published_channel)->async_publish({}, {});
                ASSERT_FALSE(subsequent.has_value());
                ASSERT_TRUE(co_await lifecycle.stop());
                ASSERT_EQ(confirmation->failures, 1U);
                published_channel->reset();
            }
            else
            {
                auto pending_channel = service->client().async_open_channel();
                pending_channel.handle().resume();
                ASSERT_FALSE(pending_channel.handle().done());
                const auto request_limit = cnetmod::deadline::after(std::chrono::milliseconds{500});
                while (!channel_requested && !request_limit.expired())
                    ASSERT_TRUE((co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1})).has_value());
                ASSERT_TRUE(channel_requested);
                ASSERT_TRUE(publication_received);
                ASSERT_EQ(completed_messages, 3U);
                ASSERT_FALSE(pending_channel.handle().done());
                auto overlapping = co_await service->client().async_connect(options);
                ASSERT_FALSE(overlapping);
                if (!overlapping)
                    ASSERT_TRUE(overlapping.error().code == amqp::error_code::command_invalid);
                ASSERT_FALSE(pending_channel.handle().done());
                ASSERT_TRUE(co_await lifecycle.stop());
                ASSERT_EQ(confirmation->failures, 1U);
                ASSERT_FALSE((*published_channel)->is_open());
                auto late_publish = co_await (*published_channel)->async_publish({}, {});
                ASSERT_FALSE(late_publish.has_value());
                if (!late_publish)
                    ASSERT_TRUE(late_publish.error().code == amqp::error_code::channel_closed);
                ASSERT_EQ(confirmation->failures, 1U);
                published_channel->reset();
                const auto settlement = cnetmod::deadline::after(std::chrono::milliseconds{500});
                while (!pending_channel.handle().done() && !settlement.expired())
                    ASSERT_TRUE((co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1})).has_value());
                ASSERT_TRUE(pending_channel.handle().done());
                if (pending_channel.handle().done())
                {
                    auto opened = pending_channel.handle().promise().result();
                    ASSERT_FALSE(opened.has_value());
                    if (!opened)
                        ASSERT_TRUE(opened.error().code == amqp::error_code::connection_closed);
                }
                auto connection = service->client().connection();
                // Only the client and this local observation may retain ownership.
                // A detached sleeping heartbeat used to hold a third reference.
                ASSERT_EQ(connection.use_count(), 2L);
            }
        }
        ASSERT_TRUE(lifecycle.started_services().empty());
        ASSERT_FALSE(service->shutdown_required());
        ASSERT_TRUE(service->client().state() == amqp::connection_state::disconnected);
        ASSERT_TRUE(co_await lifecycle.stop());
        finish();
    };
    auto server = peer();
    auto operation = exercise();
    server.handle().resume();
    operation.handle().resume();
    io->run();
    server.handle().promise().result();
    operation.handle().promise().result();
}

} // namespace

TEST(application_amqp091_registration_failure_rolls_back_connected_service)
{
    verify_amqp091_lifecycle_transport_cleanup(false);
}

TEST(application_amqp091_stop_joins_heartbeat_and_releases_transport)
{
    verify_amqp091_lifecycle_transport_cleanup(true);
}

TEST(application_amqp091_partial_publish_failure_retires_connection)
{
    verify_amqp091_lifecycle_transport_cleanup(true, true);
}

TEST(application_amqp091_preconnected_service_registers_frame_pump)
{
    verify_amqp091_lifecycle_transport_cleanup(true, false, true);
}

TEST(application_amqp091_preconnected_registration_failure_rolls_back)
{
    verify_amqp091_lifecycle_transport_cleanup(false, false, true);
}
