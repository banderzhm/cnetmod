module cnetmod.protocol.mongodb;

import std;
import cnetmod.coro.timer;
import :error;
import :bson_document;
import :connection;
import :connection_pool;
import :topology_connection_pool;
import :retryable_operation;

namespace cnetmod::mongodb {
retryable_operation_policy::retryable_operation_policy(retryable_operation_options options)
    : options_(std::move(options))
{
    options_.maximum_attempts = std::max<std::size_t>(1, options_.maximum_attempts);
}

auto retryable_operation_policy::should_retry(operation_kind operation,
    const error& failure, std::size_t attempts, bool acknowledged) const noexcept -> bool
{
    if (attempts >= options_.maximum_attempts || !acknowledged)
        return false;
    if (operation == operation_kind::read || operation == operation_kind::change_stream_get_more)
    {
        if (!options_.retry_reads)
            return false;
    }
    else if (!options_.retry_writes)
        return false;
    if (failure.code == error_code::connection_closed || failure.code == error_code::connection_failed ||
        failure.code == error_code::tls_failed ||
        failure.code == error_code::operation_timed_out)
        return true;
    // A transient transaction requires replaying the whole transaction, never
    // blindly replaying a single operation through a generic retry helper.
    if (failure.labels.contains("TransientTransactionError") ||
        failure.labels.contains("NonResumableChangeStreamError"))
        return false;
    if ((operation == operation_kind::write && failure.labels.contains("RetryableWriteError")) ||
        (operation == operation_kind::change_stream_get_more && failure.labels.contains("ResumableChangeStreamError")) ||
        (operation == operation_kind::commit_transaction && failure.labels.contains("UnknownTransactionCommitResult")))
        return true;
    if (operation == operation_kind::change_stream_get_more)
    {
        // CursorNotFound is resumable; history loss and invalidation are not.
        if (failure.server_code == 43)
            return true;
        if (failure.server_code == 136 || failure.server_code == 280 || failure.server_code == 286)
            return false;
    }
    switch (failure.server_code)
    {
    case 6:
    case 7:
    case 89:
    case 91:
    case 189:
    case 262:
    case 9001:
    case 10107:
    case 11600:
    case 11602:
    case 13435:
    case 13436:
        return true;
    default:
        return false;
    }
}

auto retryable_operation_policy::backoff(std::size_t attempts) const noexcept
    -> std::chrono::milliseconds
{
    auto factor = std::uint64_t{1} << std::min<std::size_t>(attempts, 20);
    auto value = std::chrono::milliseconds(options_.initial_backoff.count() * factor);
    return std::min(value, options_.maximum_backoff);
}

auto execute_retryable_command(connection_pool& pool, std::string_view database,
    bson_document command_document, operation_kind operation,
    retryable_operation_options options, bool acknowledged) -> task<result<bson_document>>
{
    retryable_operation_policy policy(options);
    bson_binary session_id{.subtype = 4, .bytes = std::vector<std::byte>(16)};
    if (operation == operation_kind::write)
    {
        std::random_device source;
        for (auto& byte : session_id.bytes)
            byte = static_cast<std::byte>(source() & 0xffu);
        session_id.bytes[6] = (session_id.bytes[6] & std::byte{0x0f}) | std::byte{0x40};
        session_id.bytes[8] = (session_id.bytes[8] & std::byte{0x3f}) | std::byte{0x80};
        command_document.set("lsid", bson_document{{"id", session_id}});
        command_document.set("txnNumber", std::int64_t{1});
    }
    for (std::size_t attempt = 1;; ++attempt)
    {
        auto acquired = co_await pool.acquire();
        if (!acquired)
        {
            if (!policy.should_retry(operation, acquired.error(), attempt, acknowledged))
                co_return std::unexpected(acquired.error());
            co_await async_sleep(pool.context(), policy.backoff(attempt));
            continue;
        }
        auto response = co_await (*acquired)->command(database, command_document);
        if (response)
            co_return response;
        auto failure = response.error();
        if (!(*acquired)->is_open())
            acquired->discard();
        if (!policy.should_retry(operation, failure, attempt, acknowledged))
            co_return std::unexpected(std::move(failure));
        acquired->discard();
        co_await async_sleep(pool.context(), policy.backoff(attempt));
    }
}

auto execute_retryable_command(topology_connection_pool& pool,
    std::string_view database, bson_document command_document,
    operation_kind operation, server_selection_options selection,
    retryable_operation_options options, bool acknowledged) -> task<result<bson_document>>
{
    retryable_operation_policy policy(options);
    bson_binary session_id{.subtype = 4, .bytes = std::vector<std::byte>(16)};
    if (operation == operation_kind::write)
    {
        std::random_device source;
        for (auto& byte : session_id.bytes)
            byte = static_cast<std::byte>(source() & 0xffu);
        session_id.bytes[6] = (session_id.bytes[6] & std::byte{0x0f}) | std::byte{0x40};
        session_id.bytes[8] = (session_id.bytes[8] & std::byte{0x3f}) | std::byte{0x80};
        command_document.set("lsid", bson_document{{"id", session_id}});
        command_document.set("txnNumber", std::int64_t{1});
    }
    for (std::size_t attempt = 1;; ++attempt)
    {
        auto response = co_await pool.command(database, command_document, selection);
        if (response)
            co_return response;
        if (!policy.should_retry(operation, response.error(), attempt, acknowledged))
            co_return std::unexpected(response.error());
        auto refreshed = co_await pool.refresh();
        if (!refreshed && attempt + 1 >= options.maximum_attempts)
            co_return std::unexpected(refreshed.error());
    }
}
} // namespace cnetmod::mongodb
