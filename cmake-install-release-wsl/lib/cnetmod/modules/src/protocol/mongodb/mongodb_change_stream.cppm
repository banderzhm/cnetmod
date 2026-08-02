export module cnetmod.protocol.mongodb:change_stream;

import std;
import cnetmod.coro.task;
import :error;
import :bson_document;
import :connection_pool;
import :retryable_operation;

export namespace cnetmod::mongodb {

struct change_stream_options
{
    std::string full_document = "default";
    std::string full_document_before_change;
    std::optional<bson_document> resume_after;
    std::optional<bson_document> start_after;
    std::optional<bson_timestamp> start_at_operation_time;
    std::int32_t batch_size = 100;
    std::chrono::milliseconds maximum_await_time{1000};
    std::vector<bson_document> pipeline;
    retryable_operation_options retry;
};

class change_stream
{
public:
    change_stream(connection_pool& pool, std::string database,
        std::string collection, change_stream_options options = {});
    change_stream(const change_stream&) = delete;
    auto operator=(const change_stream&) -> change_stream& = delete;
    change_stream(change_stream&&) noexcept;
    auto operator=(change_stream&&) noexcept -> change_stream&;
    ~change_stream();

    auto open() -> task<result<void>>;
    auto next() -> task<result<std::optional<bson_document>>>;
    auto close() -> task<void>;
    [[nodiscard]] auto resume_token() const noexcept -> const bson_document*;
    [[nodiscard]] auto cursor_id() const noexcept -> std::int64_t;

private:
    auto open_cursor(bool resuming) -> task<result<void>>;
    auto read_batch(std::string_view batch_field, const bson_document& response)
        -> result<void>;
    connection_pool* pool_;
    std::string database_;
    std::string collection_;
    change_stream_options options_;
    retryable_operation_policy retry_policy_;
    std::optional<pooled_connection> connection_;
    std::int64_t cursor_id_ = 0;
    std::deque<bson_document> buffered_events_;
    std::optional<bson_document> resume_token_;
    bool opened_ = false;
};

} // namespace cnetmod::mongodb
