module;
#include <cnetmod/config.hpp>

module cnetmod.testing.messaging.amqp091_driver;
import :rabbitmq_operation_executor;

import std;
import nlohmann.json;
import cnetmod.io.io_context;
import cnetmod.coro.cancel;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.protocol.amqp091;
import cnetmod.protocol.amqp091;

namespace cnetmod::testing::messaging::amqp091_driver {
namespace {

    using json = nlohmann::json;
    using namespace std::chrono_literals;

    template <class T>
    auto unwrap(amqp091::result<T> outcome, std::string_view action) -> T
    {
        if (!outcome)
            throw std::runtime_error(std::format("{}: {}", action,
                outcome.error().message));
        return std::move(*outcome);
    }

    void ensure(amqp091::result<void> outcome, std::string_view action)
    {
        if (!outcome)
            throw std::runtime_error(std::format("{}: {}", action,
                outcome.error().message));
    }

    auto decode_hex(std::string_view encoded) -> std::vector<std::byte>
    {
        if ((encoded.size() & 1U) != 0)
            throw std::invalid_argument("body_hex must contain an even number of digits");
        auto digit = [](char value) -> unsigned
        {
            if (value >= '0' && value <= '9')
                return static_cast<unsigned>(value - '0');
            if (value >= 'a' && value <= 'f')
                return static_cast<unsigned>(value - 'a' + 10);
            if (value >= 'A' && value <= 'F')
                return static_cast<unsigned>(value - 'A' + 10);
            throw std::invalid_argument("body_hex contains a non-hexadecimal digit");
        };
        std::vector<std::byte> result(encoded.size() / 2);
        for (std::size_t index = 0; index < result.size(); ++index)
            result[index] = static_cast<std::byte>((digit(encoded[index * 2]) << 4U) |
                digit(encoded[index * 2 + 1]));
        return result;
    }

    auto encode_hex(std::span<const std::byte> bytes) -> std::string
    {
        constexpr std::string_view digits = "0123456789abcdef";
        std::string result(bytes.size() * 2, '0');
        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            const auto value = std::to_integer<unsigned>(bytes[index]);
            result[index * 2] = digits[value >> 4U];
            result[index * 2 + 1] = digits[value & 15U];
        }
        return result;
    }

    auto make_message(std::string_view body_hex, const json& properties = json::object())
        -> cnetmod::amqp091::message
    {
        cnetmod::amqp091::message message;
        message.body = decode_hex(body_hex);
        message.content_type = properties.value("content_type", "");
        message.content_encoding = properties.value("content_encoding", "");
        message.message_id = properties.value("message_id", "");
        message.correlation_id = properties.value("correlation_id", "");
        message.reply_to = properties.value("reply_to", "");
        message.durable = properties.value("durable", false);
        if (auto found = properties.find("headers");
            found != properties.end() && found->is_object())
            for (auto iterator = found->begin(); iterator != found->end(); ++iterator)
                message.headers.emplace(iterator.key(), iterator.value().get<std::string>());
        return message;
    }

    auto connection_configuration(const json& parameters)
        -> amqp091::connection_options
    {
        amqp091::connection_options options;
        options.endpoint.host = parameters.at("host").get<std::string>();
        options.endpoint.port = parameters.at("port").get<std::uint16_t>();
        if (parameters.contains("connect_timeout_milliseconds"))
            options.endpoint.connect_timeout = std::chrono::milliseconds(
                parameters.at("connect_timeout_milliseconds").get<std::int64_t>());
        options.credentials.username = parameters.at("username").get<std::string>();
        options.credentials.password = parameters.at("password").get<std::string>();
        options.virtual_host = parameters.value("virtual_host", "/");
        options.heartbeat = std::chrono::seconds(parameters.value("heartbeat_seconds", 3));
        options.automatic_recovery = parameters.value("automatic_recovery", false);
        if (parameters.value("tls", false))
        {
            options.endpoint.tls.enabled = true;
            options.endpoint.tls.verify_peer = true;
            options.endpoint.tls.ca_file = parameters.value("ca_file", "");
            options.endpoint.tls.server_name = parameters.value("server_name", options.endpoint.host);
        }
        return options;
    }

    struct connected_client
    {
        explicit connected_client(io_context& context)
            : client(std::make_unique<amqp091::amqp091_client>(context)) {}

        std::unique_ptr<amqp091::amqp091_client> client;
        cancel_token read_loop_cancellation;
    };

    auto consume_connection_frames(connected_client& connection) -> task<void>
    {
        (void)co_await connection.client->async_run(connection.read_loop_cancellation);
    }

    auto connect_client(io_context& context, const json& parameters)
        -> task<std::unique_ptr<connected_client>>
    {
        auto connection = std::make_unique<connected_client>(context);
        ensure(co_await connection->client->async_connect(
                   connection_configuration(parameters)),
            "connect AMQP 0-9-1 client");
        spawn(context, consume_connection_frames(*connection));
        co_return std::move(connection);
    }

    auto open_channel(connected_client& connection)
        -> task<std::shared_ptr<amqp091::logical_channel>>
    {
        co_return unwrap(co_await connection.client->async_open_channel(),
            "open AMQP channel");
    }

    auto close_client(io_context& context,
        std::unique_ptr<connected_client> connection) -> task<void>
    {
        if (!connection)
            co_return;
        connection->read_loop_cancellation.cancel();
        (void)co_await connection->client->async_close();
        co_await async_sleep(context, 2ms);
    }

    template <class Predicate>
    auto wait_until(io_context& context, Predicate predicate,
        std::chrono::steady_clock::duration timeout,
        std::string_view description) -> task<void>
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!predicate())
        {
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error(std::format("timed out waiting for {}", description));
            co_await async_sleep(context, 2ms);
        }
    }

    class confirmation_collector final : public amqp091::publisher_confirm_observer
    {
    public:
        void on_confirm(const amqp091::publisher_confirmation& confirmation) override
        {
            std::scoped_lock lock(mutex_);
            if (confirmation.multiple)
            {
                for (std::uint64_t tag = 1; tag <= confirmation.delivery_tag; ++tag)
                    settled_[tag] = confirmation.acknowledged;
            }
            else
            {
                settled_[confirmation.delivery_tag] = confirmation.acknowledged;
            }
        }

        void on_confirm_failure(const amqp091::error& reason) override
        {
            std::scoped_lock lock(mutex_);
            failure_ = reason.message;
        }

        [[nodiscard]] auto confirmed(std::uint64_t tag) const -> bool
        {
            std::scoped_lock lock(mutex_);
            if (failure_)
                throw std::runtime_error("publisher confirm failure: " + *failure_);
            auto found = settled_.find(tag);
            return found != settled_.end() && found->second;
        }

        [[nodiscard]] auto confirmed_count() const -> std::size_t
        {
            std::scoped_lock lock(mutex_);
            if (failure_)
                throw std::runtime_error("publisher confirm failure: " + *failure_);
            return static_cast<std::size_t>(std::ranges::count_if(
                settled_, [](const auto& entry)
                {
                    return entry.second;
                }));
        }

    private:
        mutable std::mutex mutex_;
        std::map<std::uint64_t, bool> settled_;
        std::optional<std::string> failure_;
    };

    class delivery_collector
    {
    public:
        void add(const amqp091::delivery& delivery)
        {
            std::scoped_lock lock(mutex_);
            deliveries_.push_back(delivery);
        }

        [[nodiscard]] auto size() const -> std::size_t
        {
            std::scoped_lock lock(mutex_);
            return deliveries_.size();
        }

        [[nodiscard]] auto at(std::size_t index) const -> amqp091::delivery
        {
            std::scoped_lock lock(mutex_);
            return deliveries_.at(index);
        }

    private:
        mutable std::mutex mutex_;
        std::vector<amqp091::delivery> deliveries_;
    };

    auto declare_test_queue(amqp091::logical_channel& channel, std::string name,
        bool durable = false) -> task<void>
    {
        (void)unwrap(co_await channel.async_declare_queue(
                         {.name = std::move(name),
                             .durable = durable,
                             .auto_delete = !durable}),
            "declare test queue");
    }

    auto enable_confirms(amqp091::logical_channel& channel)
        -> task<std::shared_ptr<confirmation_collector>>
    {
        auto collector = std::make_shared<confirmation_collector>();
        channel.observe_confirms(collector);
        ensure(co_await channel.async_enable_confirms(), "enable publisher confirms");
        co_return collector;
    }

    auto publish_operation(io_context& context, const json& parameters) -> task<json>
    {
        auto connection = co_await connect_client(context, parameters);
        auto channel = co_await open_channel(*connection);
        std::shared_ptr<confirmation_collector> confirms;
        if (parameters.value("publisher_confirm", false))
            confirms = co_await enable_confirms(*channel);
        const auto tag = unwrap(
            co_await channel->async_publish(
                {.exchange = parameters.value("exchange", ""),
                    .routing_key = parameters.at("routing_key").get<std::string>()},
                make_message(parameters.at("body_hex").get<std::string>(),
                    parameters.value("properties", json::object()))),
            "publish message");
        if (confirms)
            co_await wait_until(context, [&]
                {
                    return confirms->confirmed(tag);
                },
                15s, "publisher confirmation");
        co_await close_client(context, std::move(connection));
        co_return json{{"confirmed", confirms != nullptr}};
    }

    auto consume_one_operation(io_context& context, const json& parameters)
        -> task<json>
    {
        auto connection = co_await connect_client(context, parameters);
        auto channel = co_await open_channel(*connection);
        auto deliveries = std::make_shared<delivery_collector>();
        auto consumer = unwrap(co_await channel->async_consume(
                                   {.queue = parameters.at("queue").get<std::string>()},
                                   [deliveries](const amqp091::delivery& delivery)
                                   {
                                       deliveries->add(delivery);
                                   }),
            "start consumer");
        co_await wait_until(context, [&]
            {
                return deliveries->size() >= 1;
            },
            15s, "one delivery");
        auto delivery = deliveries->at(0);
        const auto settlement = parameters.value("settlement", "ack");
        if (settlement == "nack_requeue")
            ensure(co_await channel->async_nack(delivery.delivery_tag, false, true),
                "nack and requeue delivery");
        else if (settlement == "nack_drop")
            ensure(co_await channel->async_nack(delivery.delivery_tag, false, false),
                "nack delivery");
        else
            ensure(co_await channel->async_ack(delivery.delivery_tag), "ack delivery");
        ensure(co_await channel->async_cancel_consumer(consumer), "cancel consumer");
        co_await close_client(context, std::move(connection));
        co_return json{{"body_hex", encode_hex(delivery.message.body)},
            {"redelivered", delivery.redelivered}};
    }

    auto transaction_operation(io_context& context, const json& parameters)
        -> task<json>
    {
        auto connection = co_await connect_client(context, parameters);
        auto channel = co_await open_channel(*connection);
        const auto queue = parameters.at("queue").get<std::string>();
        ensure(co_await channel->async_select_transaction(), "select transaction");
        (void)unwrap(co_await channel->async_publish(
                         {.exchange = "", .routing_key = queue},
                         make_message(parameters.at("committed_body_hex").get<std::string>())),
            "publish committed transaction message");
        ensure(co_await channel->async_commit_transaction(), "commit transaction");
        (void)unwrap(co_await channel->async_publish(
                         {.exchange = "", .routing_key = queue},
                         make_message(parameters.at("rolled_back_body_hex").get<std::string>())),
            "publish rolled-back transaction message");
        ensure(co_await channel->async_rollback_transaction(), "rollback transaction");
        co_await close_client(context, std::move(connection));
        co_return json{{"committed", true}, {"rolled_back", true}};
    }

    auto reconnect_operation(io_context& context, const json& parameters)
        -> task<json>
    {
        auto recovery_parameters = parameters;
        recovery_parameters["automatic_recovery"] = true;
        auto connection = co_await connect_client(context, recovery_parameters);
        auto initial_channel = co_await open_channel(*connection);
        const auto queue = parameters.at("durable_queue").get<std::string>();
        co_await declare_test_queue(*initial_channel, queue, true);
        co_await wait_until(
            context,
            [&]
            {
                return connection->client->state() != amqp091::connection_state::open;
            },
            std::chrono::seconds(parameters.value("reconnect_timeout_seconds", 60)),
            "injected broker disconnect");
        connection->read_loop_cancellation.reset();
        ensure(co_await connection->client->async_recover(
                   connection->read_loop_cancellation),
            "recover connection and durable topology");
        auto channel = co_await open_channel(*connection);
        auto confirms = co_await enable_confirms(*channel);
        auto tag = unwrap(co_await channel->async_publish(
                              {.exchange = "", .routing_key = queue},
                              make_message(parameters.at("body_hex").get<std::string>())),
            "publish after reconnect");
        co_await wait_until(context, [&]
            {
                return confirms->confirmed(tag);
            },
            15s, "post-reconnect publisher confirmation");
        co_await close_client(context, std::move(connection));
        co_return json{{"reconnected", true},
            {"topology_restored", true},
            {"publish_confirmed", true}};
    }

    auto security_operation(io_context& context, const json& parameters)
        -> task<json>
    {
        try
        {
            auto connection = co_await connect_client(context, parameters);
            co_await close_client(context, std::move(connection));
        }
        catch (const std::exception&)
        {
            if (parameters.value("expected_authentication", true))
                throw;
            co_return json{{"authentication_rejected", true}};
        }
        if (!parameters.value("expected_authentication", true))
            throw std::runtime_error("broker unexpectedly accepted invalid credentials");
        co_return json{{"tls_verified", true}};
    }

    auto boundary_operation(io_context& context, const json& parameters)
        -> task<json>
    {
        auto connection = co_await connect_client(context, parameters);
        auto channel = co_await open_channel(*connection);
        const auto queue = parameters.at("queue").get<std::string>();
        co_await declare_test_queue(*channel, queue);
        auto deliveries = std::make_shared<delivery_collector>();
        auto consumer = unwrap(co_await channel->async_consume(
                                   {.queue = queue},
                                   [deliveries](const amqp091::delivery& delivery)
                                   {
                                       deliveries->add(delivery);
                                   }),
            "start boundary consumer");
        json properties{{"headers", parameters.value("headers", json::object())}};
        for (const auto& body : parameters.at("bodies_hex"))
            (void)unwrap(co_await channel->async_publish(
                             {.exchange = "", .routing_key = queue},
                             make_message(body.get<std::string>(), properties)),
                "publish boundary message");
        const auto expected = parameters.at("bodies_hex").size();
        co_await wait_until(context, [&]
            {
                return deliveries->size() == expected;
            },
            20s, "boundary message round trips");
        json round_trip = json::array();
        for (std::size_t index = 0; index < expected; ++index)
        {
            auto delivery = deliveries->at(index);
            round_trip.push_back(encode_hex(delivery.message.body));
            ensure(co_await channel->async_ack(delivery.delivery_tag), "ack boundary message");
        }
        ensure(co_await channel->async_cancel_consumer(consumer), "cancel boundary consumer");
        const bool remained_open = connection->client->state() == amqp091::connection_state::open;
        co_await close_client(context, std::move(connection));
        co_return json{{"round_trip_bodies_hex", std::move(round_trip)},
            {"connection_remained_open", remained_open}};
    }

    auto qos_operation(io_context& context, const json& parameters) -> task<json>
    {
        auto connection = co_await connect_client(context, parameters);
        auto channel = co_await open_channel(*connection);
        const auto queue = parameters.at("queue").get<std::string>();
        const auto count = parameters.at("published_message_count").get<std::size_t>();
        const auto prefetch = parameters.at("prefetch_count").get<std::uint16_t>();
        co_await declare_test_queue(*channel, queue);
        ensure(co_await channel->async_set_qos({.prefetch_count = prefetch}), "set QoS");
        auto deliveries = std::make_shared<delivery_collector>();
        auto consumer = unwrap(co_await channel->async_consume(
                                   {.queue = queue},
                                   [deliveries](const amqp091::delivery& delivery)
                                   {
                                       deliveries->add(delivery);
                                   }),
            "start QoS consumer");
        for (std::size_t index = 0; index < count; ++index)
            (void)unwrap(co_await channel->async_publish(
                             {.exchange = "", .routing_key = queue},
                             make_message(encode_hex(std::as_bytes(std::span{&index, 1})))),
                "publish QoS probe message");
        co_await wait_until(context, [&]
            {
                return deliveries->size() >= prefetch;
            },
            15s, "initial QoS credit");
        co_await async_sleep(context, 100ms);
        const auto maximum_unacknowledged = deliveries->size();
        std::size_t acknowledged = 0;
        while (acknowledged < count)
        {
            co_await wait_until(context, [&]
                {
                    return deliveries->size() > acknowledged;
                },
                15s, "next QoS delivery");
            while (acknowledged < deliveries->size())
            {
                ensure(co_await channel->async_ack(deliveries->at(acknowledged).delivery_tag),
                    "ack QoS delivery");
                ++acknowledged;
            }
        }
        ensure(co_await channel->async_cancel_consumer(consumer), "cancel QoS consumer");
        co_await close_client(context, std::move(connection));
        co_return json{{"maximum_simultaneous_unacknowledged", maximum_unacknowledged},
            {"received_after_ack", acknowledged}};
    }

    auto stable_payload(std::uint64_t sequence, std::size_t size)
        -> std::vector<std::byte>
    {
        std::vector<std::byte> payload(size);
        for (std::size_t index = 0; index < size; ++index)
            payload[index] = static_cast<std::byte>((sequence * 131U + index * 17U) & 0xffU);
        for (std::size_t index = 0; index < std::min(size, sizeof(sequence)); ++index)
            payload[index] = static_cast<std::byte>(sequence >> (index * 8U));
        return payload;
    }

    auto payload_sequence(std::span<const std::byte> payload) -> std::uint64_t
    {
        std::uint64_t result = 0;
        for (std::size_t index = 0; index < std::min(payload.size(), sizeof(result)); ++index)
            result |= static_cast<std::uint64_t>(std::to_integer<unsigned>(payload[index]))
                << (index * 8U);
        return result;
    }

    auto sustained_operation(io_context& context, const json& parameters)
        -> task<json>
    {
        auto connection = co_await connect_client(context, parameters);
        auto channel = co_await open_channel(*connection);
        const auto queue = parameters.at("queue").get<std::string>();
        const auto count = parameters.at("message_count").get<std::size_t>();
        const auto payload_size = parameters.at("payload_size").get<std::size_t>();
        const auto prefetch = parameters.at("prefetch_count").get<std::uint16_t>();
        const auto confirm_window = std::max<std::size_t>(
            1, parameters.value("publisher_confirm_window", std::size_t{256}));
        co_await declare_test_queue(*channel, queue);
        ensure(co_await channel->async_set_qos({.prefetch_count = prefetch}),
            "set sustained-delivery QoS");
        auto deliveries = std::make_shared<delivery_collector>();
        auto consumer = unwrap(co_await channel->async_consume(
                                   {.queue = queue},
                                   [deliveries](const amqp091::delivery& delivery)
                                   {
                                       deliveries->add(delivery);
                                   }),
            "start sustained-delivery consumer");
        auto confirms = co_await enable_confirms(*channel);
        for (std::size_t index = 0; index < count; ++index)
        {
            cnetmod::amqp091::message message;
            message.body = stable_payload(index, payload_size);
            (void)unwrap(co_await channel->async_publish(
                             {.exchange = "", .routing_key = queue}, std::move(message)),
                "publish sustained-delivery message");
            const auto published = index + 1;
            if (published >= confirm_window)
                co_await wait_until(context, [&]
                    {
                        return published - confirms->confirmed_count() < confirm_window;
                    },
                    60s, "publisher-confirm window credit");
        }
        co_await wait_until(context, [&]
            {
                return confirms->confirmed_count() == count;
            },
            120s, "all sustained publisher confirmations");

        std::size_t acknowledged = 0;
        std::size_t duplicates = 0;
        std::size_t mismatches = 0;
        std::set<std::uint64_t> sequences;
        while (acknowledged < count)
        {
            co_await wait_until(context, [&]
                {
                    return deliveries->size() > acknowledged;
                },
                60s, "sustained consumer delivery");
            while (acknowledged < deliveries->size())
            {
                auto delivery = deliveries->at(acknowledged);
                auto sequence = payload_sequence(delivery.message.body);
                if (!sequences.insert(sequence).second)
                    ++duplicates;
                if (sequence >= count || delivery.message.body != stable_payload(sequence, payload_size))
                    ++mismatches;
                ensure(co_await channel->async_ack(delivery.delivery_tag),
                    "ack sustained-delivery message");
                ++acknowledged;
            }
        }
        ensure(co_await channel->async_cancel_consumer(consumer),
            "cancel sustained-delivery consumer");
        co_await close_client(context, std::move(connection));
        co_return json{{"confirmed_count", confirms->confirmed_count()},
            {"consumed_count", acknowledged},
            {"duplicate_count", duplicates},
            {"payload_mismatch_count", mismatches}};
    }

} // namespace

auto execute_rabbitmq_operation(io_context& context, const json& request)
    -> task<json>
{
    if (!request.contains("parameters") || !request["parameters"].is_object())
        throw std::invalid_argument("parameters must be a JSON object");
    const auto& parameters = request["parameters"];
    const auto operation = request.value("operation", "");
    if (operation == "publish")
        co_return co_await publish_operation(context, parameters);
    if (operation == "consume_one")
        co_return co_await consume_one_operation(context, parameters);
    if (operation == "transaction_probe")
        co_return co_await transaction_operation(context, parameters);
    if (operation == "reconnect_and_publish")
        co_return co_await reconnect_operation(context, parameters);
    if (operation == "connect_security_probe")
        co_return co_await security_operation(context, parameters);
    if (operation == "message_boundary_probe")
        co_return co_await boundary_operation(context, parameters);
    if (operation == "qos_prefetch_probe")
        co_return co_await qos_operation(context, parameters);
    if (operation == "sustained_delivery_probe")
        co_return co_await sustained_operation(context, parameters);
    throw std::invalid_argument("unsupported AMQP 0-9-1 operation: " + operation);
}

} // namespace cnetmod::testing::messaging::amqp091_driver
