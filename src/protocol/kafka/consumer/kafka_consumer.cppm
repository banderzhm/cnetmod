module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.kafka.kafka_consumer;
import std;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.protocol.kafka.protocol_constants;
import cnetmod.protocol.kafka.offset_manager;

export namespace cnetmod::kafka {
enum class consumer_assignment_policy
{
    range,
    cooperative_sticky
};
enum class offset_reset_policy
{
    earliest,
    latest,
    error
};

struct consumer_options
{
    std::string group_id;
    std::optional<std::string> group_instance_id;
    std::chrono::milliseconds session_timeout{45000};
    std::chrono::milliseconds heartbeat_interval{3000};
    std::chrono::milliseconds max_poll_interval{300000};
    std::size_t max_poll_records = 500;
    std::int32_t fetch_min_bytes = 1;
    std::int32_t fetch_max_bytes = 50 * 1024 * 1024;
    isolation_level isolation = isolation_level::read_uncommitted;
    consumer_assignment_policy assignment_policy =
        consumer_assignment_policy::cooperative_sticky;
    offset_reset_policy auto_offset_reset = offset_reset_policy::earliest;
    bool enable_auto_commit = true;
    std::chrono::milliseconds auto_commit_interval{5000};
};

class consumer_backend
{
public:
    virtual ~consumer_backend() = default;
    virtual auto subscribe(std::span<const std::string>, cancel_token*)
        -> task<result<void>> = 0;
    virtual auto assign(std::span<const topic_partition>, cancel_token*)
        -> task<result<void>> = 0;
    virtual auto poll(std::size_t, cancel_token*)
        -> task<result<std::vector<consumed_record>>> = 0;
    [[nodiscard]] virtual auto assignment() const
        -> std::vector<topic_partition> = 0;
    virtual auto seek(const topic_partition&, std::int64_t, cancel_token*)
        -> task<result<void>> = 0;
    virtual auto commit(const std::map<topic_partition, offset_and_metadata>&,
        cancel_token*) -> task<result<void>> = 0;
    virtual auto close(cancel_token*) -> task<result<void>> = 0;
};

class consumer
{
public:
    consumer(std::shared_ptr<consumer_backend>, consumer_options);
    ~consumer();
    consumer(consumer&&) noexcept;
    auto operator=(consumer&&) noexcept -> consumer&;
    auto subscribe(std::vector<std::string>, cancel_token* = nullptr)
        -> task<result<void>>;
    auto assign(std::vector<topic_partition>, cancel_token* = nullptr)
        -> task<result<void>>;
    auto poll(cancel_token* = nullptr)
        -> task<result<std::vector<consumed_record>>>;
    [[nodiscard]] auto assignment() const -> std::vector<topic_partition>;
    auto commit(const consumed_record&, cancel_token* = nullptr)
        -> task<result<void>>;
    auto seek(topic_partition, std::int64_t, cancel_token* = nullptr)
        -> task<result<void>>;
    auto close(cancel_token* = nullptr) -> task<result<void>>;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};
} // namespace cnetmod::kafka
