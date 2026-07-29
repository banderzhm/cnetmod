module cnetmod.protocol.kafka.kafka_producer;
import std;
import cnetmod.coro.mutex;
import cnetmod.coro.semaphore;

namespace cnetmod::kafka {
namespace {
    struct pending_record_send
    {
        record value;
        std::chrono::steady_clock::time_point deadline;
        std::optional<result<record_metadata>> outcome;
        std::coroutine_handle<> waiter{};
    };

    struct pending_record_awaiter
    {
        std::shared_ptr<pending_record_send> state;

        auto await_ready() const noexcept -> bool
        {
            return state->outcome.has_value();
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept
        {
            state->waiter = handle;
        }

        auto await_resume() -> result<record_metadata>
        {
            return std::move(*state->outcome);
        }
    };

    auto estimated_record_bytes(const record& value) -> std::size_t
    {
        std::size_t size = 64;
        if (value.key)
            size += value.key->size();
        if (value.value)
            size += value.value->size();
        for (auto& header : value.headers)
            size += header.key.size() + header.value.size() + 16;
        return size;
    }

    void complete_send(const std::shared_ptr<pending_record_send>& state,
        result<record_metadata> outcome)
    {
        state->outcome = std::move(outcome);
        if (state->waiter)
        {
            auto waiter = std::exchange(state->waiter, {});
            waiter.resume();
        }
    }
} // namespace

class producer::impl
{
    struct partition_batch
    {
        std::vector<std::shared_ptr<pending_record_send>> pending;
        std::shared_ptr<pending_record_send> inflight_tail;
        bool flushing = false;
    };

    class in_flight_permit
    {
    public:
        explicit in_flight_permit(async_semaphore& semaphore)
            : semaphore_(semaphore) {}

        ~in_flight_permit()
        {
            semaphore_.release();
        }

        in_flight_permit(const in_flight_permit&) = delete;
        auto operator=(const in_flight_permit&) -> in_flight_permit& = delete;

    private:
        async_semaphore& semaphore_;
    };

public:
    impl(std::shared_ptr<producer_backend> backend_value,
        producer_options option_value,
        std::unique_ptr<partitioner> partition_strategy)
        : backend(std::move(backend_value)), options(std::move(option_value)), strategy(std::move(partition_strategy)), in_flight_window(std::max<std::size_t>(1, options.max_in_flight))
    {
        if (!strategy)
            strategy = std::make_unique<murmur2_partitioner>();
        transaction_status = options.transactional_id
            ? producer_transaction_state::ready
            : producer_transaction_state::disabled;
    }

    auto initialize(cancel_token* token) -> task<result<void>>
    {
        if (initialized || (!options.idempotent && !options.transactional_id))
            co_return result<void>{};
        co_await initialization_mutex.lock();
        async_lock_guard guard(initialization_mutex, std::adopt_lock);
        if (initialized)
            co_return result<void>{};
        if (options.idempotent && options.acks != acknowledgement::all)
            co_return std::unexpected(
                make_error(error_code::configuration,
                    "idempotent producer requires acknowledgement::all"));
        auto transactional =
            options.transactional_id
            ? std::optional<std::string_view>{*options.transactional_id}
            : std::nullopt;
        auto identity = co_await backend->initialize_idempotent(
            transactional,
            transactional ? options.transaction_timeout : options.delivery_timeout,
            token);
        if (!identity)
            co_return std::unexpected(identity.error());
        producer_id = identity->first;
        producer_epoch = identity->second;
        initialized = true;
        co_return result<void>{};
    }

    auto send(std::string topic, record value, cancel_token* token)
        -> task<result<record_metadata>>
    {
        if (closed)
            co_return std::unexpected(
                make_error(error_code::configuration, "producer is closed"));
        if (options.max_in_flight == 0)
            co_return std::unexpected(
                make_error(error_code::configuration,
                    "max_in_flight must be greater than zero"));
        if (options.idempotent && options.max_in_flight > 5)
            co_return std::unexpected(
                make_error(error_code::configuration,
                    "idempotent producer requires max_in_flight <= 5"));
        if (options.delivery_timeout <= std::chrono::milliseconds::zero())
            co_return std::unexpected(
                make_error(error_code::configuration,
                    "delivery_timeout must be greater than zero"));
        if (options.transactional_id &&
            transaction_status != producer_transaction_state::in_transaction)
            co_return std::unexpected(make_error(
                error_code::configuration,
                "transactional producer send requires an active transaction"));
        auto ready = co_await initialize(token);
        if (!ready)
            co_return std::unexpected(ready.error());
        auto partitions = backend->partitions(topic);
        if (!partitions)
            co_return std::unexpected(partitions.error());
        std::span<const std::byte> key =
            value.key ? std::span<const std::byte>(*value.key)
                      : std::span<const std::byte>{};
        auto selected = value.destination
            ? result<std::int32_t>{value.destination->partition}
            : strategy->select(topic, key, *partitions);
        if (!selected)
            co_return std::unexpected(selected.error());
        topic_partition destination{std::move(topic), *selected};
        value.destination = destination;
        auto pending = std::make_shared<pending_record_send>();
        pending->value = std::move(value);
        pending->deadline =
            std::chrono::steady_clock::now() + options.delivery_timeout;
        auto& batch = batches[destination];
        bool starts_linger = batch.pending.empty() && !batch.flushing;
        batch.pending.push_back(pending);
        if (starts_linger)
        {
            if (options.linger > std::chrono::milliseconds::zero())
            {
                auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    pending->deadline - std::chrono::steady_clock::now());
                if (remaining <= std::chrono::milliseconds::zero())
                    co_return std::unexpected(
                        make_error(error_code::request_timed_out,
                            "delivery timeout expired before linger"));
                auto waited = co_await backend->wait_for_linger(
                    std::min(options.linger, remaining), token);
                if (!waited)
                {
                    auto failed = std::move(batch.pending);
                    batch.pending.clear();
                    for (auto& item : failed)
                        complete_send(item, std::unexpected(waited.error()));
                    co_return std::unexpected(waited.error());
                }
            }
            auto flushed = co_await flush_partition(destination, token);
            if (!flushed && !pending->outcome)
                complete_send(pending, std::unexpected(flushed.error()));
            if (!pending->outcome)
                co_return std::unexpected(
                    make_error(error_code::transport,
                        "producer batch completed without a result"));
            co_return std::move(*pending->outcome);
        }
        co_return co_await pending_record_awaiter{pending};
    }

    auto flush_partition(const topic_partition& destination, cancel_token* token)
        -> task<result<void>>
    {
        auto found = batches.find(destination);
        if (found == batches.end())
            co_return result<void>{};
        auto& batch = found->second;
        if (batch.flushing)
        {
            auto target =
                batch.pending.empty() ? batch.inflight_tail : batch.pending.back();
            if (!target)
                co_return result<void>{};
            auto outcome = co_await pending_record_awaiter{target};
            if (!outcome)
                co_return std::unexpected(outcome.error());
            co_return result<void>{};
        }
        if (batch.pending.empty())
            co_return result<void>{};
        batch.flushing = true;
        auto pending = std::move(batch.pending);
        batch.pending.clear();
        batch.inflight_tail = pending.back();
        std::size_t begin = 0;
        while (begin < pending.size())
        {
            std::size_t end = begin;
            std::size_t bytes = 0;
            while (end < pending.size())
            {
                auto next = estimated_record_bytes(pending[end]->value);
                if (end > begin && bytes + next > options.batch_bytes)
                    break;
                bytes += next;
                ++end;
            }
            std::vector<record> records;
            records.reserve(end - begin);
            auto deadline = pending[begin]->deadline;
            for (std::size_t i = begin; i < end; ++i)
            {
                records.push_back(pending[i]->value);
                deadline = std::min(deadline, pending[i]->deadline);
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                auto failure = make_error(error_code::request_timed_out,
                    "producer delivery timeout expired");
                for (std::size_t i = begin; i < end; ++i)
                    complete_send(pending[i], std::unexpected(failure));
                begin = end;
                continue;
            }
            co_await in_flight_window.acquire();
            in_flight_permit permit(in_flight_window);
            auto batch_identity_generation = identity_generation;
            record_batch_options batch_options{
                .compression_type = options.compression_type,
                .transactional_id = options.transactional_id,
                .producer_id = producer_id,
                .producer_epoch = producer_epoch,
                .base_sequence = sequence[destination],
                .transactional = options.transactional_id.has_value()};
            if (options.transactional_id &&
                !transaction_partitions.contains(destination))
            {
                std::array<topic_partition, 1> added{destination};
                auto registered = co_await backend->add_transaction_partitions(
                    *options.transactional_id, producer_id, producer_epoch, added,
                    token);
                if (!registered)
                {
                    transaction_status = producer_transaction_state::fatal;
                    for (std::size_t i = begin; i < end; ++i)
                        complete_send(pending[i], std::unexpected(registered.error()));
                    begin = end;
                    continue;
                }
                transaction_partitions.insert(destination);
            }
            auto sent = co_await backend->send_batch(
                destination, records, batch_options, options.acks, deadline, token);
            if (!sent &&
                (sent.error().code == error_code::invalid_producer_epoch ||
                    sent.error().code == error_code::out_of_order_sequence_number) &&
                options.idempotent && !options.transactional_id)
            {
                co_await identity_recovery_mutex.lock();
                async_lock_guard recovery_guard(identity_recovery_mutex,
                    std::adopt_lock);
                if (batch_identity_generation == identity_generation)
                {
                    initialized = false;
                    sequence.clear();
                    auto recovered = co_await initialize(token);
                    if (!recovered)
                        sent = std::unexpected(recovered.error());
                    else
                        ++identity_generation;
                }
                if (initialized)
                {
                    batch_identity_generation = identity_generation;
                    batch_options.producer_id = producer_id;
                    batch_options.producer_epoch = producer_epoch;
                    batch_options.base_sequence = sequence[destination];
                    sent =
                        co_await backend->send_batch(destination, records, batch_options,
                            options.acks, deadline, token);
                }
            }
            if (!sent)
            {
                if (options.transactional_id)
                    transaction_status = producer_transaction_state::fatal;
                for (std::size_t i = begin; i < end; ++i)
                    complete_send(pending[i], std::unexpected(sent.error()));
            }
            else if (sent->size() != end - begin)
            {
                auto failure =
                    make_error(error_code::malformed_response,
                        "Produce result count does not match the batch");
                for (std::size_t i = begin; i < end; ++i)
                    complete_send(pending[i], std::unexpected(failure));
            }
            else
            {
                for (std::size_t i = begin; i < end; ++i)
                    complete_send(pending[i], std::move((*sent)[i - begin]));
                if (options.idempotent &&
                    batch_identity_generation == identity_generation)
                    sequence[destination] = batch_options.base_sequence +
                        static_cast<std::int32_t>(end - begin);
            }
            begin = end;
        }
        batch.inflight_tail.reset();
        batch.flushing = false;
        if (!batch.pending.empty())
            co_return co_await flush_partition(destination, token);
        co_return result<void>{};
    }

    auto flush(cancel_token* token = nullptr) -> task<result<void>>
    {
        std::vector<topic_partition> destinations;
        for (auto& [destination, batch] : batches)
            if (!batch.pending.empty() || batch.flushing)
                destinations.push_back(destination);
        for (auto& destination : destinations)
        {
            auto flushed = co_await flush_partition(destination, token);
            if (!flushed)
                co_return std::unexpected(flushed.error());
        }
        co_return result<void>{};
    }

    auto begin_transaction(cancel_token* token) -> task<result<void>>
    {
        if (!options.transactional_id)
            co_return std::unexpected(make_error(
                error_code::configuration, "producer is not transactional"));
        if (transaction_status == producer_transaction_state::in_transaction)
            co_return std::unexpected(make_error(
                error_code::configuration, "transaction is already active"));
        if (transaction_status == producer_transaction_state::fatal)
            co_return std::unexpected(make_error(
                error_code::configuration, "transactional producer is in fatal state"));
        auto ready = co_await initialize(token);
        if (!ready)
        {
            transaction_status = producer_transaction_state::fatal;
            co_return std::unexpected(ready.error());
        }
        transaction_partitions.clear();
        transaction_status = producer_transaction_state::in_transaction;
        co_return result<void>{};
    }

    auto send_offsets(
        std::string_view group,
        const std::map<topic_partition, offset_and_metadata>& offsets,
        cancel_token* token) -> task<result<void>>
    {
        if (!options.transactional_id ||
            transaction_status != producer_transaction_state::in_transaction)
            co_return std::unexpected(
                make_error(error_code::configuration, "no active transaction"));
        auto sent = co_await backend->add_transaction_offsets(
            *options.transactional_id, producer_id, producer_epoch, group, offsets,
            token);
        if (!sent)
            transaction_status = producer_transaction_state::fatal;
        co_return sent;
    }

    auto finish_transaction(bool commit, cancel_token* token)
        -> task<result<void>>
    {
        if (!options.transactional_id ||
            transaction_status != producer_transaction_state::in_transaction)
            co_return std::unexpected(
                make_error(error_code::configuration, "no active transaction"));
        transaction_status = commit ? producer_transaction_state::committing
                                    : producer_transaction_state::aborting;
        auto flushed = co_await flush(token);
        if (!flushed)
        {
            transaction_status = producer_transaction_state::fatal;
            co_return std::unexpected(flushed.error());
        }
        auto finished = co_await backend->finish_transaction(
            *options.transactional_id, producer_id, producer_epoch, commit, token);
        transaction_status = finished ? producer_transaction_state::ready
                                      : producer_transaction_state::fatal;
        if (finished)
        {
            transaction_partitions.clear();
        }
        co_return finished;
    }

    void close() noexcept
    {
        closed = true;
        auto failure =
            make_error(error_code::configuration,
                "producer closed before the pending record was sent");
        for (auto& entry : batches)
        {
            auto& batch = entry.second;
            for (auto& pending : batch.pending)
                complete_send(pending, std::unexpected(failure));
            batch.pending.clear();
        }
    }

    std::shared_ptr<producer_backend> backend;
    producer_options options;
    std::unique_ptr<partitioner> strategy;
    std::map<topic_partition, std::int32_t> sequence;
    std::set<topic_partition> transaction_partitions;
    std::map<topic_partition, partition_batch> batches;
    async_mutex initialization_mutex;
    async_mutex identity_recovery_mutex;
    async_semaphore in_flight_window;
    std::int64_t producer_id = -1;
    std::int16_t producer_epoch = -1;
    std::uint64_t identity_generation = 0;
    bool initialized = false;
    bool closed = false;
    producer_transaction_state transaction_status =
        producer_transaction_state::disabled;
};

producer::producer(std::shared_ptr<producer_backend> backend,
    producer_options options,
    std::unique_ptr<partitioner> strategy)
    : impl_(std::make_unique<impl>(std::move(backend), std::move(options),
          std::move(strategy))) {}

producer::~producer() = default;
producer::producer(producer&&) noexcept = default;
auto producer::operator=(producer&&) noexcept -> producer& = default;

auto producer::send(std::string topic, record value)
    -> task<result<record_metadata>>
{
    co_return co_await impl_->send(std::move(topic), std::move(value), nullptr);
}

auto producer::send(std::string topic, record value, cancel_token& token)
    -> task<result<record_metadata>>
{
    co_return co_await impl_->send(std::move(topic), std::move(value), &token);
}

auto producer::flush() -> task<result<void>>
{
    co_return co_await impl_->flush();
}

auto producer::begin_transaction(cancel_token* token) -> task<result<void>>
{
    co_return co_await impl_->begin_transaction(token);
}

auto producer::send_offsets_to_transaction(
    std::string_view group,
    const std::map<topic_partition, offset_and_metadata>& offsets,
    cancel_token* token) -> task<result<void>>
{
    co_return co_await impl_->send_offsets(group, offsets, token);
}

auto producer::commit_transaction(cancel_token* token) -> task<result<void>>
{
    co_return co_await impl_->finish_transaction(true, token);
}

auto producer::abort_transaction(cancel_token* token) -> task<result<void>>
{
    co_return co_await impl_->finish_transaction(false, token);
}

auto producer::transaction_state() const noexcept -> producer_transaction_state
{
    return impl_->transaction_status;
}

auto producer::producer_identity() const noexcept
    -> std::optional<std::pair<std::int64_t, std::int16_t>>
{
    if (!impl_->initialized)
        return std::nullopt;
    return std::pair{impl_->producer_id, impl_->producer_epoch};
}

void producer::close() noexcept
{
    impl_->close();
}
} // namespace cnetmod::kafka
