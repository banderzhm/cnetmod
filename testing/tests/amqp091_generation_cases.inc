namespace {

enum class topology_cancel_stage : unsigned
{
    channel = 20 * 256 + 10,
    exchange = 40 * 256 + 10,
    queue = 50 * 256 + 10,
    binding = 50 * 256 + 20,
    consumer = 60 * 256 + 20
};

enum class topology_failure_mode
{
    cancellation,
    broker_rejection
};

void verify_amqp091_reconnected_channel(bool managed_recovery, bool reader_failure = false, bool immediate_close = false, bool cancel_topology = false,
    topology_cancel_stage cancel_stage = topology_cancel_stage::channel, topology_failure_mode failure_mode = topology_failure_mode::cancellation,
    bool owned_session = false)
{
    namespace amqp = cnetmod::amqp091;
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener);
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}));
    ASSERT_TRUE(listener->listen());
    amqp::connection_options options;
    options.endpoint.port = listener->local_endpoint()->port();
    amqp::amqp091_client client{*io};
    cnetmod::cancel_token recovery_cancel;
    unsigned completed = 0;
    unsigned deliveries = 0;
    std::array<std::vector<unsigned>, 3> topology_requests;
    auto finish = [&]
    {
        if (++completed == 2)
            io->stop();
    };
    auto peer = [&]() -> cnetmod::task<void>
    {
        for (unsigned attempt = 0; attempt != (reader_failure || immediate_close || cancel_topology ? 3u : 2u); ++attempt)
        {
            auto connection = co_await cnetmod::async_accept(*io, *listener);
            ASSERT_TRUE(connection);
            std::vector<std::byte> wire;
            auto append = [&](std::uint16_t channel, std::uint16_t cls, std::uint16_t method, std::initializer_list<unsigned char> arguments)
            {
                amqp::method_frame reply{.channel = channel, .class_id = cls, .method_id = method};
                for (auto byte : arguments)
                    reply.arguments.push_back(static_cast<std::byte>(byte));
                auto frame = amqp::encode_method(reply);
                auto bytes = amqp::encode_frame(*frame);
                wire.insert(wire.end(), bytes->begin(), bytes->end());
            };
            append(0, 10, 10, {0, 9, 0, 0, 0, 0, 0, 0, 0, 5, 'P', 'L', 'A', 'I', 'N', 0, 0, 0, 5, 'e', 'n', '_', 'U', 'S'});
            append(0, 10, 30, {0, 1, 0, 0, 16, 0, 0, 0});
            append(0, 10, 41, {0});
            if (!cancel_topology || attempt == 0)
                append(1, 20, 11, {0, 0, 0, 0});
            ASSERT_TRUE(co_await cnetmod::async_write_all(*io, *connection, cnetmod::const_buffer{wire.data(), wire.size()}));
            std::array<std::byte, 4096> bytes{};
            std::size_t protocol_bytes = 8;
            amqp::frame_parser parser;
            for (;;)
            {
                auto read = co_await cnetmod::async_read(*io, *connection, cnetmod::mutable_buffer{bytes.data(), bytes.size()});
                if (!read || *read == 0)
                    break;
                auto skip = std::min(protocol_bytes, *read);
                protocol_bytes -= skip;
                auto frames = parser.feed(std::span<const std::byte>{bytes.data() + skip, *read - skip});
                ASSERT_TRUE(frames);
                for (const auto& frame : *frames)
                {
                    auto method = amqp::decode_method(frame);
                    ASSERT_TRUE(method);
                    // Stale publish/ack must never reach the new transport.
                    ASSERT_TRUE(method->class_id == 10 || method->class_id == 20 || (cancel_topology && (method->class_id == 40 || method->class_id == 50 || method->class_id == 60)));
                    if (cancel_topology && method->class_id != 10)
                    {
                        const auto key = unsigned{method->class_id} * 256 + method->method_id;
                        if (attempt == 1 && key == static_cast<unsigned>(topology_cancel_stage::channel))
                        {
                            auto competing = co_await client.async_open_channel();
                            ASSERT_FALSE(competing);
                            if (!competing)
                                ASSERT_TRUE(competing.error().code == amqp::error_code::command_invalid);
                        }
                        if (key == static_cast<unsigned>(topology_cancel_stage::channel))
                            ASSERT_EQ(method->channel, 1U);
                        if (method->class_id != 20)
                        {
                            topology_requests[attempt].push_back(key);
                            std::size_t offset = 2;
                            auto short_string = [&]() -> std::string
                            {
                                ASSERT_TRUE(offset < method->arguments.size());
                                if (offset >= method->arguments.size())
                                    return {};
                                const auto length = std::to_integer<std::size_t>(method->arguments[offset++]);
                                ASSERT_TRUE(length <= method->arguments.size() - offset);
                                if (length > method->arguments.size() - offset)
                                    return {};
                                std::string value(length, '\0');
                                for (auto& character : value)
                                    character = std::to_integer<char>(method->arguments[offset++]);
                                return value;
                            };
                            if (key == static_cast<unsigned>(topology_cancel_stage::exchange))
                            {
                                ASSERT_EQ(short_string(), std::string{"recovery-preserved-exchange"});
                                ASSERT_EQ(short_string(), std::string{"direct"});
                            }
                            else if (key == static_cast<unsigned>(topology_cancel_stage::queue))
                                ASSERT_TRUE(short_string().empty());
                            else
                            {
                                ASSERT_EQ(short_string(), std::string(1, static_cast<char>('a' + attempt)));
                                if (key == static_cast<unsigned>(topology_cancel_stage::binding))
                                {
                                    ASSERT_EQ(short_string(), std::string{"recovery-preserved-exchange"});
                                    ASSERT_EQ(short_string(), std::string{"orders.created"});
                                }
                                else
                                    ASSERT_EQ(short_string(), std::string{"c"});
                            }
                        }
                        if (attempt == 1 && key == static_cast<unsigned>(cancel_stage))
                        {
                            if (failure_mode == topology_failure_mode::cancellation)
                                recovery_cancel.cancel();
                            else
                            {
                                wire.clear();
                                append(method->channel, 20, 40, {1, 150, 3, 'b', 'a', 'd', 0, static_cast<unsigned char>(method->class_id), 0, static_cast<unsigned char>(method->method_id)});
                                ASSERT_TRUE(co_await cnetmod::async_write_all(*io, *connection, cnetmod::const_buffer{wire.data(), wire.size()}));
                            }
                            continue;
                        }
                        wire.clear();
                        if (method->class_id == 20 && method->method_id == 10 && attempt != 0)
                            append(method->channel, 20, 11, {0, 0, 0, 0});
                        else if (key == static_cast<unsigned>(topology_cancel_stage::exchange))
                            append(method->channel, 40, 11, {});
                        else if (key == static_cast<unsigned>(topology_cancel_stage::queue))
                            append(method->channel, 50, 11, {1, static_cast<unsigned char>('a' + attempt), 0, 0, 0, 0, 0, 0, 0, 0});
                        else if (key == static_cast<unsigned>(topology_cancel_stage::binding))
                            append(method->channel, 50, 21, {});
                        else if (key == static_cast<unsigned>(topology_cancel_stage::consumer))
                        {
                            append(method->channel, 60, 21, {1, 'c'});
                            if (attempt == 2)
                            {
                                amqp::method_frame delivery{.channel = method->channel, .class_id = 60, .method_id = 60, .arguments = {std::byte{1}, std::byte{'c'}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{7}, std::byte{1}}};
                                auto append_short = [&](std::string_view value)
                                {
                                    delivery.arguments.push_back(static_cast<std::byte>(value.size()));
                                    for (auto character : value)
                                        delivery.arguments.push_back(static_cast<std::byte>(character));
                                };
                                append_short("recovery-preserved-exchange");
                                append_short("orders.created");
                                auto append_frame = [&](const amqp::frame& frame)
                                {
                                    auto encoded = amqp::encode_frame(frame);
                                    ASSERT_TRUE(encoded);
                                    wire.insert(wire.end(), encoded->begin(), encoded->end());
                                };
                                auto delivered = amqp::encode_method(delivery);
                                ASSERT_TRUE(delivered);
                                append_frame(*delivered);
                                auto header = amqp::encode_content_header({.channel = method->channel, .body_size = 2, .properties = {.content_type = "text/plain"}});
                                ASSERT_TRUE(header);
                                append_frame(*header);
                                append_frame({.type = amqp::frame_type::body, .channel = method->channel, .payload = {std::byte{'o'}}});
                                append_frame({.type = amqp::frame_type::body, .channel = method->channel, .payload = {std::byte{'k'}}});
                            }
                        }
                        if (!wire.empty())
                            ASSERT_TRUE(co_await cnetmod::async_write_all(*io, *connection, cnetmod::const_buffer{wire.data(), wire.size()}));
                    }
                    if (reader_failure && attempt == 1 && method->class_id == 10 && method->method_id == 40)
                    {
                        wire.clear();
                        append(1, 60, 30, {});
                        ASSERT_TRUE(co_await cnetmod::async_write_all(*io, *connection, cnetmod::const_buffer{wire.data(), wire.size()}));
                    }
                }
            }
        }
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE(co_await client.async_connect(options));
        auto old = co_await client.async_open_channel();
        ASSERT_TRUE(old);
        ASSERT_TRUE((*old)->is_open());
        if (!managed_recovery)
        {
            for (unsigned request = 0; request != 65536; ++request)
            {
                auto rejected = co_await client.async_open_channel();
                ASSERT_FALSE(rejected);
                if (!rejected)
                    ASSERT_TRUE(rejected.error().code == amqp::error_code::invalid_channel);
            }
            ASSERT_TRUE((*old)->is_open());
        }
        if (cancel_topology)
        {
            ASSERT_TRUE(co_await (*old)->async_declare_exchange({.name = "recovery-preserved-exchange"}));
            auto queue = co_await (*old)->async_declare_queue({});
            ASSERT_TRUE(queue);
            ASSERT_EQ(queue->name, std::string{"a"});
            ASSERT_TRUE(co_await (*old)->async_bind_queue({.queue = queue->name, .exchange = "recovery-preserved-exchange", .routing_key = "orders.created"}));
            ASSERT_TRUE(co_await (*old)->async_consume({.queue = queue->name, .consumer_tag = "c"}, [&](const amqp::delivery& delivery)
                {
                    ++deliveries;
                    ASSERT_EQ(delivery.consumer_tag, std::string{"c"});
                    ASSERT_EQ(delivery.delivery_tag, 7U);
                    ASSERT_TRUE(delivery.redelivered);
                    ASSERT_EQ(delivery.exchange, std::string{"recovery-preserved-exchange"});
                    ASSERT_EQ(delivery.routing_key, std::string{"orders.created"});
                    ASSERT_EQ(delivery.message.content_type, std::string{"text/plain"});
                    const std::vector<std::byte> expected{std::byte{'o'}, std::byte{'k'}};
                    ASSERT_TRUE(delivery.message.body == expected);
                }));
        }
        auto overlapping = co_await client.async_connect(options);
        ASSERT_FALSE(overlapping);
        if (!overlapping)
            ASSERT_TRUE(overlapping.error().code == amqp::error_code::command_invalid);
        ASSERT_TRUE((*old)->is_open());
        auto overlapping_recovery = co_await client.async_recover(recovery_cancel);
        ASSERT_FALSE(overlapping_recovery);
        if (!overlapping_recovery)
            ASSERT_TRUE(overlapping_recovery.error().code == amqp::error_code::command_invalid);
        ASSERT_TRUE((*old)->is_open());
        ASSERT_TRUE(co_await client.async_close());
        if (managed_recovery)
        {
            client.connection()->set_recovery_strategy(std::make_shared<amqp::automatic_recovery_strategy>(
                std::make_shared<amqp::exponential_backoff>(std::chrono::milliseconds{1}, std::chrono::milliseconds{1}), cancel_topology));
            auto recovered = co_await client.async_recover(recovery_cancel);
            if (cancel_topology)
            {
                ASSERT_FALSE(recovered);
                if (!recovered)
                {
                    if (failure_mode == topology_failure_mode::cancellation)
                        ASSERT_TRUE(recovered.error().code == amqp::error_code::cancelled);
                    else
                    {
                        ASSERT_FALSE(recovery_cancel.is_cancelled());
                        ASSERT_TRUE(recovered.error().code == amqp::error_code::precondition_failed);
                        ASSERT_EQ(recovered.error().reply_code, 406);
                        ASSERT_EQ(recovered.error().message, std::string{"bad"});
                        ASSERT_EQ(recovered.error().class_id, static_cast<unsigned>(cancel_stage) / 256);
                        ASSERT_EQ(recovered.error().method_id, static_cast<unsigned>(cancel_stage) % 256);
                    }
                }
            }
            else
                ASSERT_TRUE(recovered);
            if (!immediate_close)
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{10});
        }
        else
            ASSERT_TRUE(co_await client.async_connect(options));
        ASSERT_FALSE((*old)->is_open());
        auto publish = co_await (*old)->async_publish({}, {});
        ASSERT_FALSE(publish);
        if (!publish)
            ASSERT_TRUE(publish.error().code == amqp::error_code::channel_closed);
        auto ack = co_await (*old)->async_ack(1);
        ASSERT_FALSE(ack);
        if (!ack)
            ASSERT_TRUE(ack.error().code == amqp::error_code::channel_closed);
        if (!managed_recovery)
        {
            auto fresh = co_await client.async_open_channel();
            ASSERT_TRUE(fresh);
            ASSERT_TRUE((*fresh)->is_open());
        }
        if (reader_failure)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            while (client.state() != amqp::connection_state::disconnected && std::chrono::steady_clock::now() < deadline)
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
            ASSERT_TRUE(client.state() == amqp::connection_state::disconnected);
            auto closed = co_await client.async_close();
            ASSERT_FALSE(closed);
            if (!closed)
            {
                ASSERT_TRUE(closed.error().code == amqp::error_code::malformed_frame);
                ASSERT_EQ(closed.error().message, std::string{"truncated server Basic.Cancel"});
            }
        }
        else
            ASSERT_TRUE(co_await client.async_close());
        ASSERT_TRUE(client.state() == amqp::connection_state::disconnected);
        if (managed_recovery)
        {
            old->reset();
            auto connection = client.connection();
            ASSERT_EQ(connection.use_count(), 2L);
        }
        if (reader_failure || immediate_close)
        {
            ASSERT_TRUE(co_await client.async_connect(options));
            ASSERT_TRUE(co_await client.async_close());
        }
        if (cancel_topology)
        {
            recovery_cancel.reset();
            if (owned_session)
            {
                ASSERT_TRUE(co_await client.async_connect(options));
                unsigned ready = 0;
                auto session = [&]() -> cnetmod::task<void>
                {
                    auto result = co_await client.connection()->async_run_session(recovery_cancel, [&]
                        {
                            ++ready;
                            ASSERT_EQ(topology_requests[2].size(), 4U);
                        });
                    ASSERT_TRUE(recovery_cancel.is_cancelled());
                    ASSERT_FALSE(result);
                };
                auto stop_after_delivery = [&]() -> cnetmod::task<void>
                {
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
                    while (deliveries == 0 && std::chrono::steady_clock::now() < deadline)
                        (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
                    ASSERT_EQ(deliveries, 1U);
                    ASSERT_EQ(ready, 1U);
                    recovery_cancel.cancel();
                };
                co_await cnetmod::when_all(session(), stop_after_delivery());
                ASSERT_TRUE(client.state() == amqp::connection_state::disconnected);
            }
            else
                ASSERT_TRUE(co_await client.async_recover(recovery_cancel));
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            while (deliveries == 0 && std::chrono::steady_clock::now() < deadline)
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
            ASSERT_EQ(deliveries, 1U);
            ASSERT_TRUE(co_await client.async_close());
        }
        finish();
    };
    auto server = peer();
    auto operation = exercise();
    server.handle().resume();
    operation.handle().resume();
    io->run();
    server.handle().promise().result();
    operation.handle().promise().result();
    if (cancel_topology)
    {
        ASSERT_EQ(deliveries, 1U);
        const std::vector<unsigned> expected{
            static_cast<unsigned>(topology_cancel_stage::exchange), static_cast<unsigned>(topology_cancel_stage::queue),
            static_cast<unsigned>(topology_cancel_stage::binding), static_cast<unsigned>(topology_cancel_stage::consumer)};
        ASSERT_TRUE(topology_requests[0] == expected);
        ASSERT_TRUE(topology_requests[2] == expected);
        const auto stop = std::find(expected.begin(), expected.end(), static_cast<unsigned>(cancel_stage));
        const std::size_t prefix_size = cancel_stage == topology_cancel_stage::channel ? 0 : std::size_t(stop - expected.begin()) + 1;
        ASSERT_TRUE(topology_requests[1] == std::vector<unsigned>(expected.begin(), expected.begin() + prefix_size));
    }
}

} // namespace

TEST(amqp091_reconnected_client_rejects_stale_channel)
{
    verify_amqp091_reconnected_channel(false);
}

TEST(amqp091_recovery_reader_retains_parameters_until_completion)
{
    verify_amqp091_reconnected_channel(true);
}

TEST(amqp091_recovery_reader_preserves_protocol_failure)
{
    verify_amqp091_reconnected_channel(true, true);
}

TEST(amqp091_recovery_reader_can_close_before_dispatch)
{
    verify_amqp091_reconnected_channel(true, false, true);
}

TEST(amqp091_topology_recovery_cancels_silent_channel_open)
{
    verify_amqp091_reconnected_channel(true, false, false, true);
}

TEST(amqp091_topology_recovery_preserves_records_after_partial_replay)
{
    for (auto stage : {topology_cancel_stage::exchange, topology_cancel_stage::queue,
             topology_cancel_stage::binding, topology_cancel_stage::consumer})
        verify_amqp091_reconnected_channel(true, false, false, true, stage);
}

TEST(amqp091_topology_recovery_preserves_broker_rejection_and_records)
{
    for (auto stage : {topology_cancel_stage::channel, topology_cancel_stage::exchange, topology_cancel_stage::queue,
             topology_cancel_stage::binding, topology_cancel_stage::consumer})
        verify_amqp091_reconnected_channel(true, false, false, true, stage, topology_failure_mode::broker_rejection);
}

TEST(amqp091_owned_session_replays_before_ready_and_joins_reader)
{
    verify_amqp091_reconnected_channel(true, false, false, true, topology_cancel_stage::consumer,
        topology_failure_mode::broker_rejection, true);
}

namespace {
void verify_amqp091_channel_close_completion(bool disconnect_before_reply, bool malformed_subscription = false,
    bool run_reader = false, bool run_writer = false)
{
    namespace amqp = cnetmod::amqp091;
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener);
    ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}));
    ASSERT_TRUE(listener->listen());
    amqp::connection_options options;
    options.endpoint.port = listener->local_endpoint()->port();
    amqp::amqp091_client client{*io};
    unsigned completed = 0;
    unsigned close_requests = 0;
    std::shared_ptr<amqp::logical_channel> publishing_channel;
    std::optional<cnetmod::task<amqp::result<std::uint64_t>>> publication;
    auto finish = [&]
    {
        if (++completed == 2)
            io->stop();
    };
    auto peer = [&]() -> cnetmod::task<void>
    {
        auto connection = co_await cnetmod::async_accept(*io, *listener);
        ASSERT_TRUE(connection);
        std::vector<std::byte> wire;
        auto append = [&](std::uint16_t channel, std::uint16_t cls, std::uint16_t method, std::initializer_list<unsigned char> arguments)
        {
            amqp::method_frame reply{.channel = channel, .class_id = cls, .method_id = method};
            for (auto byte : arguments)
                reply.arguments.push_back(static_cast<std::byte>(byte));
            auto frame = amqp::encode_method(reply);
            auto encoded = amqp::encode_frame(*frame);
            wire.insert(wire.end(), encoded->begin(), encoded->end());
        };
        append(0, 10, 10, {0, 9, 0, 0, 0, 0, 0, 0, 0, 5, 'P', 'L', 'A', 'I', 'N', 0, 0, 0, 5, 'e', 'n', '_', 'U', 'S'});
        append(0, 10, 30, {0, 2, 0, 0, 16, 0, 0, 0});
        append(0, 10, 41, {0});
        append(1, 20, 11, {0, 0, 0, 0});
        if (malformed_subscription)
            append(2, 20, 11, {0, 0, 0, 0});
        ASSERT_TRUE(co_await cnetmod::async_write_all(*io, *connection, cnetmod::const_buffer{wire.data(), wire.size()}));
        amqp::frame_parser parser;
        std::array<std::byte, 4096> bytes{};
        std::size_t protocol_bytes = 8;
        while (close_requests == 0)
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
                if (run_writer && frame.type != amqp::frame_type::method)
                    continue;
                auto method = amqp::decode_method(frame);
                ASSERT_TRUE(method);
                ASSERT_TRUE(method->class_id == 10 || method->class_id == 20 || malformed_subscription);
                if (malformed_subscription && method->class_id == 60 && method->method_id == 20)
                {
                    if (run_writer)
                    {
                        amqp::message message;
                        message.body.resize(32 * 1024 * 1024);
                        publication.emplace(publishing_channel->async_publish({}, std::move(message)));
                        publication->handle().resume();
                        ASSERT_FALSE(publication->handle().done());
                    }
                    wire.clear();
                    append(1, 60, 21, {});
                    ASSERT_TRUE(co_await cnetmod::async_write_all(*io, *connection, cnetmod::const_buffer{wire.data(), wire.size()}));
                }
                if (method->class_id == 20 && method->method_id == 40)
                    ++close_requests;
            }
        }
        if (!disconnect_before_reply && !malformed_subscription)
        {
            wire.clear();
            append(1, 20, 41, {});
            ASSERT_TRUE(co_await cnetmod::async_write_all(*io, *connection, cnetmod::const_buffer{wire.data(), wire.size()}));
            auto read = co_await cnetmod::async_read(*io, *connection, cnetmod::mutable_buffer{bytes.data(), bytes.size()});
            if (read && *read != 0)
            {
                auto frames = parser.feed(std::span<const std::byte>{bytes.data(), *read});
                ASSERT_TRUE(frames);
                for (const auto& frame : *frames)
                {
                    auto method = amqp::decode_method(frame);
                    ASSERT_TRUE(method);
                    ASSERT_EQ(method->class_id, 10U);
                    ASSERT_EQ(method->method_id, 50U);
                }
            }
        }
        connection->close();
        finish();
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE(co_await client.async_connect(options));
        auto channel = co_await client.async_open_channel();
        ASSERT_TRUE(channel);
        struct failing_handler_copy
        {
            bool* fail;
            explicit failing_handler_copy(bool& value) : fail(&value) {}
            failing_handler_copy(const failing_handler_copy& other) : fail(other.fail)
            {
                if (*fail)
                    throw std::bad_alloc{};
            }
            void operator()(const amqp::delivery&) const {}
            void operator()(const amqp::delivery&, amqp::delivery_acknowledgement) const {}
        };
        for (bool acknowledged : {false, true})
        {
            bool fail = false;
            amqp::delivery_handler legacy{failing_handler_copy{fail}};
            amqp::acknowledged_delivery_handler receipt{failing_handler_copy{fail}};
            amqp::delivery_handler invalid_legacy{failing_handler_copy{fail}};
            amqp::acknowledged_delivery_handler invalid_receipt{failing_handler_copy{fail}};
            fail = true;
            try
            {
                auto invalid = acknowledged
                    ? (*channel)->async_consume_acknowledged({.queue = "unused", .no_wait = true}, std::move(invalid_receipt))
                    : (*channel)->async_consume({.queue = "unused", .no_wait = true}, std::move(invalid_legacy));
                auto rejected = co_await std::move(invalid);
                ASSERT_FALSE(rejected);
                if (!rejected)
                    ASSERT_TRUE(rejected.error().code == amqp::error_code::precondition_failed);
            }
            catch (const std::bad_alloc&)
            {
                ASSERT_TRUE(false);
            }
            auto subscription = acknowledged
                ? (*channel)->async_consume_acknowledged({.queue = "unused", .consumer_tag = "copy-failure", .no_wait = true}, std::move(receipt))
                : (*channel)->async_consume({.queue = "unused", .consumer_tag = "copy-failure", .no_wait = true}, std::move(legacy));
            fail = true;
            bool threw = false;
            try
            {
                (void)co_await std::move(subscription);
            }
            catch (const std::bad_alloc&)
            {
                threw = true;
            }
            fail = false;
            ASSERT_TRUE(threw);
            ASSERT_TRUE((*channel)->is_open());
        }
        if (malformed_subscription)
        {
            struct retired_observer final : amqp::publisher_confirm_observer
            {
                unsigned failures = 0;
                amqp::amqp091_client* closing_client = nullptr;
                void on_confirm(const amqp::publisher_confirmation&) override
                {
                    ASSERT_TRUE(false);
                }
                void on_confirm_failure(const amqp::error& reason) override
                {
                    ASSERT_TRUE(reason.code == amqp::error_code::connection_closed);
                    if (closing_client)
                        ASSERT_TRUE(closing_client->state() == amqp::connection_state::closing);
                    ++failures;
                }
            };
            auto other_channel = co_await client.async_open_channel();
            ASSERT_TRUE(other_channel);
            publishing_channel = *other_channel;
            auto observer = std::make_shared<retired_observer>();
            if (!run_reader)
                observer->closing_client = &client;
            (*other_channel)->observe_confirms(observer);
            if (run_writer)
                ASSERT_TRUE(co_await (*other_channel)->async_enable_confirms(true));
            cnetmod::cancel_token cancellation;
            auto pump = client.async_run(cancellation);
            if (run_reader)
                pump.handle().resume();
            auto consumed = co_await (*channel)->async_consume_acknowledged({.queue = "unused"},
                [](const amqp::delivery&, amqp::delivery_acknowledgement) {});
            ASSERT_FALSE(consumed);
            if (!consumed)
                ASSERT_TRUE(consumed.error().code == amqp::error_code::malformed_frame);
            ASSERT_FALSE((*channel)->is_open());
            if (run_reader)
            {
                const auto limit = cnetmod::deadline::after(std::chrono::seconds{2});
                while (!pump.handle().done() && !limit.expired())
                    (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
                ASSERT_TRUE(pump.handle().done());
                if (!pump.handle().done())
                {
                    cancellation.cancel();
                    while (!pump.handle().done())
                        (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
                }
                ASSERT_FALSE(pump.handle().promise().result());
            }
            if (!run_writer)
            {
                ASSERT_TRUE(client.state() == amqp::connection_state::disconnected);
                ASSERT_EQ(observer->failures, 1U);
            }
            if (run_writer)
            {
                auto [first_close, second_close] = co_await cnetmod::when_all(client.async_close(), client.async_close());
                ASSERT_TRUE(first_close);
                ASSERT_TRUE(second_close);
            }
            else
                ASSERT_TRUE(co_await client.async_close());
            if (run_writer)
            {
                ASSERT_TRUE(publication.has_value());
                const auto limit = cnetmod::deadline::after(std::chrono::seconds{2});
                while (!publication->handle().done() && !limit.expired())
                    (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
                ASSERT_TRUE(publication->handle().done());
                if (publication->handle().done())
                    ASSERT_FALSE(publication->handle().promise().result());
            }
            ASSERT_TRUE(client.state() == amqp::connection_state::disconnected);
            ASSERT_EQ(observer->failures, 1U);
            finish();
            co_return;
        }
        auto first = co_await (*channel)->async_close();
        ASSERT_EQ(first.has_value(), !disconnect_before_reply);
        ASSERT_FALSE((*channel)->is_open());
        auto second = co_await (*channel)->async_close();
        ASSERT_EQ(second.has_value(), !disconnect_before_reply);
        if (!second)
            ASSERT_TRUE(second.error().code == amqp::error_code::channel_closed);
        (void)co_await client.async_close();
        finish();
    };
    auto server = peer();
    auto operation = exercise();
    server.handle().resume();
    operation.handle().resume();
    io->run();
    server.handle().promise().result();
    operation.handle().promise().result();
    ASSERT_EQ(close_requests, malformed_subscription ? 0U : 1U);
}
} // namespace

TEST(amqp091_failed_channel_close_is_not_reported_as_completed)
{
    verify_amqp091_channel_close_completion(true);
}

TEST(amqp091_confirmed_channel_close_is_idempotent)
{
    verify_amqp091_channel_close_completion(false);
}

TEST(amqp091_malformed_subscription_retires_unregistered_session)
{
    verify_amqp091_channel_close_completion(true, true);
}

TEST(amqp091_malformed_subscription_joins_active_reader)
{
    verify_amqp091_channel_close_completion(true, true, true);
}

TEST(amqp091_malformed_subscription_close_joins_active_writer)
{
    verify_amqp091_channel_close_completion(true, true, false, true);
}
