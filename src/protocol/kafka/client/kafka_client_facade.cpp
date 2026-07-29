module cnetmod.protocol.kafka.client_facade;
import std;
import cnetmod.protocol.kafka.protocol_value_codec;
import cnetmod.protocol.kafka.record_batch;
import cnetmod.protocol.kafka.group_coordinator;
import cnetmod.protocol.kafka.offset_manager;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.coro.mutex;
import cnetmod.executor.async_op;

namespace cnetmod::kafka {
namespace {
    constexpr auto leave_group_completed(error_code code) noexcept -> bool
    {
        return code == error_code::none ||
            code == error_code::rebalance_in_progress ||
            code == error_code::illegal_generation ||
            code == error_code::unknown_member_id;
    }

    static_assert(leave_group_completed(error_code::rebalance_in_progress));
    static_assert(leave_group_completed(error_code::illegal_generation));
    static_assert(leave_group_completed(error_code::unknown_member_id));
    static_assert(!leave_group_completed(error_code::fenced_instance_id));

    constexpr auto group_error_resets_member(error_code code) noexcept -> bool
    {
        return code == error_code::illegal_generation ||
            code == error_code::unknown_member_id;
    }

    static_assert(group_error_resets_member(error_code::illegal_generation));
    static_assert(group_error_resets_member(error_code::unknown_member_id));
    static_assert(!group_error_resets_member(error_code::rebalance_in_progress));
    static_assert(!group_error_resets_member(error_code::fenced_instance_id));

    struct consumer_group_api_versions
    {
        std::int16_t find_coordinator = 2;
        std::int16_t join_group = 5;
        std::int16_t sync_group = 3;
        std::int16_t heartbeat = 3;
        std::int16_t leave_group = 3;
        std::int16_t offset_fetch = 5;
        std::int16_t offset_commit = 7;
        std::int16_t fetch = 11;
        std::int16_t list_offsets = 0;
    };

    auto request_with_cancel(broker_connection& c, protocol::api_key key,
        std::int16_t version, std::span<const std::byte> body,
        cancel_token* token) -> task<result<bytes>>
    {
        if (token)
            co_return co_await c.request(key, version, body, *token);
        co_return co_await c.request(key, version, body);
    }

    enum class kafka_retry_recovery
    {
        none,
        refresh_metadata,
        rediscover_coordinator
    };

    struct kafka_retry_decision
    {
        kafka_retry_recovery recovery = kafka_retry_recovery::none;
        std::chrono::milliseconds delay{};
    };

    class kafka_request_retry_state_machine
    {
    public:
        kafka_request_retry_state_machine(io_context& context, client_options options)
            : context_(context), options_(std::move(options)) {}

        auto decide(const error& failure, std::size_t failed_attempt,
            std::chrono::steady_clock::time_point deadline) const
            -> result<kafka_retry_decision>
        {
            if (!failure.retriable || failed_attempt >= options_.retries)
                return std::unexpected(failure);
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
                return std::unexpected(make_error(error_code::request_timed_out,
                    "Kafka retry deadline expired"));
            auto shift = std::min<std::size_t>(failed_attempt, 20);
            auto multiplier = std::int64_t{1} << shift;
            auto capped_base = std::min(options_.retry_backoff.count(),
                options_.retry_backoff_max.count());
            auto capped_count =
                capped_base > 0 &&
                    multiplier > options_.retry_backoff_max.count() / capped_base
                ? options_.retry_backoff_max.count()
                : capped_base * multiplier;
            auto delay = std::min(std::chrono::milliseconds{capped_count},
                options_.retry_backoff_max);
            delay = std::min(
                delay,
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
            kafka_retry_recovery recovery = kafka_retry_recovery::none;
            switch (failure.code)
            {
            case error_code::unknown_topic_or_partition:
            case error_code::leader_not_available:
            case error_code::not_leader_or_follower:
            case error_code::transport:
                recovery = kafka_retry_recovery::refresh_metadata;
                break;
            case error_code::coordinator_load_in_progress:
            case error_code::coordinator_not_available:
            case error_code::not_coordinator:
                recovery = kafka_retry_recovery::rediscover_coordinator;
                break;
            default:
                break;
            }
            return kafka_retry_decision{recovery, delay};
        }

        auto wait(const kafka_retry_decision& decision, cancel_token* token)
            -> task<result<void>>
        {
            if (decision.delay <= std::chrono::milliseconds::zero())
                co_return result<void>{};
            std::expected<void, std::error_code> waited;
            if (token)
                waited = co_await async_timer_wait(context_, decision.delay, *token);
            else
                waited = co_await async_timer_wait(context_, decision.delay);
            if (!waited)
            {
                auto code =
                    waited.error() == std::make_error_code(std::errc::operation_canceled)
                    ? error_code::cancelled
                    : error_code::transport;
                co_return std::unexpected(make_error(code, waited.error().message()));
            }
            co_return result<void>{};
        }

    private:
        io_context& context_;
        client_options options_;
    };

    using metadata_refresh_operation = std::function<task<result<void>>(
        std::vector<std::string>, bool, cancel_token*)>;

    class facade_producer_backend final : public producer_backend
    {
    public:
        facade_producer_backend(
            io_context& context, std::shared_ptr<metadata_cache> metadata,
            std::function<broker_connection*(std::int32_t)> lookup,
            std::function<broker_connection*(const broker_endpoint&)> resolve,
            std::function<broker_connection*()> seed,
            metadata_refresh_operation refresh_metadata,
            client_options client_configuration, compression_registry codecs,
            std::int16_t version, std::int16_t init_version,
            std::int16_t find_coordinator_version)
            : context_(context), metadata_(std::move(metadata)), lookup_(std::move(lookup)), resolve_(std::move(resolve)), seed_(std::move(seed)), refresh_metadata_(std::move(refresh_metadata)), client_configuration_(client_configuration), retry_(context, std::move(client_configuration)), codecs_(std::move(codecs)), version_(version), init_version_(std::min<std::int16_t>(init_version, 1)), find_coordinator_version_(std::min<std::int16_t>(find_coordinator_version, 2)) {}

        auto partitions(std::string_view topic)
            -> result<std::vector<std::int32_t>> override
        {
            auto p = metadata_->partitions(topic);
            if (p.empty())
                return std::unexpected(make_error(error_code::unknown_topic_or_partition,
                    "metadata is unavailable"));
            return p;
        }

        auto initialize_idempotent(std::optional<std::string_view> transactional,
            std::chrono::milliseconds timeout,
            cancel_token* token)
            -> task<result<std::pair<std::int64_t, std::int16_t>>> override
        {
            if (init_version_ < 0)
                co_return std::unexpected(
                    make_error(error_code::unsupported_version,
                        "broker has no compatible InitProducerId version"));
            broker_connection* connection = nullptr;
            if (transactional)
            {
                auto located = co_await locate_transaction_coordinator(*transactional,
                    token);
                if (!located)
                    co_return std::unexpected(located.error());
                connection = transaction_coordinator();
            }
            else
            {
                connection = seed_();
            }
            if (!connection)
                co_return std::unexpected(
                    make_error(error_code::coordinator_not_available));
            protocol::encoder encoder;
            if (transactional)
                encoder.nullable_string(
                    std::optional<std::string>{std::string(*transactional)});
            else
                encoder.nullable_string({});
            encoder.int32(static_cast<std::int32_t>(timeout.count()));
            auto raw = co_await request_with_cancel(
                *connection, protocol::api_key::init_producer_id, init_version_,
                std::move(encoder).take(), token);
            if (!raw)
                co_return std::unexpected(raw.error());
            protocol::decoder decoder(*raw);
            auto throttle = decoder.int32();
            auto ec = decoder.int16();
            auto producer_id = decoder.int64();
            auto epoch = decoder.int16();
            if (!throttle || !ec || !producer_id || !epoch)
                co_return std::unexpected(make_error(
                    error_code::malformed_response, "truncated InitProducerId response"));
            if (*ec != 0)
                co_return std::unexpected(make_error(static_cast<error_code>(*ec)));
            co_return std::pair{*producer_id, *epoch};
        }

        auto wait_for_linger(std::chrono::milliseconds duration, cancel_token* token)
            -> task<result<void>> override
        {
            std::expected<void, std::error_code> waited;
            if (token)
                waited = co_await async_timer_wait(context_, duration, *token);
            else
                waited = co_await async_timer_wait(context_, duration);
            if (!waited)
                co_return std::unexpected(make_error(
                    waited.error() == std::make_error_code(std::errc::operation_canceled)
                        ? error_code::cancelled
                        : error_code::transport,
                    waited.error().message()));
            co_return result<void>{};
        }

        auto send_batch(const topic_partition& tp, std::span<const record> records,
            const record_batch_options& options, acknowledgement acks,
            std::chrono::steady_clock::time_point deadline,
            cancel_token* token)
            -> task<result<std::vector<record_metadata>>> override
        {
            if (version_ < 0)
                co_return std::unexpected(
                    make_error(error_code::unsupported_version,
                        "broker has no compatible non-flexible Produce version"));
            auto batch = encode_record_batch(records, options, codecs_);
            if (!batch)
                co_return std::unexpected(batch.error());
            protocol::produce_request request{.acks = acks,
                .partitions = {{tp, std::move(*batch)}}};
            request.transactional_id = options.transactional_id;
            auto body = protocol::encode_produce(request, version_);
            error last_failure =
                make_error(error_code::transport, "Produce request was not attempted");
            for (std::size_t attempt = 0;; ++attempt)
            {
                if (std::chrono::steady_clock::now() >= deadline)
                    co_return std::unexpected(
                        make_error(error_code::request_timed_out,
                            "producer delivery timeout expired"));
                auto refreshed = co_await refresh_metadata_({tp.topic}, false, token);
                if (!refreshed)
                    last_failure = refreshed.error();
                else
                {
                    auto leader = metadata_->leader(tp);
                    if (!leader)
                        last_failure = leader.error();
                    else if (auto* c = lookup_(leader->node_id); !c)
                        last_failure = make_error(error_code::leader_not_available);
                    else if (acks == acknowledgement::none)
                    {
                        result<void> sent;
                        if (token)
                            sent = co_await c->send(protocol::api_key::produce, version_, body,
                                *token);
                        else
                            sent = co_await c->send(protocol::api_key::produce, version_, body);
                        if (sent)
                        {
                            std::vector<record_metadata> out;
                            for (auto& record : records)
                                out.push_back({tp, -1, record.timestamp});
                            co_return out;
                        }
                        last_failure = sent.error();
                    }
                    else
                    {
                        auto response = co_await request_with_cancel(
                            *c, protocol::api_key::produce, version_, body, token);
                        if (!response)
                            last_failure = response.error();
                        else
                        {
                            auto decoded =
                                protocol::decode_produce(*response, version_, request);
                            if (!decoded)
                                co_return std::unexpected(decoded.error());
                            auto produced =
                                std::ranges::find(decoded->begin(), decoded->end(), tp,
                                    &protocol::produce_result::target);
                            if (produced == decoded->end())
                                co_return std::unexpected(make_error(
                                    error_code::malformed_response,
                                    "Produce response did not map the requested partition"));
                            if (produced->error == error_code::none ||
                                produced->error == error_code::duplicate_sequence_number)
                            {
                                std::vector<record_metadata> out;
                                out.reserve(records.size());
                                for (std::size_t i = 0; i < records.size(); ++i)
                                    out.push_back(
                                        {tp,
                                            produced->base_offset >= 0
                                                ? produced->base_offset + static_cast<std::int64_t>(i)
                                                : -1,
                                            produced->log_append_time >= 0 ? produced->log_append_time
                                                                           : records[i].timestamp});
                                co_return out;
                            }
                            last_failure = make_error(produced->error);
                        }
                    }
                }
                auto decision = retry_.decide(last_failure, attempt, deadline);
                if (!decision)
                    co_return std::unexpected(decision.error());
                if (decision->recovery == kafka_retry_recovery::refresh_metadata)
                {
                    auto recovery = co_await refresh_metadata_({tp.topic}, true, token);
                    if (!recovery && !recovery.error().retriable)
                        co_return std::unexpected(recovery.error());
                }
                auto waited = co_await retry_.wait(*decision, token);
                if (!waited)
                    co_return std::unexpected(waited.error());
            }
        }

        auto add_transaction_partitions(
            std::string_view transactional_id, std::int64_t producer_id,
            std::int16_t producer_epoch, std::span<const topic_partition> partitions,
            cancel_token* token) -> task<result<void>> override
        {
            auto located =
                co_await locate_transaction_coordinator(transactional_id, token);
            if (!located)
                co_return std::unexpected(located.error());
            auto* connection = transaction_coordinator();
            if (!connection)
                co_return std::unexpected(
                    make_error(error_code::coordinator_not_available));
            protocol::encoder encoder;
            encoder.string(transactional_id);
            encoder.int64(producer_id);
            encoder.int16(producer_epoch);
            std::map<std::string, std::vector<std::int32_t>, std::less<>> topics;
            for (const auto& partition : partitions)
                topics[partition.topic].push_back(partition.partition);
            encoder.int32(static_cast<std::int32_t>(topics.size()));
            for (const auto& [topic, topic_partitions] : topics)
            {
                encoder.string(topic);
                encoder.int32(static_cast<std::int32_t>(topic_partitions.size()));
                for (auto partition : topic_partitions)
                    encoder.int32(partition);
            }
            auto response = co_await request_with_cancel(
                *connection, protocol::api_key::add_partitions_to_txn, 1,
                std::move(encoder).take(), token);
            if (!response)
                co_return std::unexpected(response.error());
            protocol::decoder decoder(*response);
            auto throttle = decoder.int32();
            auto topic_count = decoder.int32();
            if (!throttle || !topic_count || *topic_count < 0)
                co_return std::unexpected(make_error(
                    error_code::malformed_response,
                    "truncated AddPartitionsToTxn response"));
            for (std::int32_t topic_index = 0; topic_index < *topic_count;
                 ++topic_index)
            {
                auto topic = decoder.string();
                auto partition_count = decoder.int32();
                if (!topic || !partition_count || *partition_count < 0)
                    co_return std::unexpected(make_error(
                        error_code::malformed_response,
                        "truncated AddPartitionsToTxn topic response"));
                for (std::int32_t partition_index = 0;
                     partition_index < *partition_count; ++partition_index)
                {
                    auto partition = decoder.int32();
                    auto code = decoder.int16();
                    if (!partition || !code)
                        co_return std::unexpected(make_error(
                            error_code::malformed_response,
                            "truncated AddPartitionsToTxn partition response"));
                    if (*code != 0)
                        co_return std::unexpected(
                            make_error(static_cast<error_code>(*code)));
                }
            }
            co_return result<void>{};
        }

        auto add_transaction_offsets(
            std::string_view transactional_id, std::int64_t producer_id,
            std::int16_t producer_epoch, std::string_view group,
            const std::map<topic_partition, offset_and_metadata>& offsets,
            cancel_token* token) -> task<result<void>> override
        {
            auto located =
                co_await locate_transaction_coordinator(transactional_id, token);
            if (!located)
                co_return std::unexpected(located.error());
            auto* connection = transaction_coordinator();
            if (!connection)
                co_return std::unexpected(
                    make_error(error_code::coordinator_not_available));
            protocol::encoder encoder;
            encoder.string(transactional_id);
            encoder.int64(producer_id);
            encoder.int16(producer_epoch);
            encoder.string(group);
            auto response = co_await request_with_cancel(
                *connection, protocol::api_key::add_offsets_to_txn, 1,
                std::move(encoder).take(), token);
            if (!response)
                co_return std::unexpected(response.error());
            protocol::decoder decoder(*response);
            auto throttle = decoder.int32();
            auto code = decoder.int16();
            if (!throttle || !code)
                co_return std::unexpected(make_error(
                    error_code::malformed_response, "truncated AddOffsetsToTxn response"));
            if (*code != 0)
                co_return std::unexpected(make_error(static_cast<error_code>(*code)));
            std::map<topic_partition, std::int64_t> plain_offsets;
            for (const auto& [partition, offset] : offsets)
                plain_offsets[partition] = offset.offset;
            auto commit_body = protocol::encode_transaction_offset_commit(
                transactional_id, group, producer_id, producer_epoch, -1, "",
                plain_offsets);
            auto committed = co_await request_with_cancel(
                *connection, protocol::api_key::txn_offset_commit, 2, commit_body,
                token);
            if (!committed)
                co_return std::unexpected(committed.error());
            protocol::decoder commit_decoder(*committed);
            auto commit_throttle = commit_decoder.int32();
            auto topic_count = commit_decoder.int32();
            if (!commit_throttle || !topic_count || *topic_count < 0)
                co_return std::unexpected(make_error(
                    error_code::malformed_response,
                    "truncated TxnOffsetCommit response"));
            for (std::int32_t topic_index = 0; topic_index < *topic_count;
                 ++topic_index)
            {
                auto topic = commit_decoder.string();
                auto partition_count = commit_decoder.int32();
                if (!topic || !partition_count || *partition_count < 0)
                    co_return std::unexpected(make_error(
                        error_code::malformed_response,
                        "truncated TxnOffsetCommit topic response"));
                for (std::int32_t partition_index = 0;
                     partition_index < *partition_count; ++partition_index)
                {
                    auto partition = commit_decoder.int32();
                    auto commit_code = commit_decoder.int16();
                    if (!partition || !commit_code)
                        co_return std::unexpected(make_error(
                            error_code::malformed_response,
                            "truncated TxnOffsetCommit partition response"));
                    if (*commit_code != 0)
                        co_return std::unexpected(
                            make_error(static_cast<error_code>(*commit_code)));
                }
            }
            co_return result<void>{};
        }

        auto finish_transaction(std::string_view transactional_id,
            std::int64_t producer_id,
            std::int16_t producer_epoch, bool commit,
            cancel_token* token) -> task<result<void>> override
        {
            auto located =
                co_await locate_transaction_coordinator(transactional_id, token);
            if (!located)
                co_return std::unexpected(located.error());
            auto* connection = transaction_coordinator();
            if (!connection)
                co_return std::unexpected(
                    make_error(error_code::coordinator_not_available));
            auto body = protocol::encode_end_transaction(
                transactional_id, producer_id, producer_epoch, commit);
            auto response = co_await request_with_cancel(
                *connection, protocol::api_key::end_txn, 1, body, token);
            if (!response)
                co_return std::unexpected(response.error());
            protocol::decoder decoder(*response);
            auto throttle = decoder.int32();
            auto code = decoder.int16();
            if (!throttle || !code)
                co_return std::unexpected(make_error(
                    error_code::malformed_response, "truncated EndTxn response"));
            if (*code != 0)
                co_return std::unexpected(make_error(static_cast<error_code>(*code)));
            co_return result<void>{};
        }

    private:
        auto transaction_coordinator() -> broker_connection*
        {
            if (!transaction_coordinator_id_)
                return nullptr;
            return lookup_(*transaction_coordinator_id_);
        }

        auto locate_transaction_coordinator(std::string_view transactional_id,
            cancel_token* token)
            -> task<result<void>>
        {
            if (transaction_coordinator())
                co_return result<void>{};
            if (find_coordinator_version_ < 1)
                co_return std::unexpected(make_error(
                    error_code::unsupported_version,
                    "transactional producer requires FindCoordinator version 1+"));
            auto deadline = std::chrono::steady_clock::now() +
                client_configuration_.request_timeout *
                    static_cast<std::int64_t>(client_configuration_.retries + 1);
            error failure = make_error(error_code::coordinator_not_available,
                "transaction coordinator is unavailable");
            for (std::size_t attempt = 0;; ++attempt)
            {
                auto* bootstrap = seed_();
                if (!bootstrap)
                    failure = make_error(error_code::coordinator_not_available,
                        "bootstrap connection is unavailable");
                else
                {
                    auto body = protocol::encode_find_coordinator(
                        transactional_id, find_coordinator_version_, false);
                    auto raw = co_await request_with_cancel(
                        *bootstrap, protocol::api_key::find_coordinator,
                        find_coordinator_version_, body, token);
                    if (!raw)
                        failure = raw.error();
                    else
                    {
                        auto response = protocol::decode_find_coordinator(
                            *raw, find_coordinator_version_);
                        if (!response)
                            co_return std::unexpected(response.error());
                        if (response->error == error_code::none)
                        {
                            transaction_coordinator_id_ =
                                response->coordinator.node_id;
                            if (transaction_coordinator() ||
                                resolve_(response->coordinator))
                                co_return result<void>{};
                            failure = make_error(
                                error_code::coordinator_not_available,
                                "unable to create transaction coordinator connection");
                        }
                        else
                            failure = make_error(
                                response->error,
                                response->error_message.value_or(
                                    "FindCoordinator failed"));
                    }
                }
                transaction_coordinator_id_.reset();
                auto decision = retry_.decide(failure, attempt, deadline);
                if (!decision)
                    co_return std::unexpected(decision.error());
                if (decision->recovery ==
                        kafka_retry_recovery::refresh_metadata ||
                    decision->recovery ==
                        kafka_retry_recovery::rediscover_coordinator)
                {
                    auto refreshed = co_await refresh_metadata_({}, true, token);
                    if (!refreshed && !refreshed.error().retriable)
                        co_return std::unexpected(refreshed.error());
                }
                auto waited = co_await retry_.wait(*decision, token);
                if (!waited)
                    co_return std::unexpected(waited.error());
            }
        }

        io_context& context_;
        std::shared_ptr<metadata_cache> metadata_;
        std::function<broker_connection*(std::int32_t)> lookup_;
        std::function<broker_connection*(const broker_endpoint&)> resolve_;
        std::function<broker_connection*()> seed_;
        metadata_refresh_operation refresh_metadata_;
        client_options client_configuration_;
        kafka_request_retry_state_machine retry_;
        compression_registry codecs_;
        std::int16_t version_ = 3;
        std::int16_t init_version_ = 0;
        std::int16_t find_coordinator_version_ = -1;
        std::optional<std::int32_t> transaction_coordinator_id_;
    };

    auto decode_consumer_assignment(std::span<const std::byte> payload)
        -> result<std::vector<topic_partition>>
    {
        protocol::decoder decoder(payload);
        auto version = decoder.int16();
        auto topic_count = decoder.int32();
        if (!version || !topic_count || *topic_count < 0)
            return std::unexpected(make_error(error_code::malformed_response,
                "invalid consumer assignment"));
        std::vector<topic_partition> assigned;
        for (std::int32_t i = 0; i < *topic_count; ++i)
        {
            auto topic = decoder.string();
            auto partition_count = decoder.int32();
            if (!topic || !partition_count || *partition_count < 0)
                return std::unexpected(make_error(error_code::malformed_response,
                    "truncated consumer assignment"));
            for (std::int32_t p = 0; p < *partition_count; ++p)
            {
                auto partition = decoder.int32();
                if (!partition)
                    return std::unexpected(partition.error());
                assigned.push_back({*topic, *partition});
            }
        }
        auto user_data = decoder.byte_array();
        if (!user_data)
            return std::unexpected(user_data.error());
        return assigned;
    }

    class kafka_group_request_backend final : public group_backend
    {
    public:
        kafka_group_request_backend(
            io_context& context, std::shared_ptr<metadata_cache> metadata,
            std::function<std::unique_ptr<broker_connection>(
                const broker_endpoint&)>
                make_coordinator_connection,
            std::function<broker_connection*()> seed,
            metadata_refresh_operation refresh_metadata,
            client_options client_configuration, consumer_options options,
            consumer_group_api_versions versions)
            : metadata_(std::move(metadata)),
              make_coordinator_connection_(
                  std::move(make_coordinator_connection)),
              seed_(std::move(seed)),
              refresh_metadata_(std::move(refresh_metadata)),
              client_configuration_(std::move(client_configuration)),
              options_(std::move(options)),
              versions_(versions),
              retry_(context, client_configuration_) {}

        auto join(std::string_view group, const group_state& previous,
            std::span<const std::string> topics, assignment_strategy& strategy,
            cancel_token* token) -> task<result<group_state>> override
        {
            auto deadline = std::chrono::steady_clock::now() +
                client_configuration_.request_timeout *
                    static_cast<std::int64_t>(client_configuration_.retries + 1);
            auto current = previous;
            for (std::size_t attempt = 0;; ++attempt)
            {
                auto joined = co_await join_handshake(
                    group, current, topics, strategy, token);
                if (joined)
                    co_return std::move(*joined);
                auto failure = joined.error();
                if (failure.code != error_code::rebalance_in_progress &&
                    failure.code != error_code::illegal_generation &&
                    failure.code != error_code::unknown_member_id)
                    co_return std::unexpected(std::move(failure));
                if (group_error_resets_member(failure.code))
                {
                    current.member_id.clear();
                    current.generation = -1;
                    current.protocol_name.clear();
                    current.leader = false;
                    current.assigned_partitions.clear();
                    failure.retriable = true;
                }
                auto decision = retry_.decide(failure, attempt, deadline);
                if (!decision)
                    co_return std::unexpected(decision.error());
                auto waited = co_await retry_.wait(*decision, token);
                if (!waited)
                    co_return std::unexpected(waited.error());
            }
        }

        auto join_handshake(std::string_view group, group_state& previous,
            std::span<const std::string> topics, assignment_strategy& strategy,
            cancel_token* token) -> task<result<group_state>>
        {
            if (versions_.join_group < 0 || versions_.sync_group < 0)
                co_return std::unexpected(make_error(
                    error_code::unsupported_version,
                    "broker has no compatible non-flexible consumer group version"));
            if (previous.group_instance_id && versions_.join_group < 5)
                co_return std::unexpected(
                    make_error(error_code::unsupported_version,
                        "static group membership requires JoinGroup v5"));
            auto metadata_ready = co_await ensure_subscription_metadata(topics, token);
            if (!metadata_ready)
                co_return std::unexpected(metadata_ready.error());
            auto located = co_await locate(group, token);
            if (!located)
                co_return std::unexpected(located.error());
            protocol::join_group_request request{
                .group_id = std::string(group),
                .session_timeout = options_.session_timeout,
                .rebalance_timeout = options_.max_poll_interval,
                .member_id = previous.member_id,
                .group_instance_id = previous.group_instance_id,
                .protocols = {
                    {std::string(strategy.name()),
                        strategy.metadata(topics, previous.assigned_partitions)}}};
            protocol::join_group_response joined;
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                auto body = protocol::encode_join_group(request, versions_.join_group);
                auto decoded =
                    co_await request_coordinator_response<protocol::join_group_response>(
                        group, protocol::api_key::join_group, versions_.join_group, body,
                        [this](std::span<const std::byte> raw)
                        {
                            return protocol::decode_join_group(raw, versions_.join_group);
                        },
                        token);
                if (!decoded)
                    co_return std::unexpected(decoded.error());
                joined = std::move(*decoded);
                if (joined.error == error_code::member_id_required && attempt < 2 &&
                    !joined.member_id.empty())
                {
                    request.member_id = joined.member_id;
                    continue;
                }
                if (joined.error == error_code::unknown_member_id && attempt < 2)
                {
                    request.member_id.clear();
                    continue;
                }
                break;
            }
            if (joined.error != error_code::none)
                co_return std::unexpected(make_error(joined.error));
            previous.member_id = joined.member_id;
            previous.generation = joined.generation;
            previous.protocol_name = joined.protocol_name;
            previous.leader = joined.leader_id == joined.member_id;
            std::vector<protocol::sync_group_assignment> sync_assignments;
            if (joined.leader_id == joined.member_id)
            {
                std::vector<group_member> members;
                members.reserve(joined.members.size());
                for (auto& member : joined.members)
                    members.push_back({member.member_id, member.metadata});
                std::map<std::string, std::vector<std::int32_t>, std::less<>> partitions;
                for (auto& topic : topics)
                {
                    auto available = metadata_->partitions(topic);
                    if (available.empty())
                        co_return std::unexpected(
                            make_error(error_code::unknown_topic_or_partition,
                                "subscription topic has no metadata"));
                    partitions.emplace(topic, std::move(available));
                }
                auto assigned = strategy.assign(members, partitions);
                if (!assigned)
                    co_return std::unexpected(assigned.error());
                for (auto& assignment : *assigned)
                    sync_assignments.push_back(
                        {assignment.member_id, std::move(assignment.assignment)});
            }
            protocol::group_identity identity{std::string(group), joined.generation,
                joined.member_id,
                previous.group_instance_id};
            auto sync_body = protocol::encode_sync_group(
                {identity, std::move(sync_assignments)}, versions_.sync_group);
            auto synced =
                co_await request_coordinator_response<protocol::sync_group_response>(
                    group, protocol::api_key::sync_group, versions_.sync_group,
                    sync_body,
                    [this](std::span<const std::byte> raw)
                    {
                        return protocol::decode_sync_group(raw, versions_.sync_group);
                    },
                    token);
            if (!synced)
                co_return std::unexpected(synced.error());
            if (synced->error != error_code::none)
                co_return std::unexpected(make_error(synced->error));
            auto assignment = decode_consumer_assignment(synced->assignment);
            if (!assignment)
                co_return std::unexpected(assignment.error());
            co_return group_state{joined.member_id,
                previous.group_instance_id,
                joined.generation,
                joined.protocol_name,
                joined.leader_id == joined.member_id,
                std::move(*assignment)};
        }

        auto heartbeat(std::string_view group, const group_state& state,
            cancel_token* token) -> task<result<void>> override
        {
            if (versions_.heartbeat < 0)
                co_return std::unexpected(
                    make_error(error_code::unsupported_version,
                        "broker has no compatible Heartbeat version"));
            auto body =
                protocol::encode_heartbeat({std::string(group), state.generation,
                                               state.member_id, state.group_instance_id},
                    versions_.heartbeat);
            auto response = co_await request_coordinator_response<
                protocol::group_operation_response>(
                group, protocol::api_key::heartbeat, versions_.heartbeat, body,
                [this](std::span<const std::byte> raw)
                {
                    return protocol::decode_heartbeat(raw, versions_.heartbeat);
                },
                token);
            if (!response)
                co_return std::unexpected(response.error());
            if (response->error != error_code::none)
                co_return std::unexpected(make_error(response->error));
            co_return result<void>{};
        }

        auto leave(std::string_view group, const group_state& state,
            cancel_token* token) -> task<result<void>> override
        {
            if (state.member_id.empty())
                co_return result<void>{};
            if (versions_.leave_group < 0)
                co_return std::unexpected(
                    make_error(error_code::unsupported_version,
                        "broker has no compatible LeaveGroup version"));
            auto body =
                protocol::encode_leave_group({std::string(group), state.generation,
                                                 state.member_id, state.group_instance_id},
                    versions_.leave_group);
            auto response = co_await request_coordinator_response<
                protocol::group_operation_response>(
                group, protocol::api_key::leave_group, versions_.leave_group, body,
                [this](std::span<const std::byte> raw)
                {
                    return protocol::decode_leave_group(raw, versions_.leave_group);
                },
                token);
            if (!response)
                co_return std::unexpected(response.error());
            if (!leave_group_completed(response->error))
                co_return std::unexpected(make_error(response->error));
            co_return result<void>{};
        }

        auto fetch_committed_offsets(std::string_view group,
            std::span<const topic_partition> partitions,
            cancel_token* token)
            -> task<result<std::map<topic_partition, std::int64_t>>>
        {
            if (versions_.offset_fetch < 0)
                co_return std::unexpected(
                    make_error(error_code::unsupported_version,
                        "broker has no compatible OffsetFetch version"));
            auto body = protocol::encode_offset_fetch(group, partitions);
            auto response =
                co_await request_coordinator_response<protocol::offset_fetch_response>(
                    group, protocol::api_key::offset_fetch, versions_.offset_fetch,
                    body,
                    [this](std::span<const std::byte> raw)
                    {
                        return protocol::decode_offset_fetch(raw, versions_.offset_fetch);
                    },
                    token);
            if (!response)
                co_return std::unexpected(response.error());
            if (response->error != error_code::none)
                co_return std::unexpected(make_error(response->error));
            std::map<topic_partition, std::int64_t> offsets;
            for (auto& offset : response->offsets)
            {
                if (offset.error != error_code::none)
                    co_return std::unexpected(make_error(offset.error));
                if (offset.offset >= 0)
                    offsets[offset.source] = offset.offset;
            }
            co_return offsets;
        }

        auto commit_offsets(std::string_view group,
            const protocol::group_identity& identity,
            const std::map<topic_partition, std::int64_t>& offsets,
            cancel_token* token) -> task<result<void>>
        {
            auto version = versions_.offset_commit;
            if (version < 0)
                co_return std::unexpected(
                    make_error(error_code::unsupported_version,
                        "broker has no compatible OffsetCommit version"));
            auto body = protocol::encode_offset_commit(identity, offsets, version);
            auto deadline =
                std::chrono::steady_clock::now() +
                client_configuration_.request_timeout *
                    static_cast<std::int64_t>(client_configuration_.retries + 1);
            error failure = make_error(error_code::coordinator_not_available);
            for (std::size_t attempt = 0;; ++attempt)
            {
                auto located = co_await locate(group, token);
                if (!located)
                    failure = located.error();
                else if (auto* connection = coordinator(); !connection)
                    failure = make_error(error_code::coordinator_not_available);
                else
                {
                    auto raw = co_await request_with_cancel(
                        *connection, protocol::api_key::offset_commit, version, body,
                        token);
                    if (!raw)
                        failure = raw.error();
                    else
                    {
                        protocol::decoder decoder(*raw);
                        if (version >= 3)
                        {
                            auto throttle = decoder.int32();
                            if (!throttle)
                                co_return std::unexpected(throttle.error());
                        }
                        auto topic_count = decoder.int32();
                        if (!topic_count || *topic_count < 0)
                            co_return std::unexpected(
                                make_error(error_code::malformed_response,
                                    "invalid OffsetCommit response"));
                        failure = make_error(error_code::none);
                        for (std::int32_t i = 0; i < *topic_count; ++i)
                        {
                            auto topic = decoder.string();
                            auto partition_count = decoder.int32();
                            if (!topic || !partition_count || *partition_count < 0)
                                co_return std::unexpected(
                                    make_error(error_code::malformed_response,
                                        "truncated OffsetCommit response"));
                            for (std::int32_t p = 0; p < *partition_count; ++p)
                            {
                                auto partition = decoder.int32();
                                auto ec = decoder.int16();
                                if (!partition || !ec)
                                    co_return std::unexpected(
                                        make_error(error_code::malformed_response,
                                            "truncated OffsetCommit partition"));
                                if (*ec != 0 && failure.code == error_code::none)
                                    failure = make_error(static_cast<error_code>(*ec));
                            }
                        }
                        if (failure.code == error_code::none)
                            co_return result<void>{};
                    }
                }
                auto decision = retry_.decide(failure, attempt, deadline);
                if (!decision)
                    co_return std::unexpected(decision.error());
                if (decision->recovery == kafka_retry_recovery::rediscover_coordinator)
                    reset_coordinator();
                if (decision->recovery == kafka_retry_recovery::refresh_metadata)
                {
                    auto refreshed = co_await refresh_metadata_({}, true, token);
                    if (!refreshed && !refreshed.error().retriable)
                        co_return std::unexpected(refreshed.error());
                    reset_coordinator();
                }
                auto waited = co_await retry_.wait(*decision, token);
                if (!waited)
                    co_return std::unexpected(waited.error());
            }
        }

        auto ensure_coordinator(std::string_view group, cancel_token* token)
            -> task<result<void>>
        {
            co_return co_await locate(group, token);
        }

        void invalidate_coordinator() noexcept
        {
            reset_coordinator();
        }

        [[nodiscard]] auto fetch_version() const noexcept -> std::int16_t
        {
            return versions_.fetch;
        }

        [[nodiscard]] auto offset_commit_version() const noexcept -> std::int16_t
        {
            return versions_.offset_commit;
        }

        [[nodiscard]] auto list_offsets_version() const noexcept -> std::int16_t
        {
            return versions_.list_offsets;
        }

        [[nodiscard]] auto coordinator() -> broker_connection*
        {
            return coordinator_id_ ? coordinator_connection_.get() : nullptr;
        }

    private:
        template <typename Response, typename Decoder>
        auto request_coordinator_response(std::string_view group,
            protocol::api_key key, std::int16_t version,
            std::span<const std::byte> body,
            Decoder decoder, cancel_token* token)
            -> task<result<Response>>
        {
            auto deadline =
                std::chrono::steady_clock::now() +
                client_configuration_.request_timeout *
                    static_cast<std::int64_t>(client_configuration_.retries + 1);
            error failure = make_error(error_code::coordinator_not_available);
            for (std::size_t attempt = 0;; ++attempt)
            {
                auto located = co_await locate(group, token);
                if (!located)
                    failure = located.error();
                else if (auto* connection = coordinator(); !connection)
                    failure = make_error(error_code::coordinator_not_available);
                else
                {
                    auto raw = co_await request_with_cancel(*connection, key, version, body,
                        token);
                    if (!raw)
                        failure = raw.error();
                    else
                    {
                        auto response = decoder(*raw);
                        if (!response)
                            co_return std::unexpected(response.error());
                        if (response->error != error_code::coordinator_load_in_progress &&
                            response->error != error_code::coordinator_not_available &&
                            response->error != error_code::not_coordinator)
                            co_return std::move(*response);
                        failure = make_error(response->error);
                    }
                }
                auto decision = retry_.decide(failure, attempt, deadline);
                if (!decision)
                    co_return std::unexpected(decision.error());
                if (decision->recovery == kafka_retry_recovery::rediscover_coordinator)
                    reset_coordinator();
                if (decision->recovery == kafka_retry_recovery::refresh_metadata)
                {
                    auto refreshed = co_await refresh_metadata_({}, true, token);
                    if (!refreshed && !refreshed.error().retriable)
                        co_return std::unexpected(refreshed.error());
                    reset_coordinator();
                }
                auto waited = co_await retry_.wait(*decision, token);
                if (!waited)
                    co_return std::unexpected(waited.error());
            }
        }

        auto ensure_subscription_metadata(std::span<const std::string> topics,
            cancel_token* token) -> task<result<void>>
        {
            auto deadline = std::chrono::steady_clock::now() +
                client_configuration_.request_timeout;
            error failure = make_error(error_code::unknown_topic_or_partition,
                "subscription topic has no metadata");
            for (std::size_t attempt = 0;; ++attempt)
            {
                auto refreshed = co_await refresh_metadata_(
                    std::vector<std::string>(topics.begin(), topics.end()), true,
                    token);
                if (!refreshed)
                    failure = refreshed.error();
                else
                {
                    const bool complete = std::ranges::all_of(topics,
                        [this](const std::string& topic)
                        {
                            return !metadata_->partitions(topic).empty();
                        });
                    if (complete)
                        co_return result<void>{};
                    failure = make_error(error_code::unknown_topic_or_partition,
                        "subscription topic has no metadata after refresh");
                }
                auto decision = retry_.decide(failure, attempt, deadline);
                if (!decision)
                    co_return std::unexpected(decision.error());
                auto waited = co_await retry_.wait(*decision, token);
                if (!waited)
                    co_return std::unexpected(waited.error());
            }
        }

        auto locate(std::string_view group, cancel_token* token)
            -> task<result<void>>
        {
            if (versions_.find_coordinator < 0)
                co_return std::unexpected(
                    make_error(error_code::unsupported_version,
                        "broker has no compatible FindCoordinator version"));
            if (coordinator())
                co_return result<void>{};
            auto deadline =
                std::chrono::steady_clock::now() +
                client_configuration_.request_timeout *
                    static_cast<std::int64_t>(client_configuration_.retries + 1);
            error failure = make_error(error_code::coordinator_not_available);
            for (std::size_t attempt = 0;; ++attempt)
            {
                auto* connection = seed_();
                if (!connection)
                    failure = make_error(error_code::coordinator_not_available,
                        "bootstrap connection is unavailable");
                else
                {
                    auto body = protocol::encode_find_coordinator(
                        group, versions_.find_coordinator, true);
                    auto raw = co_await request_with_cancel(
                        *connection, protocol::api_key::find_coordinator,
                        versions_.find_coordinator, body, token);
                    if (!raw)
                        failure = raw.error();
                    else
                    {
                        auto response = protocol::decode_find_coordinator(
                            *raw, versions_.find_coordinator);
                        if (!response)
                            co_return std::unexpected(response.error());
                        if (response->error == error_code::none)
                        {
                            auto connection = make_coordinator_connection_(
                                response->coordinator);
                            if (!connection)
                                failure = make_error(error_code::coordinator_not_available,
                                    "unable to create coordinator connection");
                            else
                            {
                                coordinator_id_ = response->coordinator.node_id;
                                coordinator_connection_ = std::move(connection);
                                co_return result<void>{};
                            }
                        }
                        else
                            failure = make_error(
                                response->error,
                                response->error_message.value_or("FindCoordinator failed"));
                    }
                }
                auto decision = retry_.decide(failure, attempt, deadline);
                if (!decision)
                    co_return std::unexpected(decision.error());
                reset_coordinator();
                if (decision->recovery == kafka_retry_recovery::refresh_metadata ||
                    decision->recovery == kafka_retry_recovery::rediscover_coordinator)
                {
                    auto refreshed = co_await refresh_metadata_({}, true, token);
                    if (!refreshed && !refreshed.error().retriable)
                        co_return std::unexpected(refreshed.error());
                }
                auto waited = co_await retry_.wait(*decision, token);
                if (!waited)
                    co_return std::unexpected(waited.error());
            }
        }

        void reset_coordinator() noexcept
        {
            if (coordinator_connection_)
                coordinator_connection_->close();
            coordinator_connection_.reset();
            coordinator_id_.reset();
        }

        std::shared_ptr<metadata_cache> metadata_;
        std::function<std::unique_ptr<broker_connection>(
            const broker_endpoint&)>
            make_coordinator_connection_;
        std::function<broker_connection*()> seed_;
        metadata_refresh_operation refresh_metadata_;
        client_options client_configuration_;
        consumer_options options_;
        consumer_group_api_versions versions_;
        kafka_request_retry_state_machine retry_;
        std::unique_ptr<broker_connection> coordinator_connection_;
        std::optional<std::int32_t> coordinator_id_;
    };

    class facade_consumer_backend final
        : public consumer_backend,
          public std::enable_shared_from_this<facade_consumer_backend>
    {
    public:
        facade_consumer_backend(
            io_context& context, std::shared_ptr<metadata_cache> metadata,
            std::shared_ptr<kafka_group_request_backend> group_backend,
            metadata_refresh_operation refresh_metadata,
            client_options client_configuration, consumer_options options)
            : metadata_(std::move(metadata)),
              group_backend_(std::move(group_backend)),
              refresh_metadata_(std::move(refresh_metadata)),
              client_configuration_(std::move(client_configuration)),
              options_(std::move(options)),
              coordinator_(
                  options_.group_id, group_backend_,
                  options_.assignment_policy ==
                          consumer_assignment_policy::cooperative_sticky
                      ? std::unique_ptr<assignment_strategy>{std::make_unique<
                            cooperative_sticky_assignment>()}
                      : std::unique_ptr<assignment_strategy>{std::make_unique<
                            range_assignment>()},
                  options_.group_instance_id),
              retry_(context, client_configuration_) {}

        void start_background_maintenance(io_context& context)
        {
            last_auto_commit_ = std::chrono::steady_clock::now();
            auto interval = options_.heartbeat_interval;
            if (options_.enable_auto_commit)
                interval = std::min(interval, options_.auto_commit_interval);
            if (interval < std::chrono::milliseconds{100})
                interval = std::chrono::milliseconds{100};
            spawn(context, background_maintenance(weak_from_this(), context, interval));
        }

        auto subscribe(std::span<const std::string> topics, cancel_token* token)
            -> task<result<void>> override
        {
            co_await membership_mutex_.lock();
            async_lock_guard membership_guard(
                membership_mutex_, std::adopt_lock);
            if (options_.group_id.empty())
                co_return std::unexpected(make_error(
                    error_code::configuration, "group_id is required for subscribe"));
            topics_.assign(topics.begin(), topics.end());
            explicit_assignment_ = false;
            poll_timeout_ = false;
            last_poll_ = std::chrono::steady_clock::now();
            auto joined = co_await coordinator_.join(topics_, token);
            if (!joined)
                co_return std::unexpected(joined.error());
            replace_assignment(joined->assigned_partitions);
            auto initialized = co_await initialize_assigned_positions(token);
            if (!initialized)
                co_return std::unexpected(initialized.error());
            last_heartbeat_ = std::chrono::steady_clock::now();
            co_return result<void>{};
        }

        auto assign(std::span<const topic_partition> partitions, cancel_token* token)
            -> task<result<void>> override
        {
            co_await membership_mutex_.lock();
            async_lock_guard membership_guard(
                membership_mutex_, std::adopt_lock);
            auto sessions_closed = co_await close_fetch_sessions(token);
            if (!sessions_closed)
                co_return std::unexpected(sessions_closed.error());
            if (!explicit_assignment_ && !coordinator_.state().member_id.empty())
            {
                auto left = co_await coordinator_.leave(token);
                if (!left)
                    co_return std::unexpected(left.error());
            }
            std::vector<topic_partition> missing;
            for (auto& partition : partitions)
                if (!positions_.contains(partition))
                    missing.push_back(partition);
            auto reset = co_await fetch_reset_offsets(missing, token);
            if (!reset)
                co_return std::unexpected(reset.error());
            std::map<topic_partition, std::int64_t> next_positions;
            for (auto& partition : partitions)
            {
                auto existing = positions_.find(partition);
                next_positions[partition] = existing == positions_.end()
                    ? reset->at(partition)
                    : existing->second;
            }
            positions_ = std::move(next_positions);
            fetch_sessions_.clear();
            topics_.clear();
            replace_assignment(
                std::vector<topic_partition>(partitions.begin(), partitions.end()));
            explicit_assignment_ = true;
            co_return result<void>{};
        }

        [[nodiscard]] auto assignment() const
            -> std::vector<topic_partition> override
        {
            std::scoped_lock lock(assignment_snapshot_mutex_);
            return assignment_snapshot_;
        }

        auto poll(std::size_t limit, cancel_token* token)
            -> task<result<std::vector<consumed_record>>> override
        {
            last_poll_ = std::chrono::steady_clock::now();
            poll_timeout_ = false;
            if (!explicit_assignment_)
            {
                auto ready = co_await maintain_membership(token);
                if (!ready)
                    co_return std::unexpected(ready.error());
            }
            auto deadline =
                std::chrono::steady_clock::now() +
                client_configuration_.request_timeout *
                    static_cast<std::int64_t>(client_configuration_.retries + 1);
            for (std::size_t attempt = 0;; ++attempt)
            {
                auto refreshed = co_await refresh_metadata_(topics_, false, token);
                result<std::vector<consumed_record>> fetched = std::unexpected(
                    refreshed ? make_error(error_code::transport) : refreshed.error());
                if (refreshed)
                    fetched = co_await poll_once(limit, token);
                if (fetched)
                    co_return std::move(*fetched);
                auto decision = retry_.decide(fetched.error(), attempt, deadline);
                if (!decision)
                    co_return std::unexpected(decision.error());
                if (decision->recovery == kafka_retry_recovery::refresh_metadata)
                {
                    auto recovery = co_await refresh_metadata_(topics_, true, token);
                    if (!recovery && !recovery.error().retriable)
                        co_return std::unexpected(recovery.error());
                    fetch_sessions_.clear();
                }
                auto waited = co_await retry_.wait(*decision, token);
                if (!waited)
                    co_return std::unexpected(waited.error());
            }
        }

        auto seek(const topic_partition& tp, std::int64_t offset, cancel_token*)
            -> task<result<void>> override
        {
            if (std::ranges::find(assigned_, tp) == assigned_.end())
                co_return std::unexpected(make_error(
                    error_code::configuration, "cannot seek an unassigned partition"));
            positions_[tp] = offset;
            co_return result<void>{};
        }

        auto commit(const std::map<topic_partition, offset_and_metadata>& offsets,
            cancel_token* token) -> task<result<void>> override
        {
            if (options_.group_id.empty())
                co_return std::unexpected(make_error(
                    error_code::configuration, "group_id is required to commit offsets"));
            auto& state = coordinator_.state();
            std::map<topic_partition, std::int64_t> plain;
            for (auto& [tp, offset] : offsets)
                plain[tp] = offset.offset;
            protocol::group_identity identity{
                options_.group_id, explicit_assignment_ ? -1 : state.generation,
                explicit_assignment_ ? std::string{} : state.member_id,
                explicit_assignment_ ? std::optional<std::string>{}
                                     : state.group_instance_id};
            co_return co_await group_backend_->commit_offsets(options_.group_id,
                identity, plain, token);
        }

        auto close(cancel_token* token) -> task<result<void>> override
        {
            closed_ = true;
            co_await membership_mutex_.lock();
            async_lock_guard membership_guard(
                membership_mutex_, std::adopt_lock);
            if (options_.enable_auto_commit && !options_.group_id.empty() &&
                !positions_.empty())
            {
                std::map<topic_partition, offset_and_metadata> offsets;
                for (auto& [partition, offset] : positions_)
                    offsets[partition] = {offset, {}, {}};
                auto saved = co_await commit(offsets, token);
                if (!saved)
                    co_return std::unexpected(saved.error());
            }
            auto sessions_closed = co_await close_fetch_sessions(token);
            if (!sessions_closed)
                co_return std::unexpected(sessions_closed.error());
            if (!explicit_assignment_ && !coordinator_.state().member_id.empty())
            {
                auto left = co_await coordinator_.leave(token);
                if (!left)
                    co_return std::unexpected(left.error());
            }
            clear_assignment();
            positions_.clear();
            topics_.clear();
            fetch_sessions_.clear();
            co_return result<void>{};
        }

        void
        set_broker_lookup(std::function<broker_connection*(std::int32_t)> lookup)
        {
            broker_lookup_ = std::move(lookup);
        }

    private:
        auto poll_once(std::size_t limit, cancel_token* token)
            -> task<result<std::vector<consumed_record>>>
        {
            std::map<std::int32_t, std::vector<protocol::fetch_partition>> by_broker;
            for (auto& partition : assigned_)
            {
                auto leader = metadata_->leader(partition);
                if (!leader)
                    co_return std::unexpected(leader.error());
                by_broker[leader->node_id].push_back({partition, positions_[partition]});
            }
            for (auto& [broker_id, session] : fetch_sessions_)
                if (!session.partitions.empty())
                    by_broker.try_emplace(broker_id);
            std::vector<consumed_record> out;
            auto fetch_version = group_backend_->fetch_version();
            if (fetch_version < 0)
                co_return std::unexpected(
                    make_error(error_code::unsupported_version,
                        "broker has no compatible non-flexible Fetch version"));
            for (auto& [broker_id, partitions] : by_broker)
            {
                auto* connection = lookup_broker(broker_id);
                if (!connection)
                    co_return std::unexpected(make_error(error_code::leader_not_available));
                auto& session = fetch_sessions_[broker_id];
                std::set<topic_partition> current;
                for (auto& partition : partitions)
                    current.insert(partition.source);
                std::vector<topic_partition> forgotten;
                std::ranges::set_difference(session.partitions, current,
                    std::back_inserter(forgotten));
                protocol::fetch_request request{
                    .max_wait = std::chrono::milliseconds{500},
                    .min_bytes = options_.fetch_min_bytes,
                    .max_bytes = options_.fetch_max_bytes,
                    .isolation = options_.isolation,
                    .session_id = fetch_version >= 7 ? session.id : 0,
                    .session_epoch = fetch_version >= 7 ? session.epoch : 0,
                    .partitions = std::move(partitions),
                    .forgotten_partitions = std::move(forgotten)};
                auto body = protocol::encode_fetch(request, fetch_version);
                auto raw = co_await request_with_cancel(
                    *connection, protocol::api_key::fetch, fetch_version, body, token);
                if (!raw)
                    co_return std::unexpected(raw.error());
                auto response = protocol::decode_fetch(*raw, fetch_version);
                if (!response)
                    co_return std::unexpected(response.error());
                if (response->error == error_code::fetch_session_id_not_found ||
                    response->error == error_code::invalid_fetch_session_epoch)
                {
                    session = {};
                    continue;
                }
                if (response->error != error_code::none)
                    co_return std::unexpected(make_error(response->error));
                if (fetch_version >= 7)
                {
                    session.id = response->session_id;
                    session.epoch = session.id == 0 ? 0 : session.epoch + 1;
                    session.partitions = std::move(current);
                }
                for (auto& part : response->partitions)
                {
                    if (part.error != error_code::none)
                        co_return std::unexpected(make_error(part.error));
                    std::size_t at = 0;
                    std::map<std::int64_t, std::deque<std::int64_t>>
                        aborted_transactions;
                    if (options_.isolation == isolation_level::read_committed)
                        for (const auto& aborted : part.aborted_transactions)
                            aborted_transactions[aborted.producer_id].push_back(
                                aborted.first_offset);
                    while (part.records.size() - at >= 12 && out.size() < limit)
                    {
                        protocol::decoder head(
                            std::span<const std::byte>(part.records).subspan(at + 8, 4));
                        auto n = head.int32();
                        if (!n || *n < 0 ||
                            static_cast<std::size_t>(*n) + 12 > part.records.size() - at)
                            co_return std::unexpected(make_error(
                                error_code::corrupt_message, "invalid record batch length"));
                        auto decoded = decode_record_batch(
                            std::span<const std::byte>(part.records)
                                .subspan(at, static_cast<std::size_t>(*n) + 12),
                            part.source, codecs_);
                        if (!decoded)
                            co_return std::unexpected(decoded.error());
                        const auto aborted = aborted_transactions.find(decoded->producer_id);
                        const bool discard_batch =
                            decoded->control ||
                            (decoded->transactional && aborted != aborted_transactions.end() &&
                                !aborted->second.empty() &&
                                decoded->base_offset >= aborted->second.front());
                        if (decoded->control)
                        {
                            if (aborted != aborted_transactions.end() &&
                                !aborted->second.empty() &&
                                decoded->base_offset >= aborted->second.front())
                                aborted->second.pop_front();
                        }
                        if (discard_batch)
                        {
                            positions_[part.source] = decoded->last_offset + 1;
                            at += static_cast<std::size_t>(*n) + 12;
                            continue;
                        }
                        for (auto& record : decoded->records)
                        {
                            const auto requested = positions_.find(record.source);
                            if (requested != positions_.end() &&
                                record.offset < requested->second)
                                continue;
                            positions_[record.source] = record.offset + 1;
                            out.push_back(std::move(record));
                            if (out.size() >= limit)
                                break;
                        }
                        at += static_cast<std::size_t>(*n) + 12;
                    }
                }
                if (out.size() >= limit)
                    break;
            }
            co_return out;
        }

        static auto
        background_maintenance(std::weak_ptr<facade_consumer_backend> weak,
            io_context& context,
            std::chrono::milliseconds interval) -> task<void>
        {
            steady_timer timer(context);
            while (true)
            {
                auto waited = co_await timer.async_wait(interval);
                if (!waited)
                    co_return;
                auto self = weak.lock();
                if (!self || self->closed_)
                    co_return;
                co_await self->membership_mutex_.lock();
                async_lock_guard membership_guard(
                    self->membership_mutex_, std::adopt_lock);
                if (self->closed_)
                    co_return;
                auto maintained =
                    co_await self->maintain_membership_unlocked(nullptr);
                if (!maintained && (self->poll_timeout_ || maintained.error().code != error_code::configuration))
                    continue;
                if (self->options_.enable_auto_commit &&
                    !self->options_.group_id.empty() && !self->positions_.empty() &&
                    std::chrono::steady_clock::now() - self->last_auto_commit_ >=
                        self->options_.auto_commit_interval)
                {
                    std::map<topic_partition, offset_and_metadata> offsets;
                    for (auto& [partition, offset] : self->positions_)
                        offsets[partition] = {offset, {}, {}};
                    auto committed = co_await self->commit(offsets, nullptr);
                    if (committed)
                        self->last_auto_commit_ = std::chrono::steady_clock::now();
                }
            }
        }

        auto close_fetch_sessions(cancel_token* token) -> task<result<void>>
        {
            auto version = group_backend_->fetch_version();
            if (version < 7)
                co_return result<void>{};
            for (auto& [broker_id, session] : fetch_sessions_)
            {
                if (session.id == 0)
                    continue;
                auto* connection = lookup_broker(broker_id);
                if (!connection)
                    continue;
                protocol::fetch_request request{.session_id = session.id,
                    .session_epoch = -1};
                auto body = protocol::encode_fetch(request, version);
                auto raw = co_await request_with_cancel(
                    *connection, protocol::api_key::fetch, version, body, token);
                if (!raw)
                    co_return std::unexpected(raw.error());
                auto response = protocol::decode_fetch(*raw, version);
                if (!response)
                    co_return std::unexpected(response.error());
                if (response->error != error_code::none &&
                    response->error != error_code::fetch_session_id_not_found)
                    co_return std::unexpected(make_error(response->error));
            }
            co_return result<void>{};
        }

        auto lookup_broker(std::int32_t id) -> broker_connection*
        {
            return broker_lookup_ ? broker_lookup_(id) : nullptr;
        }

        auto fetch_reset_offsets(std::span<const topic_partition> partitions,
            cancel_token* token)
            -> task<result<std::map<topic_partition, std::int64_t>>>
        {
            if (partitions.empty())
                co_return std::map<topic_partition, std::int64_t>{};
            if (options_.auto_offset_reset == offset_reset_policy::error)
                co_return std::unexpected(
                    make_error(error_code::configuration,
                        "no committed offset and auto_offset_reset is error"));
            auto version = group_backend_->list_offsets_version();
            if (version < 0)
                co_return std::unexpected(
                    make_error(error_code::unsupported_version,
                        "broker has no compatible ListOffsets version"));
            std::map<std::int32_t, std::vector<protocol::list_offset_partition>>
                requests;
            auto timestamp = options_.auto_offset_reset == offset_reset_policy::earliest
                ? -2LL
                : -1LL;
            for (auto& partition : partitions)
            {
                auto leader = metadata_->leader(partition);
                if (!leader)
                    co_return std::unexpected(leader.error());
                requests[leader->node_id].push_back({partition, timestamp, -1});
            }
            std::map<topic_partition, std::int64_t> out;
            for (auto& [broker_id, entries] : requests)
            {
                auto* connection = lookup_broker(broker_id);
                if (!connection)
                    co_return std::unexpected(make_error(error_code::leader_not_available));
                auto body =
                    protocol::encode_list_offsets(entries, options_.isolation, version);
                auto raw = co_await request_with_cancel(
                    *connection, protocol::api_key::list_offsets, version, body, token);
                if (!raw)
                    co_return std::unexpected(raw.error());
                auto listed = protocol::decode_list_offsets(*raw, version);
                if (!listed)
                    co_return std::unexpected(listed.error());
                for (auto& entry : *listed)
                {
                    if (entry.error != error_code::none)
                        co_return std::unexpected(make_error(entry.error));
                    if (entry.offset < 0)
                        co_return std::unexpected(
                            make_error(error_code::offset_out_of_range,
                                "ListOffsets returned no offset"));
                    out[entry.source] = entry.offset;
                }
            }
            co_return out;
        }

        auto initialize_assigned_positions(cancel_token* token)
            -> task<result<void>>
        {
            auto committed = co_await group_backend_->fetch_committed_offsets(
                options_.group_id, assigned_, token);
            if (!committed)
                co_return std::unexpected(committed.error());
            std::vector<topic_partition> missing;
            for (auto& partition : assigned_)
                if (!positions_.contains(partition) && !committed->contains(partition))
                    missing.push_back(partition);
            auto reset = co_await fetch_reset_offsets(missing, token);
            if (!reset)
                co_return std::unexpected(reset.error());
            std::map<topic_partition, std::int64_t> next;
            for (auto& partition : assigned_)
            {
                auto existing = positions_.find(partition);
                if (existing != positions_.end())
                    next[partition] = existing->second;
                else if (auto saved = committed->find(partition);
                         saved != committed->end())
                    next[partition] = saved->second;
                else
                    next[partition] = reset->at(partition);
            }
            positions_ = std::move(next);
            co_return result<void>{};
        }

        auto maintain_membership(cancel_token* token) -> task<result<void>>
        {
            co_await membership_mutex_.lock();
            async_lock_guard membership_guard(
                membership_mutex_, std::adopt_lock);
            co_return co_await maintain_membership_unlocked(token);
        }

        auto maintain_membership_unlocked(cancel_token* token)
            -> task<result<void>>
        {
            if (topics_.empty())
                co_return std::unexpected(make_error(
                    error_code::configuration, "subscribe or assign before polling"));
            if (!poll_timeout_ && !coordinator_.state().member_id.empty() &&
                std::chrono::steady_clock::now() - last_poll_ >
                    options_.max_poll_interval)
            {
                auto left = co_await coordinator_.leave(token);
                if (!left)
                    co_return std::unexpected(left.error());
                clear_assignment();
                poll_timeout_ = true;
            }
            if (poll_timeout_)
                co_return std::unexpected(make_error(
                    error_code::configuration,
                    "max_poll_interval exceeded; poll is required before rejoining"));
            if (coordinator_.state().member_id.empty())
            {
                auto joined = co_await coordinator_.join(topics_, token);
                if (!joined)
                    co_return std::unexpected(joined.error());
                replace_assignment(joined->assigned_partitions);
                auto initialized = co_await initialize_assigned_positions(token);
                if (!initialized)
                    co_return std::unexpected(initialized.error());
                last_heartbeat_ = std::chrono::steady_clock::now();
                co_return result<void>{};
            }
            if (std::chrono::steady_clock::now() - last_heartbeat_ <
                options_.heartbeat_interval)
                co_return result<void>{};
            auto heartbeat = co_await coordinator_.heartbeat(token);
            if (heartbeat)
            {
                last_heartbeat_ = std::chrono::steady_clock::now();
                co_return result<void>{};
            }
            auto code = heartbeat.error().code;
            if (code != error_code::rebalance_in_progress &&
                code != error_code::illegal_generation &&
                code != error_code::unknown_member_id &&
                code != error_code::not_coordinator &&
                code != error_code::coordinator_not_available)
                co_return std::unexpected(heartbeat.error());
            if (code == error_code::not_coordinator ||
                code == error_code::coordinator_not_available)
                group_backend_->invalidate_coordinator();
            auto joined = co_await coordinator_.join(topics_, token);
            if (!joined)
                co_return std::unexpected(joined.error());
            replace_assignment(joined->assigned_partitions);
            auto initialized = co_await initialize_assigned_positions(token);
            if (!initialized)
                co_return std::unexpected(initialized.error());
            last_heartbeat_ = std::chrono::steady_clock::now();
            co_return result<void>{};
        }

        struct fetch_session_state
        {
            std::int32_t id = 0;
            std::int32_t epoch = 0;
            std::set<topic_partition> partitions;
        };

        void replace_assignment(std::vector<topic_partition> assignment)
        {
            assigned_ = std::move(assignment);
            std::scoped_lock lock(assignment_snapshot_mutex_);
            assignment_snapshot_ = assigned_;
        }

        void clear_assignment()
        {
            assigned_.clear();
            std::scoped_lock lock(assignment_snapshot_mutex_);
            assignment_snapshot_.clear();
        }

        std::shared_ptr<metadata_cache> metadata_;
        std::shared_ptr<kafka_group_request_backend> group_backend_;
        metadata_refresh_operation refresh_metadata_;
        client_options client_configuration_;
        consumer_options options_;
        group_coordinator coordinator_;
        kafka_request_retry_state_machine retry_;
        async_mutex membership_mutex_;
        std::function<broker_connection*(std::int32_t)> broker_lookup_;
        std::vector<std::string> topics_;
        std::vector<topic_partition> assigned_;
        mutable std::mutex assignment_snapshot_mutex_;
        std::vector<topic_partition> assignment_snapshot_;
        std::map<topic_partition, std::int64_t> positions_;
        std::map<std::int32_t, fetch_session_state> fetch_sessions_;
        compression_registry codecs_;
        std::chrono::steady_clock::time_point last_heartbeat_{};
        std::chrono::steady_clock::time_point last_auto_commit_{};
        std::chrono::steady_clock::time_point last_poll_{};
        bool explicit_assignment_ = false;
        bool closed_ = false;
        bool poll_timeout_ = false;
    };
} // namespace

class kafka_client_runtime_state
{
public:
    kafka_client_runtime_state(io_context& c, client_options o)
        : ctx(c), options(std::move(o)), metadata(std::make_shared<metadata_cache>()) {}

    auto connect(cancel_token* token) -> task<result<void>>
    {
        if (options.bootstrap_servers.empty())
            co_return std::unexpected(
                make_error(error_code::configuration, "bootstrap_servers is empty"));
        error last = make_error(error_code::transport,
            "no bootstrap broker accepted the connection");
        for (auto& e : options.bootstrap_servers)
        {
            auto conn = std::make_unique<broker_connection>(
                ctx, broker_endpoint{-1, e.host, e.port, {}}, options);
            for (auto& o : observers)
                conn->add_observer(o);
            result<void> opened;
            if (token)
                opened = co_await conn->connect(*token);
            else
                opened = co_await conn->connect();
            if (!opened)
            {
                last = opened.error();
                continue;
            }
            auto body = protocol::encode_api_versions();
            auto response = co_await request_with_cancel(
                *conn, protocol::api_key::api_versions, 0, body, token);
            if (!response)
            {
                last = response.error();
                continue;
            }
            auto parsed = protocol::decode_api_versions(*response, 0);
            if (!parsed)
            {
                last = parsed.error();
                continue;
            }
            versions = std::move(parsed->versions);
            bootstrap = std::move(conn);
            co_return result<void>{};
        }
        co_return std::unexpected(last);
    }

    auto refresh(std::vector<std::string> topics, cancel_token* token)
        -> task<result<void>>
    {
        co_return co_await ensure_metadata(std::move(topics), true, token);
    }

    auto ensure_metadata(std::vector<std::string> topics, bool force,
        cancel_token* token) -> task<result<void>>
    {
        auto now = std::chrono::steady_clock::now();
        if (!force &&
            last_metadata_refresh_ != std::chrono::steady_clock::time_point{} &&
            now - last_metadata_refresh_ < options.metadata_refresh_interval)
            co_return result<void>{};
        co_await metadata_refresh_mutex_.lock();
        async_lock_guard guard(metadata_refresh_mutex_, std::adopt_lock);
        now = std::chrono::steady_clock::now();
        if (!force &&
            last_metadata_refresh_ != std::chrono::steady_clock::time_point{} &&
            now - last_metadata_refresh_ < options.metadata_refresh_interval)
            co_return result<void>{};
        if (!bootstrap)
        {
            auto connected = co_await connect(token);
            if (!connected)
                co_return std::unexpected(connected.error());
        }
        auto version = api_version(protocol::api_key::metadata, 0, 7);
        if (version < 0)
            co_return std::unexpected(
                make_error(error_code::unsupported_version,
                    "broker has no compatible non-flexible Metadata version"));
        auto body = protocol::encode_metadata(topics);
        std::vector<broker_connection*> candidates;
        if (bootstrap)
            candidates.push_back(bootstrap.get());
        for (auto& [id, connection] : connections)
            if (connection.get() != bootstrap.get())
                candidates.push_back(connection.get());
        error last =
            make_error(error_code::transport, "no broker returned metadata");
        for (auto* connection : candidates)
        {
            auto response = co_await request_with_cancel(
                *connection, protocol::api_key::metadata, version, body, token);
            if (!response)
            {
                last = response.error();
                continue;
            }
            auto parsed = protocol::decode_metadata(*response, version);
            if (!parsed)
            {
                last = parsed.error();
                continue;
            }
            for (auto& broker : parsed->brokers)
            {
                auto found = connections.find(broker.node_id);
                if (found == connections.end())
                {
                    auto created =
                        std::make_unique<broker_connection>(ctx, broker, options);
                    for (auto& observer : observers)
                        created->add_observer(observer);
                    connections.emplace(broker.node_id, std::move(created));
                }
                else if (found->second->endpoint().host != broker.host ||
                    found->second->endpoint().port != broker.port)
                {
                    found->second->close();
                    auto replacement =
                        std::make_unique<broker_connection>(ctx, broker, options);
                    for (auto& observer : observers)
                        replacement->add_observer(observer);
                    found->second = std::move(replacement);
                }
            }
            metadata->update(std::move(*parsed));
            last_metadata_refresh_ = std::chrono::steady_clock::now();
            co_return result<void>{};
        }
        if (last.code == error_code::transport)
        {
            bootstrap.reset();
            auto reconnected = co_await connect(token);
            if (reconnected)
            {
                auto response = co_await request_with_cancel(
                    *bootstrap, protocol::api_key::metadata, version, body, token);
                if (response)
                {
                    auto parsed = protocol::decode_metadata(*response, version);
                    if (parsed)
                    {
                        for (auto& broker : parsed->brokers)
                            resolve(broker);
                        metadata->update(std::move(*parsed));
                        last_metadata_refresh_ = std::chrono::steady_clock::now();
                        co_return result<void>{};
                    }
                    last = parsed.error();
                }
                else
                    last = response.error();
            }
        }
        co_return std::unexpected(last);
    }

    auto lookup(std::int32_t id) -> broker_connection*
    {
        auto i = connections.find(id);
        return i == connections.end() ? nullptr : i->second.get();
    }

    auto resolve(const broker_endpoint& endpoint) -> broker_connection*
    {
        if (auto* existing = lookup(endpoint.node_id))
            return existing;
        auto connection =
            std::make_unique<broker_connection>(ctx, endpoint, options);
        for (auto& observer : observers)
            connection->add_observer(observer);
        auto* result = connection.get();
        connections.emplace(endpoint.node_id, std::move(connection));
        return result;
    }

    auto api_version(protocol::api_key key, std::int16_t minimum,
        std::int16_t maximum) const -> std::int16_t
    {
        for (auto& version : versions)
            if (version.key == key)
            {
                auto lower = std::max(version.minimum, minimum);
                auto upper = std::min(version.maximum, maximum);
                return lower <= upper ? upper : static_cast<std::int16_t>(-1);
            }
        return static_cast<std::int16_t>(-1);
    }

    io_context& ctx;
    client_options options;
    std::shared_ptr<metadata_cache> metadata;
    std::unique_ptr<broker_connection> bootstrap;
    std::map<std::int32_t, std::unique_ptr<broker_connection>> connections;
    std::vector<protocol::api_version> versions;
    std::vector<std::weak_ptr<connection_observer>> observers;
    async_mutex metadata_refresh_mutex_;
    std::chrono::steady_clock::time_point last_metadata_refresh_{};
};

class client_facade::impl
{
public:
    impl(io_context& context, client_options options_value)
        : lifetime(std::make_shared<kafka_client_runtime_state>(
              context, std::move(options_value))),
          ctx(lifetime->ctx),
          options(lifetime->options),
          metadata(lifetime->metadata),
          bootstrap(lifetime->bootstrap),
          connections(lifetime->connections),
          versions(lifetime->versions),
          observers(lifetime->observers) {}

    auto connect(cancel_token* token) -> task<result<void>>
    {
        co_return co_await lifetime->connect(token);
    }

    auto refresh(std::vector<std::string> topics, cancel_token* token)
        -> task<result<void>>
    {
        co_return co_await lifetime->refresh(std::move(topics), token);
    }

    auto lookup(std::int32_t id) -> broker_connection*
    {
        return lifetime->lookup(id);
    }

    auto resolve(const broker_endpoint& endpoint) -> broker_connection*
    {
        return lifetime->resolve(endpoint);
    }

    auto api_version(protocol::api_key key, std::int16_t minimum,
        std::int16_t maximum) const -> std::int16_t
    {
        return lifetime->api_version(key, minimum, maximum);
    }

    std::shared_ptr<kafka_client_runtime_state> lifetime;
    io_context& ctx;
    client_options& options;
    std::shared_ptr<metadata_cache>& metadata;
    std::unique_ptr<broker_connection>& bootstrap;
    std::map<std::int32_t, std::unique_ptr<broker_connection>>& connections;
    std::vector<protocol::api_version>& versions;
    std::vector<std::weak_ptr<connection_observer>>& observers;
};

client_facade::client_facade(io_context& context, client_options options)
    : impl_(std::make_unique<impl>(context, std::move(options))) {}

client_facade::~client_facade() = default;
client_facade::client_facade(client_facade&&) noexcept = default;
auto client_facade::operator=(client_facade&&) noexcept
    -> client_facade& = default;

auto client_facade::connect(cancel_token* token) -> task<result<void>>
{
    co_return co_await impl_->connect(token);
}

auto client_facade::refresh_metadata(std::vector<std::string> topics,
    cancel_token* token)
    -> task<result<void>>
{
    co_return co_await impl_->refresh(std::move(topics), token);
}

auto client_facade::metadata() const -> std::shared_ptr<metadata_cache>
{
    return impl_->metadata;
}

auto client_facade::api_versions() const
    -> std::span<const protocol::api_version>
{
    return impl_->versions;
}

auto client_facade::make_producer(producer_options options,
    std::unique_ptr<partitioner> strategy)
    -> result<producer>
{
    if (impl_->connections.empty())
        return std::unexpected(
            make_error(error_code::configuration,
                "refresh metadata before creating a producer"));
    auto runtime = impl_->lifetime;
    auto lookup = [runtime](std::int32_t id)
    {
        return runtime->lookup(id);
    };
    auto resolve = [runtime](const broker_endpoint& endpoint)
    {
        return runtime->resolve(endpoint);
    };
    auto seed = [runtime]()
    {
        return runtime->bootstrap.get();
    };
    metadata_refresh_operation refresh =
        [runtime](std::vector<std::string> topics, bool force,
            cancel_token* token) -> task<result<void>>
    {
        co_return co_await runtime->ensure_metadata(std::move(topics), force,
            token);
    };
    return producer(
        std::make_shared<facade_producer_backend>(
            runtime->ctx, runtime->metadata, std::move(lookup),
            std::move(resolve), std::move(seed), std::move(refresh),
            runtime->options,
            compression_registry{},
            runtime->api_version(protocol::api_key::produce, 3, 7),
            runtime->api_version(protocol::api_key::init_producer_id, 0, 1),
            runtime->api_version(protocol::api_key::find_coordinator, 1, 2)),
        std::move(options), std::move(strategy));
}

auto client_facade::make_consumer(consumer_options options)
    -> result<consumer>
{
    if (impl_->connections.empty())
        return std::unexpected(
            make_error(error_code::configuration,
                "refresh metadata before creating a consumer"));
    auto runtime = impl_->lifetime;
    auto lookup = [runtime](std::int32_t id)
    {
        return runtime->lookup(id);
    };
    auto make_coordinator_connection = [runtime](const broker_endpoint& endpoint)
    {
        auto connection = std::make_unique<broker_connection>(
            runtime->ctx, endpoint, runtime->options);
        for (auto& observer : runtime->observers)
            connection->add_observer(observer);
        return connection;
    };
    auto seed = [runtime]()
    {
        return runtime->bootstrap.get();
    };
    metadata_refresh_operation group_refresh =
        [runtime](std::vector<std::string> topics, bool force,
            cancel_token* token) -> task<result<void>>
    {
        co_return co_await runtime->ensure_metadata(std::move(topics), force,
            token);
    };
    metadata_refresh_operation consumer_refresh = group_refresh;
    consumer_group_api_versions versions{
        runtime->api_version(protocol::api_key::find_coordinator, 0, 2),
        runtime->api_version(protocol::api_key::join_group, 0, 5),
        runtime->api_version(protocol::api_key::sync_group, 0, 3),
        runtime->api_version(protocol::api_key::heartbeat, 0, 3),
        runtime->api_version(protocol::api_key::leave_group, 0, 3),
        runtime->api_version(protocol::api_key::offset_fetch, 1, 5),
        runtime->api_version(protocol::api_key::offset_commit, 2, 7),
        runtime->api_version(protocol::api_key::fetch, 4, 11),
        runtime->api_version(protocol::api_key::list_offsets, 0, 5)};
    auto group_backend = std::make_shared<kafka_group_request_backend>(
        runtime->ctx, runtime->metadata, std::move(make_coordinator_connection),
        std::move(seed), std::move(group_refresh), runtime->options, options,
        versions);
    auto backend = std::make_shared<facade_consumer_backend>(
        runtime->ctx, runtime->metadata, std::move(group_backend),
        std::move(consumer_refresh), runtime->options, options);
    backend->set_broker_lookup(std::move(lookup));
    backend->start_background_maintenance(runtime->ctx);
    return consumer(std::move(backend), std::move(options));
}

void client_facade::add_connection_observer(
    std::weak_ptr<connection_observer> observer)
{
    auto runtime = impl_->lifetime;
    runtime->observers.push_back(observer);
    if (runtime->bootstrap)
        runtime->bootstrap->add_observer(observer);
    for (auto& [id, connection] : runtime->connections)
        connection->add_observer(observer);
}

void client_facade::close() noexcept
{
    auto runtime = impl_->lifetime;
    if (runtime->bootstrap)
        runtime->bootstrap->close();
    for (auto& [id, connection] : runtime->connections)
        connection->close();
}
} // namespace cnetmod::kafka
