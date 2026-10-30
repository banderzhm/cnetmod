module cnetmod.protocol.mongodb;
import std;
import cnetmod.coro.task;
import :error;
import :bson_document;
import :connection;
import :connection_pool;
import :cursor;

namespace cnetmod::mongodb {
namespace {
    struct cursor_operation
    {
        bool& active;

        explicit cursor_operation(bool& value) : active(value)
        {
            active = true;
        }

        ~cursor_operation()
        {
            active = false;
        }
    };
} // namespace

cursor::cursor(connection_pool& pool, std::string database, std::string collection,
    cursor_options options)
    : pool_(&pool), database_(std::move(database)), collection_(std::move(collection)), options_(options) {}

cursor::~cursor()
{
    // Destruction cannot await killCursors. Retire the transport rather than
    // returning a lease with an abandoned live cursor to the pool.
    if (id_ != 0)
        retire();
}

void cursor::retire() noexcept
{
    if (lease_)
        lease_->discard();
    lease_.reset();
    id_ = 0;
    buffered_.clear();
    opened_ = false;
}

auto cursor::accept_batch(const bson_document& response, std::string_view field) -> result<void>
{
    const auto value = response.find("cursor");
    const auto body = value ? value->as_document() : nullptr;
    const auto cursor_id = body ? body->find("id") : nullptr;
    const auto batch = body ? body->find(field) : nullptr;
    if (!cursor_id || !cursor_id->get_if<std::int64_t>() || !batch || !batch->as_array())
        return std::unexpected(make_error(error_code::protocol_error,
            "MongoDB cursor response has an invalid id or batch"));
    if (const auto ns = body->find("ns"); ns && ns->get_if<std::string>())
    {
        const auto prefix = database_ + ".";
        const auto& name = *ns->get_if<std::string>();
        if (!name.starts_with(prefix) || name.size() == prefix.size())
            return std::unexpected(make_error(error_code::protocol_error,
                "MongoDB cursor namespace does not match database"));
        collection_ = name.substr(prefix.size());
    }
    std::deque<bson_document> documents;
    for (const auto& item : *batch->as_array())
    {
        if (!item.as_document())
            return std::unexpected(make_error(error_code::protocol_error,
                "MongoDB cursor batch contains a non-document"));
        documents.push_back(*item.as_document());
    }
    id_ = *cursor_id->get_if<std::int64_t>();
    buffered_ = std::move(documents);
    if (id_ == 0)
        lease_.reset();
    return {};
}

auto cursor::open(bson_document command) -> task<result<void>>
{
    if (operation_active_ || opened_ || options_.batch_size < 0 || database_.empty() || collection_.empty() ||
        (options_.maximum_await_time && options_.maximum_await_time->count() < 0))
        co_return std::unexpected(make_error(error_code::protocol_error,
            "MongoDB cursor is already open or has invalid options"));
    cursor_operation operation{operation_active_};
    auto acquired = co_await pool_->acquire();
    if (!acquired)
        co_return std::unexpected(acquired.error());
    lease_.emplace(std::move(*acquired));
    try
    {
        auto reply = co_await (*lease_)->command(database_, std::move(command));
        if (!reply)
        {
            retire();
            co_return std::unexpected(reply.error());
        }
        auto parsed = accept_batch(*reply, "firstBatch");
        if (!parsed)
        {
            retire();
            co_return parsed;
        }
        opened_ = true;
        co_return result<void>{};
    }
    catch (...)
    {
        retire();
        throw;
    }
}

auto cursor::find(bson_document filter) -> task<result<void>>
{
    co_return co_await open(bson_document{{"find", collection_}, {"filter", std::move(filter)},
        {"batchSize", options_.batch_size}});
}

auto cursor::aggregate(std::vector<bson_document> pipeline) -> task<result<void>>
{
    bson_array stages;
    for (auto& stage : pipeline)
        stages.emplace_back(std::move(stage));
    co_return co_await open(bson_document{{"aggregate", collection_}, {"pipeline", std::move(stages)},
        {"cursor", bson_document{{"batchSize", options_.batch_size}}}});
}

auto cursor::try_next() -> task<result<std::optional<bson_document>>>
{
    if (!opened_ || operation_active_)
        co_return std::unexpected(make_error(error_code::protocol_error, "MongoDB cursor is not open or is busy"));
    cursor_operation operation{operation_active_};
    if (buffered_.empty() && id_ != 0)
    {
        bson_document command{{"getMore", id_}, {"collection", collection_}};
        if (options_.batch_size > 0)
            command.append("batchSize", options_.batch_size);
        if (options_.maximum_await_time)
            command.append("maxTimeMS", static_cast<std::int64_t>(options_.maximum_await_time->count()));
        try
        {
            auto reply = co_await (*lease_)->command(database_, std::move(command));
            if (!reply)
            {
                retire();
                co_return std::unexpected(reply.error());
            }
            auto parsed = accept_batch(*reply, "nextBatch");
            if (!parsed)
            {
                retire();
                co_return std::unexpected(parsed.error());
            }
        }
        catch (...)
        {
            retire();
            throw;
        }
    }
    if (buffered_.empty())
        co_return std::optional<bson_document>{};
    auto document = std::move(buffered_.front());
    buffered_.pop_front();
    co_return std::optional<bson_document>{std::move(document)};
}

auto cursor::next() -> task<result<std::optional<bson_document>>>
{
    for (;;)
    {
        auto document = co_await try_next();
        if (!document || *document || exhausted())
            co_return document;
    }
}

auto cursor::close() -> task<result<void>>
{
    if (operation_active_)
        co_return std::unexpected(make_error(error_code::protocol_error, "MongoDB cursor is busy"));
    cursor_operation operation{operation_active_};
    result<void> result;
    if (id_ != 0 && lease_)
    {
        try
        {
            auto reply = co_await (*lease_)->command(database_, bson_document{{"killCursors", collection_}, {"cursors", bson_array{bson_value{id_}}}});
            if (!reply)
            {
                retire();
                co_return std::unexpected(reply.error());
            }
        }
        catch (...)
        {
            retire();
            throw;
        }
    }
    lease_.reset();
    id_ = 0;
    buffered_.clear();
    opened_ = false;
    co_return result;
}

auto cursor::exhausted() const noexcept -> bool
{
    return opened_ && id_ == 0 && buffered_.empty();
}

auto cursor::id() const noexcept -> std::int64_t
{
    return id_;
}
} // namespace cnetmod::mongodb
