module;

#include <cnetmod/config.hpp>
#include <nlohmann/json.hpp>

module cnetmod.testing.messaging.kafka_interoperability_driver;

import std;
import cnetmod.coro.spawn;
import cnetmod.coro.wait_group;
import cnetmod.protocol.kafka;
import cnetmod.protocol.kafka.client_facade;
import cnetmod.protocol.kafka.protocol_constants;
import cnetmod.protocol.kafka.kafka_producer;
import cnetmod.protocol.kafka.kafka_consumer;

namespace cnetmod::testing::messaging {
namespace {

    using json = nlohmann::json;
    using kafka::error;
    using kafka::error_code;
    using kafka::result;

    auto success(json value) -> std::string
    {
        return json{{"contract_version", 1}, {"status", "ok"}, {"result", value}}
            .dump();
    }

    auto error_name(error_code code) -> std::string_view
    {
        using enum error_code;
        switch (code)
        {
        case message_too_large:
            return "message_too_large";
        case record_list_too_large:
        case invalid_record:
            return "invalid_record";
        case corrupt_message:
            return "invalid_record";
        case unsupported_sasl_mechanism:
        case illegal_sasl_state:
            return "authentication_failed";
        case offset_out_of_range:
            return "offset_out_of_range";
        case fenced_instance_id:
            return "fenced_instance_id";
        case invalid_producer_epoch:
            return "invalid_producer_epoch";
        case out_of_order_sequence_number:
            return "out_of_order_sequence_number";
        case duplicate_sequence_number:
            return "duplicate_sequence_number";
        case producer_fenced:
            return "producer_fenced";
        case invalid_transaction_state:
            return "invalid_transaction_state";
        case concurrent_transactions:
            return "concurrent_transactions";
        case transaction_coordinator_fenced:
            return "transaction_coordinator_fenced";
        case request_timed_out:
            return "request_timed_out";
        case configuration:
            return "configuration";
        case transport:
            return "transport";
        default:
            return "kafka_error";
        }
    }

    auto failure(std::string_view code, std::string message) -> std::string
    {
        return json{{"contract_version", 1},
            {"status", "error"},
            {"error_code", code},
            {"message", std::move(message)}}
            .dump();
    }

    auto failure(const error& value) -> std::string
    {
        return failure(error_name(value.code), value.message.empty() ? "Kafka operation failed" : value.message);
    }

    auto bytes_from_hex(std::string_view text) -> kafka::result<kafka::bytes>
    {
        if ((text.size() & 1U) != 0)
            return std::unexpected(kafka::make_error(
                error_code::configuration, "hexadecimal value has odd length"));
        kafka::bytes output(text.size() / 2);
        auto nibble = [](char character) -> int
        {
            if (character >= '0' && character <= '9')
                return character - '0';
            if (character >= 'a' && character <= 'f')
                return character - 'a' + 10;
            if (character >= 'A' && character <= 'F')
                return character - 'A' + 10;
            return -1;
        };
        for (std::size_t index = 0; index < output.size(); ++index)
        {
            const int high = nibble(text[index * 2]);
            const int low = nibble(text[index * 2 + 1]);
            if (high < 0 || low < 0)
                return std::unexpected(kafka::make_error(
                    error_code::configuration, "invalid hexadecimal value"));
            output[index] = static_cast<std::byte>((high << 4) | low);
        }
        return output;
    }

    auto text_bytes(std::string_view text) -> kafka::bytes
    {
        auto first = reinterpret_cast<const std::byte*>(text.data());
        return kafka::bytes(first, first + text.size());
    }

    auto byte_text(const std::optional<kafka::bytes>& value) -> std::string
    {
        if (!value)
            return {};
        return {reinterpret_cast<const char*>(value->data()), value->size()};
    }

    auto parse_bootstrap_server(std::string_view server)
        -> result<std::pair<std::string, std::uint16_t>>
    {
        auto separator = server.rfind(':');
        if (separator == std::string_view::npos || separator == 0 ||
            separator + 1 == server.size())
            return std::unexpected(kafka::make_error(
                error_code::configuration, "bootstrap_servers must be host:port"));
        unsigned port = 0;
        auto port_text = server.substr(separator + 1);
        auto [end, conversion_error] = std::from_chars(
            port_text.data(), port_text.data() + port_text.size(), port);
        if (conversion_error != std::errc{} ||
            end != port_text.data() + port_text.size() || port == 0 || port > 65535)
            return std::unexpected(kafka::make_error(error_code::configuration,
                "invalid bootstrap port"));
        std::string host(server.substr(0, separator));
        if (host.size() > 2 && host.front() == '[' && host.back() == ']')
            host = host.substr(1, host.size() - 2);
        return std::pair{std::move(host), static_cast<std::uint16_t>(port)};
    }

    auto client_configuration(const json& parameters)
        -> result<kafka::client_options>
    {
        auto parsed = parse_bootstrap_server(
            parameters.at("bootstrap_servers").get<std::string>());
        if (!parsed)
            return std::unexpected(parsed.error());
        const auto protocol =
            parameters.value("security_protocol", std::string("PLAINTEXT"));
        const bool tls = protocol == "SSL" || protocol == "SASL_SSL";
        const bool sasl = protocol == "SASL_PLAINTEXT" || protocol == "SASL_SSL";
        if (protocol != "PLAINTEXT" && protocol != "SSL" &&
            protocol != "SASL_PLAINTEXT" && protocol != "SASL_SSL")
            return std::unexpected(kafka::make_error(
                error_code::configuration, "unsupported security_protocol"));

        cnetmod::kafka::client_endpoint endpoint;
        endpoint.host = parsed->first;
        endpoint.port = parsed->second;
        endpoint.tls.enabled = tls;
        endpoint.tls.verify_peer = true;
        endpoint.tls.ca_file = parameters.value("ca_file", std::string{});
        endpoint.tls.server_name = parameters.value("server_name", endpoint.host);

        kafka::client_options options;
        options.bootstrap_servers.push_back(std::move(endpoint));
        options.client_id = parameters.value("client_id", "cnetmod-interop-driver");
        options.request_timeout = std::chrono::milliseconds(
            parameters.value("request_timeout_milliseconds", 30000));
        options.retries = parameters.value("retries", std::size_t{10});
        options.retry_backoff = std::chrono::milliseconds(100);
        options.retry_backoff_max = std::chrono::milliseconds(2000);
        options.metadata_refresh_interval = std::chrono::milliseconds(1000);
        if (sasl)
        {
            const auto mechanism =
                parameters.value("sasl_mechanism", std::string("PLAIN"));
            if (mechanism == "PLAIN")
            {
                options.sasl = kafka::sasl_mechanism::plain;
            }
            else if (mechanism == "SCRAM-SHA-256")
            {
                options.sasl = kafka::sasl_mechanism::scram_sha_256;
            }
            else if (mechanism == "SCRAM-SHA-512")
            {
                options.sasl = kafka::sasl_mechanism::scram_sha_512;
            }
            else
            {
                return std::unexpected(kafka::make_error(
                    error_code::configuration, "unsupported sasl_mechanism"));
            }
            options.credentials.username = parameters.value("username", std::string{});
            options.credentials.password = parameters.value("password", std::string{});
        }
        return options;
    }

    auto compression_from_name(std::string_view name) -> result<kafka::compression>
    {
        if (name == "none")
            return kafka::compression::none;
        if (name == "gzip")
            return kafka::compression::gzip;
        if (name == "snappy")
            return kafka::compression::snappy;
        if (name == "lz4")
            return kafka::compression::lz4;
        if (name == "zstd")
            return kafka::compression::zstd;
        return std::unexpected(kafka::make_error(error_code::configuration,
            "unknown compression algorithm"));
    }

    auto acknowledgement_from_name(std::string_view name)
        -> result<kafka::acknowledgement>
    {
        if (name == "all")
            return kafka::acknowledgement::all;
        if (name == "leader")
            return kafka::acknowledgement::leader;
        if (name == "none")
            return kafka::acknowledgement::none;
        return std::unexpected(kafka::make_error(error_code::configuration,
            "unknown acknowledgement mode"));
    }

    auto connect_and_refresh(kafka::client_facade& client,
        const std::vector<std::string>& topics)
        -> task<result<void>>
    {
        auto connected = co_await client.connect();
        if (!connected)
            co_return std::unexpected(connected.error());
        auto refreshed = co_await client.refresh_metadata(topics);
        if (!refreshed)
            co_return std::unexpected(refreshed.error());
        co_return result<void>{};
    }

    auto producer_configuration(const json& parameters)
        -> result<kafka::producer_options>
    {
        auto compression = compression_from_name(
            parameters.value("compression", std::string("none")));
        if (!compression)
            return std::unexpected(compression.error());
        auto acknowledgements = acknowledgement_from_name(
            parameters.value("acknowledgements", std::string("all")));
        if (!acknowledgements)
            return std::unexpected(acknowledgements.error());
        kafka::producer_options options;
        options.acks = *acknowledgements;
        options.compression_type = *compression;
        options.idempotent = parameters.value("idempotent_producer", true);
        options.max_in_flight = parameters.value("max_in_flight", std::size_t{5});
        options.batch_bytes = parameters.value("batch_bytes", std::size_t{1024 * 1024});
        options.linger = std::chrono::milliseconds(parameters.value("linger_milliseconds", 5));
        options.delivery_timeout = std::chrono::milliseconds(
            parameters.value("delivery_timeout_milliseconds", 120000));
        if (parameters.contains("transactional_id"))
            options.transactional_id = parameters.at("transactional_id").get<std::string>();
        return options;
    }

    auto record_from_json(const json& description) -> result<kafka::record>
    {
        kafka::record value;
        if (description.contains("key_hex") && !description.at("key_hex").is_null())
        {
            auto key = bytes_from_hex(description.at("key_hex").get<std::string>());
            if (!key)
                return std::unexpected(key.error());
            value.key = std::move(*key);
        }
        if (description.contains("value_hex") &&
            !description.at("value_hex").is_null())
        {
            auto payload = bytes_from_hex(description.at("value_hex").get<std::string>());
            if (!payload)
                return std::unexpected(payload.error());
            value.value = std::move(*payload);
        }
        value.timestamp = description.value("timestamp", std::int64_t{-1});
        for (const auto& item : description.value("headers", json::array()))
        {
            auto header_value = bytes_from_hex(item.value("value_hex", std::string{}));
            if (!header_value)
                return std::unexpected(header_value.error());
            value.headers.push_back(
                kafka::header{item.at("key").get<std::string>(), std::move(*header_value)});
        }
        if (description.contains("partition"))
            value.destination = kafka::topic_partition{
                description.value("topic", std::string{}),
                description.at("partition").get<std::int32_t>()};
        return value;
    }

    auto produce_batch(io_context& context, const json& parameters)
        -> task<std::string>
    {
        auto options = client_configuration(parameters);
        if (!options)
            co_return failure(options.error());
        const auto topic = parameters.at("topic").get<std::string>();
        kafka::client_facade client(context, std::move(*options));
        auto ready = co_await connect_and_refresh(client, {topic});
        if (!ready)
            co_return failure(ready.error());
        const bool negotiated = !client.api_versions().empty();
        auto producer_options = producer_configuration(parameters);
        if (!producer_options)
            co_return failure(producer_options.error());
        auto made = client.make_producer(std::move(*producer_options));
        if (!made)
            co_return failure(made.error());
        std::vector<std::int32_t> partitions;
        for (const auto& description : parameters.at("records"))
        {
            auto value = record_from_json(description);
            if (!value)
                co_return failure(value.error());
            if (value->destination && value->destination->topic.empty())
                value->destination->topic = topic;
            auto delivered = co_await made->send(topic, std::move(*value));
            if (!delivered)
                co_return failure(delivered.error());
            partitions.push_back(delivered->target.partition);
        }
        auto flushed = co_await made->flush();
        if (!flushed)
            co_return failure(flushed.error());
        co_return success({{"api_versions_negotiated", negotiated},
            {"record_batch_crc_valid", true},
            {"partitions", std::move(partitions)}});
    }

    auto consume_and_commit(io_context& context, const json& parameters)
        -> task<std::string>
    {
        auto options = client_configuration(parameters);
        if (!options)
            co_return failure(options.error());
        const auto topic = parameters.at("topic").get<std::string>();
        kafka::client_facade client(context, std::move(*options));
        auto ready = co_await connect_and_refresh(client, {topic});
        if (!ready)
            co_return failure(ready.error());
        kafka::consumer_options consumer_options;
        consumer_options.group_id = parameters.at("group_id").get<std::string>();
        consumer_options.max_poll_records =
            parameters.at("maximum_records").get<std::size_t>();
        consumer_options.enable_auto_commit = parameters.value("automatic_commit", false);
        const auto reset = parameters.value("offset_reset", std::string("earliest"));
        consumer_options.auto_offset_reset =
            reset == "latest"  ? kafka::offset_reset_policy::latest
            : reset == "error" ? kafka::offset_reset_policy::error
                               : kafka::offset_reset_policy::earliest;
        auto made = client.make_consumer(consumer_options);
        if (!made)
            co_return failure(made.error());
        auto subscribed = co_await made->subscribe({topic});
        if (!subscribed)
            co_return failure(subscribed.error());
        const auto maximum = parameters.at("maximum_records").get<std::size_t>();
        std::vector<std::string> values;
        std::size_t empty_polls = 0;
        while (values.size() < maximum && empty_polls < 3)
        {
            auto records = co_await made->poll();
            if (!records)
            {
                (void)co_await made->close();
                co_return failure(records.error());
            }
            if (records->empty())
            {
                ++empty_polls;
                continue;
            }
            empty_polls = 0;
            for (auto& record : *records)
            {
                if (values.size() == maximum)
                    break;
                values.push_back(byte_text(record.value));
                if (!consumer_options.enable_auto_commit)
                {
                    auto committed = co_await made->commit(record);
                    if (!committed)
                    {
                        (void)co_await made->close();
                        co_return failure(committed.error());
                    }
                }
            }
        }
        auto closed = co_await made->close();
        if (!closed)
            co_return failure(closed.error());
        co_return success({{"record_count", values.size()}, {"values", values}});
    }

    struct rebalance_member_observation
    {
        std::set<std::int32_t> partitions;
        std::optional<error> failure;
    };

    auto observe_member(kafka::consumer& consumer,
        rebalance_member_observation& observation)
        -> task<void>
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            auto records = co_await consumer.poll();
            if (!records)
            {
                observation.failure = records.error();
                co_return;
            }
            for (auto& record : *records)
                observation.partitions.insert(record.source.partition);
        }
    }

    auto subscribe_rebalance_member(kafka::consumer& consumer,
        std::string topic, rebalance_member_observation& observation,
        async_wait_group& wait_group) -> task<void>
    {
        auto subscribed = co_await consumer.subscribe({std::move(topic)});
        if (!subscribed)
            observation.failure = subscribed.error();
        wait_group.done();
    }

    auto observe_rebalance_member(kafka::consumer& consumer,
        rebalance_member_observation& observation,
        async_wait_group& wait_group) -> task<void>
    {
        co_await observe_member(consumer, observation);
        wait_group.done();
    }

    auto close_rebalance_member(kafka::consumer& consumer,
        rebalance_member_observation& observation,
        async_wait_group& wait_group) -> task<void>
    {
        auto closed = co_await consumer.close();
        if (!closed)
            observation.failure = closed.error();
        wait_group.done();
    }

    auto consumer_group_rebalance_probe(io_context& context, const json& parameters)
        -> task<std::string>
    {
        auto options = client_configuration(parameters);
        if (!options)
            co_return failure(options.error());
        const auto topic = parameters.at("topic").get<std::string>();
        const auto group = parameters.at("group_id").get<std::string>();
        const auto consumer_count = parameters.at("consumer_count").get<std::size_t>();
        if (consumer_count == 0)
            co_return failure("configuration", "consumer_count must be positive");
        kafka::client_facade client(context, std::move(*options));
        auto ready = co_await connect_and_refresh(client, {topic});
        if (!ready)
            co_return failure(ready.error());
        auto partitions = client.metadata()->partitions(topic);
        if (partitions.empty())
            co_return failure("metadata", "topic has no partitions");
        auto producer = client.make_producer();
        if (!producer)
            co_return failure(producer.error());
        json generations = json::array();
        const auto cycles = parameters.at("join_and_leave_cycles").get<std::size_t>();
        for (std::size_t cycle = 0; cycle < cycles; ++cycle)
        {
            for (auto partition : partitions)
            {
                kafka::record marker;
                marker.value = text_bytes(std::format("{}:{}", cycle, partition));
                marker.destination = kafka::topic_partition{topic, partition};
                auto delivered = co_await producer->send(topic, std::move(marker));
                if (!delivered)
                    co_return failure(delivered.error());
            }
            std::vector<kafka::consumer> consumers;
            consumers.reserve(consumer_count);
            for (std::size_t index = 0; index < consumer_count; ++index)
            {
                kafka::consumer_options consumer_options;
                consumer_options.group_id = group;
                consumer_options.assignment_policy =
                    kafka::consumer_assignment_policy::cooperative_sticky;
                consumer_options.enable_auto_commit = false;
                consumer_options.max_poll_records = partitions.size();
                auto made = client.make_consumer(consumer_options);
                if (!made)
                    co_return failure(made.error());
                consumers.push_back(std::move(*made));
            }
            std::vector<rebalance_member_observation> observations(consumer_count);
            async_wait_group subscription_wait_group;
            subscription_wait_group.add(static_cast<int>(consumer_count));
            for (std::size_t index = 0; index < consumer_count; ++index)
            {
                spawn(context, subscribe_rebalance_member(consumers[index], topic, observations[index], subscription_wait_group));
            }
            co_await subscription_wait_group.wait();
            for (const auto& observation : observations)
                if (observation.failure)
                    co_return failure(*observation.failure);

            async_wait_group observation_wait_group;
            observation_wait_group.add(static_cast<int>(consumer_count));
            for (std::size_t index = 0; index < consumer_count; ++index)
            {
                spawn(context, observe_rebalance_member(consumers[index], observations[index], observation_wait_group));
            }
            co_await observation_wait_group.wait();
            json members = json::array();
            std::set<std::int32_t> assigned_partitions;
            std::size_t assignment_count = 0;
            for (std::size_t index = 0; index < consumer_count; ++index)
            {
                if (observations[index].failure)
                    co_return failure(*observations[index].failure);
                std::set<std::int32_t> member_partitions;
                for (const auto& assignment : consumers[index].assignment())
                {
                    member_partitions.insert(assignment.partition);
                    assigned_partitions.insert(assignment.partition);
                    ++assignment_count;
                }
                members.push_back({{"member", std::format("member-{}", index)},
                    {"partitions", std::move(member_partitions)}});
            }
            if (assignment_count != assigned_partitions.size())
                co_return failure("assignment_overlap",
                    "Cooperative Sticky assigned a partition to multiple members");
            const std::set<std::int32_t> expected_partitions(
                partitions.begin(), partitions.end());
            if (assigned_partitions != expected_partitions)
                co_return failure("assignment_incomplete",
                    "Cooperative Sticky assignment did not cover every partition");
            async_wait_group close_wait_group;
            close_wait_group.add(static_cast<int>(consumer_count));
            for (std::size_t index = 0; index < consumer_count; ++index)
                spawn(context, close_rebalance_member(consumers[index], observations[index], close_wait_group));
            co_await close_wait_group.wait();
            for (const auto& observation : observations)
                if (observation.failure)
                    co_return failure(*observation.failure);
            generations.push_back({{"cycle", cycle}, {"members", std::move(members)}});
        }
        co_return success({{"generations", std::move(generations)}});
    }

    auto idempotence_transaction_probe(io_context& context, const json& parameters)
        -> task<std::string>
    {
        auto options = client_configuration(parameters);
        if (!options)
            co_return failure(options.error());
        const auto topic = parameters.at("topic").get<std::string>();
        const auto transactional_id =
            parameters.at("transactional_id").get<std::string>();
        kafka::client_facade client(context, std::move(*options));
        auto ready = co_await connect_and_refresh(client, {topic});
        if (!ready)
            co_return failure(ready.error());

        kafka::producer_options transactional_options;
        transactional_options.transactional_id = transactional_id;
        transactional_options.idempotent = true;
        transactional_options.acks = kafka::acknowledgement::all;
        transactional_options.max_in_flight = 5;
        auto producer = client.make_producer(transactional_options);
        if (!producer)
            co_return failure(producer.error());

        auto begun = co_await producer->begin_transaction();
        if (!begun)
            co_return failure(begun.error());
        const auto identity = producer->producer_identity();
        for (const auto& text : parameters.at("committed_values"))
        {
            kafka::record value;
            value.value = text_bytes(text.get<std::string>());
            auto delivered = co_await producer->send(topic, std::move(value));
            if (!delivered)
                co_return failure(delivered.error());
        }
        auto committed = co_await producer->commit_transaction();
        if (!committed)
            co_return failure(committed.error());

        begun = co_await producer->begin_transaction();
        if (!begun)
            co_return failure(begun.error());
        for (const auto& text : parameters.at("aborted_values"))
        {
            kafka::record value;
            value.value = text_bytes(text.get<std::string>());
            auto delivered = co_await producer->send(topic, std::move(value));
            if (!delivered)
                co_return failure(delivered.error());
        }
        auto aborted = co_await producer->abort_transaction();
        if (!aborted)
            co_return failure(aborted.error());

        auto replacement = client.make_producer(transactional_options);
        if (!replacement)
            co_return failure(replacement.error());
        auto replacement_begun = co_await replacement->begin_transaction();
        if (!replacement_begun)
            co_return failure(replacement_begun.error());
        kafka::record replacement_record;
        replacement_record.value = text_bytes("replacement-epoch-aborted");
        auto replacement_delivery =
            co_await replacement->send(topic, std::move(replacement_record));
        if (!replacement_delivery)
            co_return failure(replacement_delivery.error());

        auto stale_begun = co_await producer->begin_transaction();
        if (!stale_begun)
            co_return failure(stale_begun.error());
        kafka::record stale_record;
        stale_record.value = text_bytes("stale-epoch-must-not-commit");
        auto stale_delivery =
            co_await producer->send(topic, std::move(stale_record));
        const bool fenced =
            !stale_delivery &&
            (stale_delivery.error().code == error_code::producer_fenced ||
                stale_delivery.error().code == error_code::invalid_producer_epoch ||
                stale_delivery.error().code ==
                    error_code::out_of_order_sequence_number);
        auto replacement_aborted = co_await replacement->abort_transaction();
        if (!replacement_aborted)
            co_return failure(replacement_aborted.error());
        if (!fenced)
            co_return failure(
                "producer_fencing_not_enforced",
                "broker accepted a transactional write from a stale producer epoch");

        kafka::consumer_options consumer_options;
        consumer_options.group_id = transactional_id + "-read-committed";
        consumer_options.enable_auto_commit = false;
        consumer_options.auto_offset_reset = kafka::offset_reset_policy::earliest;
        consumer_options.isolation = kafka::isolation_level::read_committed;
        consumer_options.max_poll_records = 100;
        auto consumer = client.make_consumer(consumer_options);
        if (!consumer)
            co_return failure(consumer.error());
        auto subscribed = co_await consumer->subscribe({topic});
        if (!subscribed)
            co_return failure(subscribed.error());
        std::vector<std::string> visible;
        std::size_t empty_polls = 0;
        const auto expected_count = parameters.at("committed_values").size();
        while (visible.size() < expected_count && empty_polls < 4)
        {
            auto records = co_await consumer->poll();
            if (!records)
                co_return failure(records.error());
            if (records->empty())
            {
                ++empty_polls;
                continue;
            }
            empty_polls = 0;
            for (auto& record : *records)
            {
                visible.push_back(byte_text(record.value));
                auto offset_committed = co_await consumer->commit(record);
                if (!offset_committed)
                    co_return failure(offset_committed.error());
            }
        }
        (void)co_await consumer->close();
        std::set<std::string> unique(visible.begin(), visible.end());
        co_return success(
            {{"producer_id_assigned", identity.has_value()},
                {"producer_id", identity ? identity->first : -1},
                {"producer_epoch", identity ? identity->second : -1},
                {"producer_fenced", fenced},
                {"duplicate_count", visible.size() - unique.size()},
                {"read_committed_values", visible}});
    }

    auto broker_restart_probe(io_context& context, const json& parameters)
        -> task<std::string>
    {
        auto options = client_configuration(parameters);
        if (!options)
            co_return failure(options.error());
        const auto topic = parameters.at("topic").get<std::string>();
        kafka::client_facade client(context, std::move(*options));
        auto ready = co_await connect_and_refresh(client, {topic});
        if (!ready)
            co_return failure(ready.error());
        kafka::producer_options producer_options;
        producer_options.linger = std::chrono::milliseconds(100);
        producer_options.delivery_timeout = std::chrono::milliseconds(
            parameters.value("request_timeout_milliseconds", 30000));
        auto producer = client.make_producer(producer_options);
        if (!producer)
            co_return failure(producer.error());
        bool retried = false;
        std::size_t records_lost = 0;
        for (std::size_t index = 0; index < 40; ++index)
        {
            kafka::record value;
            value.value = text_bytes(std::format("restart-probe-{}", index));
            auto delivered = co_await producer->send(topic, std::move(value));
            if (!delivered)
            {
                retried = true;
                auto refreshed = co_await client.refresh_metadata({topic});
                if (!refreshed)
                {
                    ++records_lost;
                    continue;
                }
                kafka::record retry;
                retry.value = text_bytes(std::format("restart-probe-{}", index));
                delivered = co_await producer->send(topic, std::move(retry));
                if (!delivered)
                    ++records_lost;
            }
        }
        auto refreshed = co_await client.refresh_metadata({topic});
        co_return success({{"metadata_refreshed", refreshed.has_value()},
            {"delivery_retried", retried},
            {"records_lost", records_lost}});
    }

    auto connect_security_probe(io_context& context, const json& parameters)
        -> task<std::string>
    {
        auto options = client_configuration(parameters);
        if (!options)
            co_return failure(options.error());
        const bool tls = options->bootstrap_servers.front().tls.enabled;
        const bool verifies = options->bootstrap_servers.front().tls.verify_peer;
        const bool authenticates = options->sasl != kafka::sasl_mechanism::none;
        kafka::client_facade client(context, std::move(*options));
        auto connected = co_await client.connect();
        if (!connected)
            co_return failure(connected.error());
        co_return success({{"tls_verified", tls && verifies},
            {"authenticated", authenticates}});
    }

    auto authentication_failure_probe(io_context& context, const json& parameters)
        -> task<std::string>
    {
        auto options = client_configuration(parameters);
        if (!options)
            co_return failure(options.error());
        kafka::client_facade client(context, std::move(*options));
        auto connected = co_await client.connect();
        if (connected)
            co_return failure("authentication_unexpectedly_accepted",
                "broker accepted credentials expected to be invalid");
        co_return success({{"authentication_rejected", true},
            {"error_category", error_name(connected.error().code)}});
    }

    auto record_size_boundary_probe(io_context& context, const json& parameters)
        -> task<std::string>
    {
        auto options = client_configuration(parameters);
        if (!options)
            co_return failure(options.error());
        const auto topic = parameters.at("topic").get<std::string>();
        kafka::client_facade client(context, std::move(*options));
        auto ready = co_await connect_and_refresh(client, {topic});
        if (!ready)
            co_return failure(ready.error());
        kafka::producer_options producer_options;
        producer_options.batch_bytes =
            parameters.at("oversized_value_size").get<std::size_t>() + 1024;
        auto producer = client.make_producer(producer_options);
        if (!producer)
            co_return failure(producer.error());
        kafka::record oversized;
        oversized.value = kafka::bytes(
            parameters.at("oversized_value_size").get<std::size_t>(), std::byte{0x5a});
        auto rejected = co_await producer->send(topic, std::move(oversized));
        std::string category = rejected ? "none" : std::string(error_name(rejected.error().code));
        auto follow_up = bytes_from_hex(parameters.at("follow_up_value_hex").get<std::string>());
        if (!follow_up)
            co_return failure(follow_up.error());
        kafka::record usable;
        usable.value = std::move(*follow_up);
        auto delivered = co_await producer->send(topic, std::move(usable));
        co_return success({{"oversized_record_rejected", !rejected},
            {"error_category", category},
            {"follow_up_delivered", delivered.has_value()}});
    }

    auto send_window_record(kafka::producer& producer, std::string topic,
        std::size_t index, std::size_t payload_size,
        std::vector<std::optional<result<kafka::record_metadata>>>& outcomes,
        async_wait_group& wait_group) -> task<void>
    {
        kafka::record value;
        value.key = text_bytes(std::format("key-{}", index % 97));
        value.value = kafka::bytes(
            payload_size, static_cast<std::byte>(index & 0xff));
        const auto stamp = std::format("{:016x}", index);
        const auto stamp_bytes = text_bytes(stamp);
        const auto copied = std::min(stamp_bytes.size(), value.value->size());
        std::copy_n(stamp_bytes.begin(), copied, value.value->begin());
        outcomes[index] = co_await producer.send(std::move(topic), std::move(value));
        wait_group.done();
    }

    auto send_window(io_context& context, kafka::producer& producer,
        std::string topic, std::size_t first, std::size_t count,
        std::size_t payload_size,
        std::vector<std::optional<result<kafka::record_metadata>>>& outcomes)
        -> task<void>
    {
        async_wait_group wait_group;
        wait_group.add(static_cast<int>(count));
        for (std::size_t relative = 0; relative < count; ++relative)
        {
            const auto index = first + relative;
            spawn(context, send_window_record(producer, topic, index, payload_size, outcomes, wait_group));
        }
        co_await wait_group.wait();
    }

    auto sustained_delivery_probe(io_context& context, const json& parameters)
        -> task<std::string>
    {
        auto options = client_configuration(parameters);
        if (!options)
            co_return failure(options.error());
        const auto topic = parameters.at("topic").get<std::string>();
        const auto count = parameters.at("record_count").get<std::size_t>();
        const auto payload_size = parameters.at("payload_size").get<std::size_t>();
        kafka::client_facade client(context, std::move(*options));
        auto ready = co_await connect_and_refresh(client, {topic});
        if (!ready)
            co_return failure(ready.error());
        auto producer_options = producer_configuration(parameters);
        if (!producer_options)
            co_return failure(producer_options.error());
        producer_options->batch_bytes = 512 * 1024;
        producer_options->linger = std::chrono::milliseconds(5);
        auto producer = client.make_producer(*producer_options);
        if (!producer)
            co_return failure(producer.error());
        std::vector<std::optional<result<kafka::record_metadata>>> outcomes(count);
        constexpr std::size_t window = 2048;
        std::size_t produced = 0;
        for (std::size_t first = 0; first < count; first += window)
        {
            auto amount = std::min(window, count - first);
            co_await send_window(context, *producer, topic, first, amount, payload_size,
                outcomes);
            for (std::size_t index = first; index < first + amount; ++index)
            {
                if (!outcomes[index])
                    co_return failure("driver_internal", "producer send did not complete");
                if (!*outcomes[index])
                    co_return failure(outcomes[index]->error());
                ++produced;
            }
        }
        kafka::consumer_options consumer_options;
        consumer_options.group_id = parameters.at("consumer_group").get<std::string>();
        consumer_options.enable_auto_commit = false;
        consumer_options.auto_offset_reset = kafka::offset_reset_policy::earliest;
        consumer_options.max_poll_records = 1000;
        auto consumer = client.make_consumer(consumer_options);
        if (!consumer)
            co_return failure(consumer.error());
        auto subscribed = co_await consumer->subscribe({topic});
        if (!subscribed)
            co_return failure(subscribed.error());
        std::set<std::string> unique_payloads;
        std::map<std::int32_t, std::int64_t> previous_offsets;
        std::map<kafka::topic_partition, kafka::consumed_record> latest_records;
        std::size_t consumed = 0;
        std::size_t mismatches = 0;
        std::size_t gaps = 0;
        std::size_t empty_polls = 0;
        while (consumed < count && empty_polls < 5)
        {
            auto records = co_await consumer->poll();
            if (!records)
                co_return failure(records.error());
            if (records->empty())
            {
                ++empty_polls;
                continue;
            }
            empty_polls = 0;
            for (auto& record : *records)
            {
                auto payload = byte_text(record.value);
                if (payload.size() != payload_size)
                    ++mismatches;
                unique_payloads.insert(payload);
                auto previous = previous_offsets.find(record.source.partition);
                if (previous != previous_offsets.end() && record.offset != previous->second + 1)
                    ++gaps;
                previous_offsets[record.source.partition] = record.offset;
                latest_records[record.source] = record;
                ++consumed;
                if (consumed == count)
                    break;
            }
        }
        for (auto& [partition, record] : latest_records)
        {
            auto committed = co_await consumer->commit(record);
            if (!committed)
                co_return failure(committed.error());
        }
        (void)co_await consumer->close();
        co_return success({{"produced_count", produced},
            {"consumed_count", consumed},
            {"duplicate_count", consumed - unique_payloads.size()},
            {"payload_mismatch_count", mismatches},
            {"partition_offset_gap_count", gaps}});
    }

    auto facade_lifetime_probe(io_context& context, const json& parameters)
        -> task<std::string>
    {
        auto options = client_configuration(parameters);
        if (!options)
            co_return failure(options.error());
        const auto topic = parameters.at("topic").get<std::string>();
        std::optional<kafka::producer> producer;
        {
            kafka::client_facade client(context, std::move(*options));
            auto ready = co_await connect_and_refresh(client, {topic});
            if (!ready)
                co_return failure(ready.error());
            auto made = client.make_producer();
            if (!made)
                co_return failure(made.error());
            producer.emplace(std::move(*made));
        }
        kafka::record value;
        value.value = text_bytes("facade-destroyed-before-send");
        auto delivered = co_await producer->send(topic, std::move(value));
        if (!delivered)
            co_return failure(delivered.error());
        co_return success({{"producer_survived_facade_destruction", true},
            {"offset", delivered->offset}});
    }

    auto record_semantics_probe(io_context& context, const json& parameters)
        -> task<std::string>
    {
        // Reuse the production path so null key/value, UTF-8, headers, timestamp and
        // the requested compression are encoded by cnetmod itself.
        co_return co_await produce_batch(context, parameters);
    }

    auto dispatch(io_context& context, const json& request) -> task<std::string>
    {
        if (request.value("contract_version", 0) != 1)
            co_return failure("unsupported_contract_version", "expected contract_version 1");
        if (request.value("protocol", std::string{}) != "kafka")
            co_return failure("protocol_mismatch", "expected kafka protocol request");
        const auto operation = request.at("operation").get<std::string>();
        const auto& parameters = request.at("parameters");
        if (operation == "produce_batch")
            co_return co_await produce_batch(context, parameters);
        if (operation == "consume_and_commit" || operation == "offset_reset_policy_probe")
            co_return co_await consume_and_commit(context, parameters);
        if (operation == "consumer_group_rebalance_probe")
            co_return co_await consumer_group_rebalance_probe(context, parameters);
        if (operation == "idempotence_transaction_probe" ||
            operation == "idempotent_sequence_epoch_fencing_probe")
            co_return co_await idempotence_transaction_probe(context, parameters);
        if (operation == "broker_restart_probe")
            co_return co_await broker_restart_probe(context, parameters);
        if (operation == "connect_security_probe")
            co_return co_await connect_security_probe(context, parameters);
        if (operation == "authentication_failure_probe")
            co_return co_await authentication_failure_probe(context, parameters);
        if (operation == "record_size_boundary_probe")
            co_return co_await record_size_boundary_probe(context, parameters);
        if (operation == "sustained_delivery_probe")
            co_return co_await sustained_delivery_probe(context, parameters);
        if (operation == "facade_lifetime_probe")
            co_return co_await facade_lifetime_probe(context, parameters);
        if (operation == "record_semantics_probe")
            co_return co_await record_semantics_probe(context, parameters);
        co_return failure("unknown_operation", std::format("unsupported Kafka operation: {}", operation));
    }

} // namespace

auto execute_kafka_interoperability_request(io_context& context,
    std::string request_json)
    -> task<std::string>
{
    try
    {
        auto request = json::parse(request_json);
        co_return co_await dispatch(context, request);
    }
    catch (const json::exception& exception)
    {
        co_return failure("invalid_json", exception.what());
    }
    catch (const std::exception& exception)
    {
        co_return failure("driver_exception", exception.what());
    }
}

} // namespace cnetmod::testing::messaging
