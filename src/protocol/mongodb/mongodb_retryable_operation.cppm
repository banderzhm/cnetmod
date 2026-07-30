export module cnetmod.protocol.mongodb:retryable_operation;

import std;
import cnetmod.coro.task;
import :error;
import :bson_document;
import :connection_pool;
import :topology_connection_pool;

export namespace cnetmod::mongodb {

enum class operation_kind
{
    read,
    write,
    commit_transaction,
    change_stream_get_more
};

struct retryable_operation_options
{
    bool retry_reads = true;
    bool retry_writes = true;
    std::size_t maximum_attempts = 2;
    std::chrono::milliseconds initial_backoff{10};
    std::chrono::milliseconds maximum_backoff{500};
};

class retryable_operation_policy
{
public:
    explicit retryable_operation_policy(retryable_operation_options options = {});
    [[nodiscard]] auto should_retry(operation_kind operation,
        const error& failure, std::size_t completed_attempts,
        bool acknowledged_write = true) const noexcept -> bool;
    [[nodiscard]] auto backoff(std::size_t completed_attempts) const noexcept
        -> std::chrono::milliseconds;

private:
    retryable_operation_options options_;
};

auto execute_retryable_command(connection_pool& pool, std::string_view database,
    bson_document command_document, operation_kind operation,
    retryable_operation_options options = {}, bool acknowledged_write = true)
    -> task<result<bson_document>>;
auto execute_retryable_command(topology_connection_pool& pool,
    std::string_view database, bson_document command_document,
    operation_kind operation, server_selection_options selection = {},
    retryable_operation_options options = {}, bool acknowledged_write = true)
    -> task<result<bson_document>>;

} // namespace cnetmod::mongodb
