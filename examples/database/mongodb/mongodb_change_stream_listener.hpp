#pragma once

namespace mongodb_example {

class change_stream_listener
{
public:
    change_stream_listener(cnetmod::io_context& context,
        cnetmod::mongodb::topology_connection_pool& pool,
        std::string database, std::string collection,
        std::chrono::milliseconds maximum_await)
        : context_(context), pool_(pool), database_(std::move(database)), collection_(std::move(collection)), maximum_await_(maximum_await) {}

    auto run(std::stop_token stop) -> cnetmod::task<void>
    {
        while (!stop.stop_requested())
        {
            auto opened = co_await open_cursor();
            if (!opened)
            {
                logger::warn("MongoDB change stream open failed: error={}", opened.error().message);
                co_await cnetmod::async_sleep(context_, std::chrono::milliseconds{250});
                continue;
            }
            while (!stop.stop_requested() && cursor_id_ != 0)
            {
                auto response = co_await pool_.command(database_, cnetmod::mongodb::bson_document{{"getMore", cursor_id_}, {"collection", collection_}, {"batchSize", std::int32_t{100}}, {"maxTimeMS", static_cast<std::int64_t>(maximum_await_.count())}});
                if (!response)
                {
                    logger::warn("MongoDB change stream interrupted; resuming: error={}", response.error().message);
                    cursor_id_ = 0;
                    resumed_.fetch_add(1, std::memory_order_relaxed);
                    auto refreshed = co_await pool_.refresh();
                    (void)refreshed;
                    break;
                }
                auto parsed = read_batch("nextBatch", *response);
                if (!parsed)
                {
                    logger::warn("MongoDB change stream response rejected: error={}", parsed.error().message);
                    cursor_id_ = 0;
                    break;
                }
            }
        }
        co_await close_cursor();
    }

    [[nodiscard]] auto event_count() const noexcept -> std::size_t
    {
        return events_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] auto resume_count() const noexcept -> std::size_t
    {
        return resumed_.load(std::memory_order_relaxed);
    }

    auto interrupt_cursor_for_verification() -> cnetmod::task<bool>
    {
        if (cursor_id_ == 0)
            co_return false;
        auto killed = co_await pool_.command(database_, cnetmod::mongodb::bson_document{{"killCursors", collection_}, {"cursors", cnetmod::mongodb::bson_array{cursor_id_}}});
        co_return killed.has_value();
    }

private:
    auto open_cursor() -> cnetmod::task<cnetmod::mongodb::result<void>>
    {
        namespace mongo = cnetmod::mongodb;
        mongo::bson_document stream_stage;
        stream_stage.append("fullDocument", "updateLookup");
        if (resume_token_)
            stream_stage.append("resumeAfter", *resume_token_);
        mongo::bson_array pipeline{mongo::bson_document{{"$changeStream", std::move(stream_stage)}}};
        auto response = co_await pool_.command(database_, mongo::bson_document{{"aggregate", collection_}, {"pipeline", std::move(pipeline)}, {"cursor", mongo::bson_document{{"batchSize", std::int32_t{100}}}}});
        if (!response)
            co_return std::unexpected(response.error());
        co_return read_batch("firstBatch", *response);
    }

    auto read_batch(std::string_view batch_name,
        const cnetmod::mongodb::bson_document& response)
        -> cnetmod::mongodb::result<void>
    {
        namespace mongo = cnetmod::mongodb;
        const auto* cursor_value = response.find("cursor");
        const auto* cursor = cursor_value ? cursor_value->as_document() : nullptr;
        if (!cursor)
            return std::unexpected(mongo::make_error(
                mongo::error_code::protocol_error, "change stream response has no cursor"));
        const auto* id = cursor->find("id");
        if (id && id->get_if<std::int64_t>())
            cursor_id_ = *id->get_if<std::int64_t>();
        else if (id && id->get_if<std::int32_t>())
            cursor_id_ = *id->get_if<std::int32_t>();
        else
            return std::unexpected(mongo::make_error(
                mongo::error_code::protocol_error, "change stream cursor has no id"));
        if (const auto* token = cursor->find("postBatchResumeToken");
            token && token->as_document())
            resume_token_ = *token->as_document();
        if (const auto* batch = cursor->find(batch_name); batch && batch->as_array())
            for (const auto& value : *batch->as_array())
            {
                const auto* event = value.as_document();
                if (!event)
                    continue;
                if (const auto* token = event->find("_id"); token && token->as_document())
                    resume_token_ = *token->as_document();
                events_.fetch_add(1, std::memory_order_relaxed);
            }
        return {};
    }

    auto close_cursor() -> cnetmod::task<void>
    {
        if (cursor_id_ == 0)
            co_return;
        auto ignored = co_await pool_.command(database_, cnetmod::mongodb::bson_document{{"killCursors", collection_}, {"cursors", cnetmod::mongodb::bson_array{cursor_id_}}});
        (void)ignored;
        cursor_id_ = 0;
    }

    cnetmod::io_context& context_;
    cnetmod::mongodb::topology_connection_pool& pool_;
    std::string database_;
    std::string collection_;
    std::chrono::milliseconds maximum_await_;
    std::int64_t cursor_id_ = 0;
    std::optional<cnetmod::mongodb::bson_document> resume_token_;
    std::atomic_size_t events_{0};
    std::atomic_size_t resumed_{0};
};

} // namespace mongodb_example
