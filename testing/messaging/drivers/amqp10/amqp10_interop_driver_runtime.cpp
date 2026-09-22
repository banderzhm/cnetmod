module;

#include <cnetmod/config.hpp>

module cnetmod.testing.messaging.amqp10_interop_driver;

import std;
import cnetmod.json;
import cnetmod.io.io_context;
import cnetmod.coro.cancel;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.protocol.amqp10;

namespace cnetmod::testing::messaging::amqp10 {
namespace {

    using json = cnetmod::json::document;
    namespace protocol = cnetmod::amqp10;

    template <typename T>
    auto get_or(const json& source, std::string_view key, T fallback) -> T
    {
        return cnetmod::json::value_or(source, key, std::move(fallback));
    }

    auto get_or(const json& source, std::string_view key, const char* fallback)
        -> std::string
    {
        return cnetmod::json::value_or(source, key, fallback);
    }

    auto bytes_to_json(std::span<const std::byte> bytes) -> json
    {
        auto result = cnetmod::json::array();
        result.get_array().reserve(bytes.size());
        for (const auto value : bytes)
            result.get_array().emplace_back(std::to_integer<std::uint8_t>(value));
        return result;
    }

    [[nodiscard]] auto unique_name(std::string_view prefix) -> std::string
    {
        return std::format("{}-{}", prefix,
            std::chrono::steady_clock::now().time_since_epoch().count());
    }

    [[nodiscard]] auto error_text(const cnetmod::amqp10::error& error)
        -> std::string
    {
        return std::format("{}: {} ({})", cnetmod::amqp10::to_string(error.stage),
            error.message, error.code.message());
    }

    template <class T>
    auto require_value(std::expected<T, cnetmod::amqp10::error> result) -> T
    {
        if (!result)
            throw std::runtime_error(error_text(result.error()));
        return std::move(*result);
    }

    void require_success(std::expected<void, cnetmod::amqp10::error> result)
    {
        if (!result)
            throw std::runtime_error(error_text(result.error()));
    }

    [[nodiscard]] auto json_to_amqp_value(const json& input) -> protocol::value
    {
        if (input.is_null())
            return {};
        if (input.is_boolean())
            return protocol::value{input.get<bool>()};
        if (input.is_uint64())
            return protocol::value{input.get<std::uint64_t>()};
        if (input.is_int64())
            return protocol::value{input.get<std::int64_t>()};
        if (input.is_double())
            return protocol::value{input.get<double>()};
        if (input.is_string())
            return protocol::value{input.get<std::string>()};
        if (input.is_array())
        {
            protocol::list entries;
            entries.reserve(input.size());
            for (const auto& entry : input.get_array())
                entries.emplace_back(json_to_amqp_value(entry));
            return protocol::value::make_list(std::move(entries));
        }
        if (input.is_object())
        {
            protocol::map entries;
            entries.reserve(input.size());
            for (const auto& [key, value] : input.get_object())
                entries.emplace_back(protocol::value{key}, json_to_amqp_value(value));
            return protocol::value::make_map(std::move(entries));
        }
        throw std::invalid_argument("unsupported JSON value");
    }

    [[nodiscard]] auto amqp_value_to_json(const protocol::value& input) -> json
    {
        return std::visit(
            [](const auto& stored) -> json
            {
                using stored_value = std::remove_cvref_t<decltype(stored)>;
                if constexpr (std::same_as<stored_value, std::monostate>)
                    return nullptr;
                else if constexpr (std::same_as<stored_value, bool> ||
                    std::integral<stored_value> ||
                    std::floating_point<stored_value>)
                    return stored;
                else if constexpr (std::same_as<stored_value, std::string>)
                    return stored;
                else if constexpr (std::same_as<stored_value, protocol::symbol>)
                    return std::string(std::string_view(stored));
                else if constexpr (std::same_as<stored_value,
                                       std::shared_ptr<protocol::list>> ||
                    std::same_as<stored_value,
                        std::shared_ptr<protocol::array>>)
                {
                    auto result = cnetmod::json::array();
                    if (stored)
                        for (const auto& entry : *stored)
                            result.get_array().push_back(amqp_value_to_json(entry));
                    return result;
                }
                else if constexpr (std::same_as<stored_value,
                                       std::shared_ptr<protocol::map>>)
                {
                    auto result = cnetmod::json::object();
                    if (stored)
                        for (const auto& [key, entry] : *stored)
                        {
                            auto json_key = amqp_value_to_json(key);
                            if (!json_key.is_string())
                                throw std::runtime_error("AMQP map key is not a string");
                            result[json_key.template get<std::string>()] =
                                amqp_value_to_json(entry);
                        }
                    return result;
                }
                else if constexpr (std::same_as<stored_value, protocol::binary>)
                {
                    return bytes_to_json(stored);
                }
                else
                {
                    throw std::runtime_error("AMQP value has no JSON representation");
                }
            },
            input.data);
    }

    [[nodiscard]] auto message_body_to_json(const protocol::message_body& body)
        -> json
    {
        if (const auto* value = std::get_if<protocol::value>(&body))
            return amqp_value_to_json(*value);
        if (const auto* binary = std::get_if<protocol::binary>(&body))
            return bytes_to_json(*binary);
        auto result = cnetmod::json::array();
        for (const auto& sequence : std::get<std::vector<protocol::list>>(body))
        {
            auto entries = cnetmod::json::array();
            for (const auto& entry : sequence)
                entries.get_array().push_back(amqp_value_to_json(entry));
            result.get_array().push_back(std::move(entries));
        }
        return result;
    }

    [[nodiscard]] auto make_message(const json& body,
        const json* application_properties = nullptr)
        -> protocol::message
    {
        protocol::message message;
        message.body = json_to_amqp_value(body);
        message.properties = protocol::properties_section{};
        message.properties->content_type = "application/json";
        if (application_properties && application_properties->is_object())
            for (const auto& [key, value] : application_properties->get_object())
                message.application.emplace(key, json_to_amqp_value(value));
        return message;
    }

    [[nodiscard]] auto outcome_name(protocol::outcome_kind outcome)
        -> std::string_view
    {
        switch (outcome)
        {
        case protocol::outcome_kind::accepted:
            return "accepted";
        case protocol::outcome_kind::rejected:
            return "rejected";
        case protocol::outcome_kind::released:
            return "released";
        case protocol::outcome_kind::modified:
            return "modified";
        case protocol::outcome_kind::declared:
            return "declared";
        case protocol::outcome_kind::transactional:
            return "transactional";
        }
        return "unknown";
    }

    [[nodiscard]] auto parse_outcome(std::string_view name)
        -> protocol::delivery_outcome
    {
        if (name == "accepted")
            return {.kind = protocol::outcome_kind::accepted};
        if (name == "released")
            return {.kind = protocol::outcome_kind::released};
        if (name == "rejected")
            return {.kind = protocol::outcome_kind::rejected,
                .error = protocol::error_condition{
                    .condition = "amqp:rejected",
                    .description = "interop outcome probe"}};
        if (name == "modified")
            return {.kind = protocol::outcome_kind::modified,
                .delivery_failed = true,
                .undeliverable_here = true};
        throw std::invalid_argument(std::format("unknown delivery outcome: {}", name));
    }

    struct connected_client
    {
        std::unique_ptr<cnetmod::io_context> context;
        protocol::client connection;
        cnetmod::cancel_token cancellation;
        std::string_view stage = "initialization";
        bool timed_out = false;

        explicit connected_client(std::unique_ptr<cnetmod::io_context> io)
            : context(std::move(io)), connection(*context) {}
    };

    [[nodiscard]] auto client_options_from(const json& parameters)
        -> protocol::client_options
    {
        protocol::client_options options;
        options.endpoint.host = parameters.at("host").get<std::string>();
        options.endpoint.port = parameters.at("port").as<std::uint16_t>();
        options.endpoint.tls.enabled = get_or(parameters, "tls", false);
        options.endpoint.tls.verify_peer = get_or(parameters, "verify_hostname", true);
        options.endpoint.tls.ca_file = get_or(parameters, "ca_file", std::string{});
        options.endpoint.tls.server_name = options.endpoint.host;
        options.credentials.mechanism =
            cnetmod::amqp10::authentication_mechanism::plain;
        options.credentials.username = parameters.at("username").get<std::string>();
        options.credentials.password = parameters.at("password").get<std::string>();
        options.container_id = unique_name("cnetmod-amqp10-interop");
        options.hostname = options.endpoint.host;
        options.idle_timeout = std::chrono::milliseconds(
            get_or(parameters, "idle_timeout_milliseconds", 60000));
        return options;
    }

    auto connect_client(connected_client& runtime, const json& parameters)
        -> cnetmod::task<void>
    {
        runtime.stage = "connection establishment";
        require_success(co_await runtime.connection.connect(
            client_options_from(parameters), runtime.cancellation));
    }

    auto begin_session(connected_client& runtime) -> cnetmod::task<protocol::session>
    {
        runtime.stage = "session begin";
        auto session = require_value(runtime.connection.make_session());
        require_success(co_await session.begin(runtime.cancellation));
        co_return session;
    }

    auto make_sender(protocol::session& session, std::string_view address,
        cnetmod::cancel_token& token)
        -> cnetmod::task<protocol::sender_link>
    {
        auto sender = require_value(session.make_sender(
            {.name = unique_name("sender"),
                .target_terminus = protocol::target{.address = std::string(address)},
                .sender_settlement = protocol::sender_settle_mode::mixed,
                .receiver_settlement = protocol::receiver_settle_mode::first}));
        require_success(co_await sender.attach(token));
        co_return sender;
    }

    auto make_receiver(protocol::session& session, std::string_view address,
        std::uint32_t credit, cnetmod::cancel_token& token)
        -> cnetmod::task<protocol::receiver_link>
    {
        auto receiver = require_value(session.make_receiver(
            {.name = unique_name("receiver"),
                .source_terminus = protocol::source{.address = std::string(address)},
                .sender_settlement = protocol::sender_settle_mode::mixed,
                .receiver_settlement = protocol::receiver_settle_mode::first}));
        require_success(co_await receiver.attach(credit, token));
        co_return receiver;
    }

    auto send_operation(connected_client& runtime, const json& parameters)
        -> cnetmod::task<json>
    {
        co_await connect_client(runtime, parameters);
        auto session = co_await begin_session(runtime);
        runtime.stage = "sender link attach";
        auto sender = co_await make_sender(session, parameters.at("address").get<std::string>(),
            runtime.cancellation);
        const bool settled = get_or(parameters, "settlement", "unsettled") == "settled";
        runtime.stage = "message transfer";
        auto sent = require_value(co_await sender.send(
            make_message(parameters.at("body")), {.settled = settled},
            runtime.cancellation));
        co_return json{{"remote_outcome", outcome_name(sent.outcome.kind)}};
    }

    auto receive_one_operation(connected_client& runtime, const json& parameters)
        -> cnetmod::task<json>
    {
        co_await connect_client(runtime, parameters);
        auto session = co_await begin_session(runtime);
        auto receiver = co_await make_receiver(
            session, parameters.at("address").get<std::string>(),
            get_or(parameters, "link_credit", 1U), runtime.cancellation);
        auto received = require_value(co_await receiver.receive(runtime.cancellation));
        if (!received.settled)
            require_success(co_await receiver.settle(
                received.delivery_id,
                parse_outcome(get_or(parameters, "outcome", "accepted")),
                runtime.cancellation));
        const auto content_type = received.payload.properties
            ? received.payload.properties->content_type
            : std::string{};
        co_return json{{"body", message_body_to_json(received.payload.body)},
            {"content_type", content_type},
            {"settled", true}};
    }

    auto link_credit_probe(connected_client& runtime, const json& parameters)
        -> cnetmod::task<json>
    {
        co_await connect_client(runtime, parameters);
        auto session = co_await begin_session(runtime);
        auto receiver = co_await make_receiver(
            session, parameters.at("address").get<std::string>(),
            parameters.at("initial_credit").as<std::uint32_t>(), runtime.cancellation);
        auto first = require_value(co_await receiver.receive(runtime.cancellation));
        if (!first.settled)
            require_success(co_await receiver.settle(
                first.delivery_id, {.kind = protocol::outcome_kind::accepted},
                runtime.cancellation));
        json bodies = cnetmod::json::array({message_body_to_json(first.payload.body)});
        const auto before = bodies.size();
        require_success(co_await receiver.add_credit(
            parameters.at("replenish_credit").as<std::uint32_t>(), false,
            runtime.cancellation));
        auto second = require_value(co_await receiver.receive(runtime.cancellation));
        if (!second.settled)
            require_success(co_await receiver.settle(
                second.delivery_id, {.kind = protocol::outcome_kind::accepted},
                runtime.cancellation));
        bodies.get_array().push_back(message_body_to_json(second.payload.body));
        co_return json{{"before_replenish_count", before},
            {"after_replenish_count", bodies.size()},
            {"bodies", std::move(bodies)}};
    }

    auto delivery_outcome_probe(connected_client& runtime, const json& parameters)
        -> cnetmod::task<json>
    {
        co_await connect_client(runtime, parameters);
        auto sender_session = co_await begin_session(runtime);
        auto receiver_session = co_await begin_session(runtime);
        const auto address = parameters.at("address").get<std::string>();
        auto sender =
            co_await make_sender(sender_session, address, runtime.cancellation);
        auto receiver =
            co_await make_receiver(receiver_session, address, 1, runtime.cancellation);
        json observed = cnetmod::json::array();
        std::uint64_t sequence = 0;
        for (const auto& requested : parameters.at("outcomes").get_array())
        {
            require_value(co_await sender.send(make_message(sequence++), {.settled = false},
                runtime.cancellation));
            auto delivery = require_value(co_await receiver.receive(runtime.cancellation));
            const auto name = requested.get<std::string>();
            auto outcome = parse_outcome(name);
            require_success(co_await receiver.settle(delivery.delivery_id, outcome,
                runtime.cancellation));
            observed.get_array().emplace_back(outcome_name(outcome.kind));
            require_success(co_await receiver.add_credit(1, false, runtime.cancellation));
        }
        co_return json{{"observed_outcomes", std::move(observed)}};
    }

    auto reconnect_link_probe(connected_client& runtime, const json& parameters)
        -> cnetmod::task<json>
    {
        auto options = client_options_from(parameters);
        options.reconnect = std::make_shared<cnetmod::amqp10::exponential_backoff>(
            std::chrono::milliseconds{100}, std::chrono::seconds{2}, 1.5, 80);
        std::size_t opened_count = 0;
        runtime.connection.on_state_change([&opened_count](protocol::connection_state state)
            {
                if (state == protocol::connection_state::opened)
                    ++opened_count;
            });
        require_success(
            co_await runtime.connection.connect(std::move(options), runtime.cancellation));
        auto session = co_await begin_session(runtime);
        auto sender = co_await make_sender(session, parameters.at("address").get<std::string>(),
            runtime.cancellation);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{90};
        while ((opened_count < 2 ||
                   session.state() != protocol::session_state::mapped ||
                   sender.state() != protocol::link_state::attached) &&
            std::chrono::steady_clock::now() < deadline)
            co_await cnetmod::async_sleep(*runtime.context, std::chrono::milliseconds{50});
        if (opened_count < 2 || session.state() != protocol::session_state::mapped ||
            sender.state() != protocol::link_state::attached)
            throw std::runtime_error(
                "broker restart did not restore the connection, session, and link");

        auto sent = require_value(co_await sender.send(
            make_message("after-reconnect"), {.settled = false}, runtime.cancellation));
        const bool accepted = sent.outcome.kind == protocol::outcome_kind::accepted;
        co_return json{{"connection_reopened", opened_count >= 2},
            {"session_rebegun", session.state() == protocol::session_state::mapped},
            {"link_reattached", sender.state() == protocol::link_state::attached},
            {"unsettled_deliveries_resolved", accepted}};
    }

    auto connect_security_probe(connected_client& runtime, const json& parameters)
        -> cnetmod::task<json>
    {
        co_await connect_client(runtime, parameters);
        bool requested_plain = false;
        for (const auto& mechanism : parameters.at("sasl_mechanisms").get_array())
            requested_plain = requested_plain ||
                mechanism.get<std::string>() == "PLAIN";
        if (!requested_plain)
            throw std::invalid_argument("the AMQP 1.0 driver supports PLAIN in this probe");
        co_return json{{"tls_verified", runtime.connection.state() == protocol::connection_state::opened},
            {"sasl_mechanism", "PLAIN"}};
    }

    auto message_boundary_probe(connected_client& runtime, const json& parameters)
        -> cnetmod::task<json>
    {
        co_await connect_client(runtime, parameters);
        auto sender_session = co_await begin_session(runtime);
        auto receiver_session = co_await begin_session(runtime);
        const auto address = parameters.at("address").get<std::string>();
        auto sender =
            co_await make_sender(sender_session, address, runtime.cancellation);
        auto receiver = co_await make_receiver(
            receiver_session, address,
            static_cast<std::uint32_t>(parameters.at("bodies").size()),
            runtime.cancellation);
        bool fragmented = false;
        for (const auto& body : parameters.at("bodies").get_array())
        {
            const auto message = make_message(body, &parameters.at("application_properties"));
            fragmented = fragmented || protocol::encode_message(message).size() > 261888;
            require_value(co_await sender.send(message, {.settled = false},
                runtime.cancellation));
        }
        json round_trip = cnetmod::json::array();
        for (std::size_t index = 0; index < parameters.at("bodies").size(); ++index)
        {
            auto received = require_value(co_await receiver.receive(runtime.cancellation));
            round_trip.get_array().push_back(message_body_to_json(received.payload.body));
            if (!received.settled)
                require_success(co_await receiver.settle(
                    received.delivery_id, {.kind = protocol::outcome_kind::accepted},
                    runtime.cancellation));
        }
        co_return json{{"round_trip_bodies", std::move(round_trip)},
            {"transfers_fragmented_to_remote_limit", fragmented},
            {"connection_remained_open", runtime.connection.state() == protocol::connection_state::opened}};
    }

    auto transaction_coordinator_probe(connected_client& runtime,
        const json& parameters)
        -> cnetmod::task<json>
    {
        co_await connect_client(runtime, parameters);
        auto session = co_await begin_session(runtime);
        auto sender = co_await make_sender(session, parameters.at("address").get<std::string>(),
            runtime.cancellation);
        auto controller = require_value(session.make_transaction_controller());

        runtime.stage = "transaction declaration";
        auto commit_id = require_value(co_await controller.declare(runtime.cancellation));
        runtime.stage = "committed transactional transfers";
        std::vector<std::uint32_t> committed_delivery_ids;
        for (const auto& body : parameters.at("committed_bodies").get_array())
            committed_delivery_ids.push_back(require_value(
                co_await sender.begin_send(make_message(body),
                {.settled = false,
                    .transaction_id = commit_id},
                runtime.cancellation)));
        runtime.stage = "transaction commit";
        require_success(co_await controller.discharge(commit_id, false,
            runtime.cancellation));
        for (const auto delivery_id : committed_delivery_ids)
            require_value(co_await sender.await_outcome(
                delivery_id, runtime.cancellation));

        runtime.stage = "rollback transaction declaration";
        auto rollback_id = require_value(co_await controller.declare(runtime.cancellation));
        runtime.stage = "rolled-back transactional transfers";
        std::vector<std::uint32_t> rolled_back_delivery_ids;
        for (const auto& body : parameters.at("rolled_back_bodies").get_array())
            rolled_back_delivery_ids.push_back(require_value(
                co_await sender.begin_send(make_message(body),
                {.settled = false,
                    .transaction_id = rollback_id},
                runtime.cancellation)));
        runtime.stage = "transaction rollback";
        require_success(co_await controller.discharge(rollback_id, true,
            runtime.cancellation));
        for (const auto delivery_id : rolled_back_delivery_ids)
            require_value(co_await sender.await_outcome(
                delivery_id, runtime.cancellation));

        runtime.stage = "transaction verification session";
        auto receiver_session = co_await begin_session(runtime);
        auto receiver = co_await make_receiver(
            receiver_session, parameters.at("address").get<std::string>(),
            static_cast<std::uint32_t>(parameters.at("committed_bodies").size()),
            runtime.cancellation);
        json visible = cnetmod::json::array();
        for (std::size_t index = 0; index < parameters.at("committed_bodies").size();
            ++index)
        {
            auto delivery = require_value(co_await receiver.receive(runtime.cancellation));
            visible.get_array().push_back(message_body_to_json(delivery.payload.body));
            if (!delivery.settled)
                require_success(co_await receiver.settle(
                    delivery.delivery_id, {.kind = protocol::outcome_kind::accepted},
                    runtime.cancellation));
        }
        co_return json{{"declared_transaction", !commit_id.empty() && !rollback_id.empty()},
            {"discharged_commit", true},
            {"discharged_rollback", true},
            {"visible_bodies", std::move(visible)}};
    }

    auto sustained_unsettled_delivery_probe(connected_client& runtime,
        const json& parameters)
        -> cnetmod::task<json>
    {
        co_await connect_client(runtime, parameters);
        auto session = co_await begin_session(runtime);
        const auto address = parameters.at("address").get<std::string>();
        auto sender = co_await make_sender(session, address, runtime.cancellation);
        const auto count = parameters.at("delivery_count").as<std::uint32_t>();
        const auto window = parameters.at("unsettled_window").as<std::size_t>();
        if (window == 0)
            throw std::invalid_argument("unsettled_window must be greater than zero");
        std::uint32_t accepted = 0;
        std::uint32_t sent = 0;
        std::size_t maximum_pending = 0;
        std::deque<std::uint32_t> awaiting_outcomes;
        while (sent < count)
        {
            while (sent < count && sender.pending_unsettled_count() < window)
            {
                auto delivery_id = require_value(co_await sender.begin_send(
                    make_message(sent), {.settled = false}, runtime.cancellation));
                awaiting_outcomes.push_back(delivery_id);
                ++sent;
                maximum_pending =
                    std::max(maximum_pending, sender.pending_unsettled_count());
            }
            if (awaiting_outcomes.empty())
                throw std::runtime_error(
                    "unsettled sender window made no forward progress");
            auto result = require_value(co_await sender.await_outcome(
                awaiting_outcomes.front(), runtime.cancellation));
            awaiting_outcomes.pop_front();
            if (result.outcome.kind == protocol::outcome_kind::accepted)
                ++accepted;
        }
        while (!awaiting_outcomes.empty())
        {
            auto result = require_value(co_await sender.await_outcome(
                awaiting_outcomes.front(), runtime.cancellation));
            awaiting_outcomes.pop_front();
            if (result.outcome.kind == protocol::outcome_kind::accepted)
                ++accepted;
        }
        if (count >= window && maximum_pending < window)
            throw std::runtime_error(std::format(
                "broker credit limited the requested unsettled window: requested {}, observed {}",
                window, maximum_pending));

        const auto credit_batch = parameters.at("receiver_credit").as<std::uint32_t>();
        auto receiver_session = co_await begin_session(runtime);
        auto receiver = co_await make_receiver(receiver_session, address,
            std::min(count, credit_batch),
            runtime.cancellation);
        std::unordered_set<std::uint64_t> seen;
        std::uint32_t duplicates = 0;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            if (index != 0 && index % credit_batch == 0)
                require_success(co_await receiver.add_credit(
                    std::min(credit_batch, count - index), false, runtime.cancellation));
            auto delivery = require_value(co_await receiver.receive(runtime.cancellation));
            auto body = message_body_to_json(delivery.payload.body);
            const auto sequence = body.as<std::uint64_t>();
            if (!seen.insert(sequence).second)
                ++duplicates;
            if (!delivery.settled)
                require_success(co_await receiver.settle(
                    delivery.delivery_id, {.kind = protocol::outcome_kind::accepted},
                    runtime.cancellation));
        }
        co_return json{{"sent_count", count},
            {"accepted_count", accepted},
            {"remaining_unsettled_count", sender.pending_unsettled_count()},
            {"duplicate_delivery_count", duplicates}};
    }

    auto dispatch_operation(connected_client& runtime, std::string_view operation,
        const json& parameters) -> cnetmod::task<json>
    {
        if (operation == "send")
            co_return co_await send_operation(runtime, parameters);
        if (operation == "receive_one")
            co_return co_await receive_one_operation(runtime, parameters);
        if (operation == "link_credit_probe")
            co_return co_await link_credit_probe(runtime, parameters);
        if (operation == "delivery_outcome_probe")
            co_return co_await delivery_outcome_probe(runtime, parameters);
        if (operation == "reconnect_link_probe")
            co_return co_await reconnect_link_probe(runtime, parameters);
        if (operation == "connect_security_probe")
            co_return co_await connect_security_probe(runtime, parameters);
        if (operation == "message_boundary_probe")
            co_return co_await message_boundary_probe(runtime, parameters);
        if (operation == "transaction_coordinator_probe")
            co_return co_await transaction_coordinator_probe(runtime, parameters);
        if (operation == "sustained_unsettled_delivery_probe")
            co_return co_await sustained_unsettled_delivery_probe(runtime, parameters);
        throw std::invalid_argument(std::format("unsupported operation: {}", operation));
    }

    auto execute_request(const json& request) -> json
    {
        if (request.at("contract_version").as<int>() != 1)
            throw std::invalid_argument("unsupported contract_version");
        if (request.at("protocol").get<std::string>() != "amqp10")
            throw std::invalid_argument("request protocol must be amqp10");

        connected_client runtime(cnetmod::make_io_context());
        std::optional<json> result;
        std::exception_ptr failure;
        auto operation = [&]() -> cnetmod::task<void>
        {
            try
            {
                result = co_await dispatch_operation(
                    runtime, request.at("operation").get<std::string>(),
                    request.at("parameters"));
            }
            catch (...)
            {
                failure = std::current_exception();
            }
            runtime.context->stop();
        };
        auto watchdog = [&]() -> cnetmod::task<void>
        {
            const auto timeout = std::chrono::milliseconds{
                get_or(request.at("parameters"),
                    "operation_timeout_milliseconds", 30000)};
            auto waited = co_await cnetmod::async_timer_wait(
                *runtime.context, timeout, runtime.cancellation);
            if (waited && !runtime.cancellation.is_cancelled())
            {
                runtime.timed_out = true;
                runtime.cancellation.cancel();
            }
        };
        cnetmod::spawn(*runtime.context, operation());
        cnetmod::spawn(*runtime.context, watchdog());
        runtime.context->run();
        if (runtime.timed_out)
            throw std::runtime_error(std::format(
                "AMQP 1.0 operation timed out during {}", runtime.stage));
        if (failure)
            std::rethrow_exception(failure);
        if (!result)
            throw std::runtime_error("AMQP 1.0 operation completed without a result");
        return *result;
    }

} // namespace

auto run_json_lines(std::istream& input, std::ostream& output,
    std::ostream& diagnostics) -> int
{
    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty())
            continue;
        json response{{"contract_version", 1}};
        try
        {
            const auto parsed = cnetmod::json::parse_document(line);
            if (!parsed)
                throw std::runtime_error("invalid JSON request");
            response["status"] = "ok";
            response["result"] = execute_request(*parsed);
        }
        catch (const std::exception& error)
        {
            response["status"] = "error";
            response["error_code"] = "amqp10_driver_error";
            response["message"] = error.what();
            diagnostics << "AMQP 1.0 interop request failed: " << error.what() << '\n';
        }
        output << cnetmod::json::write_document(response).value_or("{}") << '\n';
        output.flush();
    }
    return 0;
}

} // namespace cnetmod::testing::messaging::amqp10
