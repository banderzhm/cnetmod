module cnetmod.protocol.mongodb;

import std;
import cnetmod.coro.timer;
import :error;
import :bson_document;
import :connection;
import :connection_pool;
import :retryable_operation;
import :client_session;

namespace cnetmod::mongodb {
client_session::client_session()
{
    id_.subtype = 4;
    id_.bytes.resize(16);
    std::random_device source;
    for (auto& byte : id_.bytes)
        byte = static_cast<std::byte>(source() & 0xffu);
    id_.bytes[6] = (id_.bytes[6] & std::byte{0x0f}) | std::byte{0x40};
    id_.bytes[8] = (id_.bytes[8] & std::byte{0x3f}) | std::byte{0x80};
}

client_session::client_session(client_session&&) noexcept = default;
auto client_session::operator=(client_session&&) noexcept -> client_session& = default;
client_session::~client_session() = default;

auto client_session::start_transaction(transaction_options options) -> result<void>
{
    if (state_ == transaction_state::starting || state_ == transaction_state::in_progress)
        return std::unexpected(make_error(error_code::transaction_failed,
            "MongoDB transaction is already active"));
    ++transaction_number_;
    state_ = transaction_state::starting;
    transaction_options_ = std::move(options);
    pinned_connection_.reset();
    return {};
}

auto client_session::connection_for(connection_pool& pool) -> task<result<connection*>>
{
    if (pinned_connection_)
        co_return &pinned_connection_->get();
    auto acquired = co_await pool.acquire();
    if (!acquired)
        co_return std::unexpected(acquired.error());
    pinned_connection_.emplace(std::move(*acquired));
    co_return &pinned_connection_->get();
}

void client_session::decorate(bson_document& command_document)
{
    command_document.set("lsid", bson_document{{"id", id_}});
    if (state_ == transaction_state::starting || state_ == transaction_state::in_progress)
    {
        command_document.set("txnNumber", transaction_number_);
        command_document.set("autocommit", false);
        if (state_ == transaction_state::starting)
        {
            command_document.set("startTransaction", true);
            if (transaction_options_.read_concern_level)
                command_document.set("readConcern", bson_document{{"level", *transaction_options_.read_concern_level}});
            state_ = transaction_state::in_progress;
        }
    }
}

auto client_session::command(connection_pool& pool, std::string_view database,
    bson_document document) -> task<result<bson_document>>
{
    auto client = co_await connection_for(pool);
    if (!client)
        co_return std::unexpected(client.error());
    decorate(document);
    auto response = co_await (*client)->command(database, std::move(document));
    if (!response && pinned_connection_ && !pinned_connection_->valid())
        pinned_connection_->discard();
    if (state_ == transaction_state::none || state_ == transaction_state::committed ||
        state_ == transaction_state::aborted)
        pinned_connection_.reset();
    co_return response;
}

auto client_session::commit_transaction(connection_pool& pool) -> task<result<void>>
{
    if (state_ != transaction_state::starting && state_ != transaction_state::in_progress)
        co_return std::unexpected(make_error(error_code::transaction_failed,
            "no active MongoDB transaction to commit"));
    const auto attempts = std::max<std::size_t>(1, transaction_options_.maximum_commit_attempts);
    retryable_operation_policy retry_policy({.retry_reads = false,
        .retry_writes = true,
        .maximum_attempts = attempts,
        .initial_backoff = transaction_options_.commit_retry_backoff,
        .maximum_backoff = transaction_options_.commit_retry_backoff});
    for (std::size_t attempt = 1; attempt <= attempts; ++attempt)
    {
        auto client = co_await connection_for(pool);
        if (!client)
            co_return std::unexpected(client.error());
        bson_document command{{"commitTransaction", std::int32_t{1}},
            {"lsid", bson_document{{"id", id_}}}, {"txnNumber", transaction_number_},
            {"autocommit", false}};
        if (transaction_options_.write_concern)
            command.append("writeConcern", bson_document{{"w", *transaction_options_.write_concern}});
        if (transaction_options_.maximum_commit_time)
            command.append("maxTimeMS", static_cast<std::int64_t>(transaction_options_.maximum_commit_time->count()));
        auto response = co_await (*client)->command("admin", std::move(command));
        if (response)
        {
            state_ = transaction_state::committed;
            pinned_connection_.reset();
            co_return result<void>{};
        }
        const bool retry = retry_policy.should_retry(
            operation_kind::commit_transaction, response.error(), attempt);
        if (!retry)
            co_return std::unexpected(response.error());
        if (pinned_connection_ && !pinned_connection_->valid())
        {
            pinned_connection_->discard();
            pinned_connection_.reset();
        }
        co_await async_sleep(pool.context(), transaction_options_.commit_retry_backoff);
    }
    co_return std::unexpected(make_error(error_code::transaction_failed,
        "MongoDB commit retry attempts exhausted"));
}

auto client_session::abort_transaction(connection_pool& pool) -> task<result<void>>
{
    if (state_ != transaction_state::starting && state_ != transaction_state::in_progress)
        co_return std::unexpected(make_error(error_code::transaction_failed,
            "no active MongoDB transaction to abort"));
    auto client = co_await connection_for(pool);
    if (!client)
        co_return std::unexpected(client.error());
    bson_document command{{"abortTransaction", std::int32_t{1}},
        {"lsid", bson_document{{"id", id_}}}, {"txnNumber", transaction_number_},
        {"autocommit", false}};
    auto response = co_await (*client)->command("admin", std::move(command));
    state_ = transaction_state::aborted;
    pinned_connection_.reset();
    if (!response)
        co_return std::unexpected(response.error());
    co_return result<void>{};
}

void client_session::reset() noexcept
{
    pinned_connection_.reset();
    state_ = transaction_state::none;
}

auto client_session::id() const noexcept -> const bson_binary&
{
    return id_;
}

auto client_session::state() const noexcept -> transaction_state
{
    return state_;
}

auto client_session::transaction_number() const noexcept -> std::int64_t
{
    return transaction_number_;
}

auto client_session::has_pinned_connection() const noexcept -> bool
{
    return pinned_connection_.has_value();
}
} // namespace cnetmod::mongodb
