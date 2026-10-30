export module cnetmod.protocol.mongodb:cursor;
import std;
import cnetmod.coro.task;
import :error;
import :bson_document;
import :connection_pool;

export namespace cnetmod::mongodb {

struct cursor_options
{
    std::int32_t batch_size = 100;
    std::optional<std::chrono::milliseconds> maximum_await_time;
};

/// A cursor owns one lease so getMore/killCursors always use its original server.
/// Await close() before destruction when the server cursor is still live.
class cursor
{
public:
    cursor(connection_pool& pool, std::string database, std::string collection,
        cursor_options options = {});
    cursor(const cursor&) = delete;
    auto operator=(const cursor&) -> cursor& = delete;
    cursor(cursor&&) = delete;
    auto operator=(cursor&&) -> cursor& = delete;
    ~cursor();
    auto find(bson_document filter = {}) -> task<result<void>>;
    auto aggregate(std::vector<bson_document> pipeline) -> task<result<void>>;
    /// One getMore at most; an empty live batch is distinguishable via exhausted().
    auto try_next() -> task<result<std::optional<bson_document>>>;
    /// Continues across empty batches until a document, exhaustion, or error.
    auto next() -> task<result<std::optional<bson_document>>>;
    auto close() -> task<result<void>>;
    [[nodiscard]] auto exhausted() const noexcept -> bool;
    [[nodiscard]] auto id() const noexcept -> std::int64_t;

private:
    auto open(bson_document command) -> task<result<void>>;
    auto accept_batch(const bson_document& response, std::string_view field) -> result<void>;
    void retire() noexcept;
    connection_pool* pool_;
    std::string database_;
    std::string collection_;
    cursor_options options_;
    std::optional<pooled_connection> lease_;
    std::deque<bson_document> buffered_;
    std::int64_t id_ = 0;
    bool opened_ = false;
    bool operation_active_ = false;
};
} // namespace cnetmod::mongodb
