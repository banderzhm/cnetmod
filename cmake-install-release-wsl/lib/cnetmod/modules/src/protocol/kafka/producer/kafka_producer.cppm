module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.kafka.kafka_producer;
import std;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.protocol.kafka.protocol_constants;
import cnetmod.protocol.kafka.partitioner;
import cnetmod.protocol.kafka.record_batch;
import cnetmod.protocol.kafka.offset_manager;

export namespace cnetmod::kafka {
enum class producer_transaction_state
{
    disabled,
    ready,
    in_transaction,
    committing,
    aborting,
    fatal
};

struct producer_options
{
    acknowledgement acks = acknowledgement::all;
    compression compression_type = compression::none;
    std::size_t batch_bytes = 1024 * 1024;
    std::chrono::milliseconds linger{5};
    std::chrono::milliseconds delivery_timeout{120000};
    std::chrono::milliseconds transaction_timeout{60000};
    bool idempotent = true;
    std::optional<std::string> transactional_id;
    std::size_t max_in_flight = 5;
};

struct record_metadata
{
    topic_partition target;
    std::int64_t offset = -1;
    std::int64_t timestamp = -1;
};

class producer_backend
{
public:
    virtual ~producer_backend() = default;
    virtual auto partitions(std::string_view)
        -> result<std::vector<std::int32_t>> = 0;
    virtual auto initialize_idempotent(std::optional<std::string_view>,
        std::chrono::milliseconds, cancel_token*)
        -> task<result<std::pair<std::int64_t, std::int16_t>>> = 0;
    virtual auto wait_for_linger(std::chrono::milliseconds, cancel_token*)
        -> task<result<void>> = 0;
    virtual auto send_batch(const topic_partition&, std::span<const record>,
        const record_batch_options&, acknowledgement,
        std::chrono::steady_clock::time_point, cancel_token*)
        -> task<result<std::vector<record_metadata>>> = 0;
    virtual auto add_transaction_partitions(
        std::string_view, std::int64_t, std::int16_t,
        std::span<const topic_partition>, cancel_token*) -> task<result<void>> = 0;
    virtual auto add_transaction_offsets(
        std::string_view, std::int64_t, std::int16_t, std::string_view,
        const std::map<topic_partition, offset_and_metadata>&, cancel_token*)
        -> task<result<void>> = 0;
    virtual auto finish_transaction(std::string_view, std::int64_t, std::int16_t,
        bool, cancel_token*) -> task<result<void>> = 0;
};

class producer
{
public:
    producer(std::shared_ptr<producer_backend>, producer_options = {},
        std::unique_ptr<partitioner> = {});
    ~producer();
    producer(producer&&) noexcept;
    auto operator=(producer&&) noexcept -> producer&;
    auto send(std::string topic, record) -> task<result<record_metadata>>;
    auto send(std::string topic, record, cancel_token&)
        -> task<result<record_metadata>>;
    auto flush() -> task<result<void>>;
    auto begin_transaction(cancel_token* = nullptr) -> task<result<void>>;
    auto send_offsets_to_transaction(
        std::string_view,
        const std::map<topic_partition, offset_and_metadata>&,
        cancel_token* = nullptr) -> task<result<void>>;
    auto commit_transaction(cancel_token* = nullptr) -> task<result<void>>;
    auto abort_transaction(cancel_token* = nullptr) -> task<result<void>>;
    [[nodiscard]] auto transaction_state() const noexcept
        -> producer_transaction_state;
    [[nodiscard]] auto producer_identity() const noexcept
        -> std::optional<std::pair<std::int64_t, std::int16_t>>;
    void close() noexcept;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};
} // namespace cnetmod::kafka
