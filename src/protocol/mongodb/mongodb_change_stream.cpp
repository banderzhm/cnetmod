module cnetmod.protocol.mongodb;

import std;
import cnetmod.coro.timer;
import :error;
import :bson_document;
import :connection;
import :connection_pool;
import :retryable_operation;
import :change_stream;

namespace cnetmod::mongodb {
namespace {
    auto integer(const bson_value* value) -> std::optional<std::int64_t>
    {
        if (!value)
            return {};
        if (auto number = value->get_if<std::int64_t>())
            return *number;
        if (auto number = value->get_if<std::int32_t>())
            return *number;
        return {};
    }
} // namespace

change_stream::change_stream(connection_pool& pool, std::string database,
    std::string collection, change_stream_options options)
    : pool_(&pool), database_(std::move(database)), collection_(std::move(collection)), options_(std::move(options)), retry_policy_(options_.retry)
{
    options_.batch_size = std::max<std::int32_t>(1, options_.batch_size);
}

change_stream::change_stream(change_stream&&) noexcept = default;
auto change_stream::operator=(change_stream&&) noexcept -> change_stream& = default;
change_stream::~change_stream() = default;

auto change_stream::read_batch(std::string_view field, const bson_document& response)
    -> result<void>
{
    auto cursor_value = response.find("cursor");
    auto cursor = cursor_value ? cursor_value->as_document() : nullptr;
    if (!cursor)
        return std::unexpected(make_error(error_code::protocol_error,
            "MongoDB change stream response has no cursor document"));
    auto id = integer(cursor->find("id"));
    if (!id)
        return std::unexpected(make_error(error_code::protocol_error,
            "MongoDB change stream response has no cursor id"));
    cursor_id_ = *id;
    if (auto token = cursor->find("postBatchResumeToken"); token && token->as_document())
        resume_token_ = *token->as_document();
    if (auto batch = cursor->find(field); batch && batch->as_array())
        for (const auto& event : *batch->as_array())
            if (auto document = event.as_document())
                buffered_events_.push_back(*document);
            else
                return std::unexpected(make_error(error_code::protocol_error,
                    "MongoDB change stream batch contains a non-document event"));
    return {};
}

auto change_stream::open_cursor(bool resuming) -> task<result<void>>
{
    if (!connection_)
    {
        auto acquired = co_await pool_->acquire();
        if (!acquired)
            co_return std::unexpected(acquired.error());
        connection_.emplace(std::move(*acquired));
    }
    bson_document stage;
    if (options_.full_document != "default")
        stage.append("fullDocument", options_.full_document);
    if (!options_.full_document_before_change.empty())
        stage.append("fullDocumentBeforeChange", options_.full_document_before_change);
    if (resuming && resume_token_)
        stage.append("resumeAfter", *resume_token_);
    else if (options_.resume_after)
        stage.append("resumeAfter", *options_.resume_after);
    else if (options_.start_after)
        stage.append("startAfter", *options_.start_after);
    else if (options_.start_at_operation_time)
        stage.append("startAtOperationTime", *options_.start_at_operation_time);
    bson_array pipeline{bson_value{bson_document{{"$changeStream", std::move(stage)}}}};
    for (const auto& item : options_.pipeline)
        pipeline.emplace_back(item);
    bson_document command{{"aggregate", collection_}, {"pipeline", std::move(pipeline)},
        {"cursor", bson_document{{"batchSize", options_.batch_size}}}};
    auto response = co_await (*connection_)->command(database_, std::move(command));
    if (!response)
        co_return std::unexpected(response.error());
    buffered_events_.clear();
    auto parsed = read_batch("firstBatch", *response);
    if (!parsed)
        co_return std::unexpected(parsed.error());
    opened_ = true;
    co_return result<void>{};
}

auto change_stream::open() -> task<result<void>>
{
    if (opened_)
        co_return result<void>{};
    co_return co_await open_cursor(false);
}

auto change_stream::next() -> task<result<std::optional<bson_document>>>
{
    if (!opened_)
    {
        auto started = co_await open();
        if (!started)
            co_return std::unexpected(started.error());
    }
    for (std::size_t attempt = 1;; ++attempt)
    {
        if (!buffered_events_.empty())
        {
            auto event = std::move(buffered_events_.front());
            buffered_events_.pop_front();
            if (auto token = event.find("_id"); token && token->as_document())
                resume_token_ = *token->as_document();
            co_return std::optional<bson_document>{std::move(event)};
        }
        if (cursor_id_ == 0)
            co_return std::optional<bson_document>{};
        bson_document get_more{{"getMore", cursor_id_}, {"collection", collection_},
            {"batchSize", options_.batch_size},
            {"maxTimeMS", static_cast<std::int64_t>(options_.maximum_await_time.count())}};
        auto response = co_await (*connection_)->command(database_, std::move(get_more));
        if (response)
        {
            auto parsed = read_batch("nextBatch", *response);
            if (!parsed)
                co_return std::unexpected(parsed.error());
            continue;
        }
        if (!retry_policy_.should_retry(operation_kind::change_stream_get_more,
                response.error(), attempt))
            co_return std::unexpected(response.error());
        connection_->discard();
        connection_.reset();
        opened_ = false;
        cursor_id_ = 0;
        co_await async_sleep(pool_->context(), retry_policy_.backoff(attempt));
        auto resumed = co_await open_cursor(true);
        if (!resumed)
            co_return std::unexpected(resumed.error());
    }
}

auto change_stream::close() -> task<void>
{
    if (connection_ && cursor_id_ != 0 && connection_->valid())
    {
        bson_array cursors{bson_value{cursor_id_}};
        auto ignored = co_await (*connection_)->command(database_, bson_document{{"killCursors", collection_}, {"cursors", std::move(cursors)}});
        (void)ignored;
    }
    cursor_id_ = 0;
    opened_ = false;
    buffered_events_.clear();
    connection_.reset();
}

auto change_stream::resume_token() const noexcept -> const bson_document*
{
    return resume_token_ ? &*resume_token_ : nullptr;
}

auto change_stream::cursor_id() const noexcept -> std::int64_t
{
    return cursor_id_;
}
} // namespace cnetmod::mongodb
