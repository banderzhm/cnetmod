export module cnetmod.protocol.mongodb:client_session;

import std;
import cnetmod.coro.task;
import :error;
import :bson_document;
import :connection_pool;

export namespace cnetmod::mongodb {

enum class transaction_state
{
    none,
    starting,
    in_progress,
    committed,
    aborted
};

struct transaction_options
{
    std::optional<std::string> read_concern_level;
    std::optional<std::string> write_concern = std::string{"majority"};
    std::optional<std::chrono::milliseconds> maximum_commit_time;
    std::size_t maximum_commit_attempts = 2;
    std::chrono::milliseconds commit_retry_backoff{10};
};

class client_session
{
public:
    client_session();
    client_session(const client_session&) = delete;
    auto operator=(const client_session&) -> client_session& = delete;
    client_session(client_session&&) noexcept;
    auto operator=(client_session&&) noexcept -> client_session&;
    ~client_session();

    auto start_transaction(transaction_options options = {}) -> result<void>;
    auto command(connection_pool& pool, std::string_view database,
        bson_document command_document) -> task<result<bson_document>>;
    auto commit_transaction(connection_pool& pool) -> task<result<void>>;
    auto abort_transaction(connection_pool& pool) -> task<result<void>>;
    void reset() noexcept;

    [[nodiscard]] auto id() const noexcept -> const bson_binary&;
    [[nodiscard]] auto state() const noexcept -> transaction_state;
    [[nodiscard]] auto transaction_number() const noexcept -> std::int64_t;
    [[nodiscard]] auto has_pinned_connection() const noexcept -> bool;

private:
    auto connection_for(connection_pool& pool) -> task<result<connection*>>;
    void decorate(bson_document& command_document);
    bson_binary id_;
    std::int64_t transaction_number_ = 0;
    transaction_state state_ = transaction_state::none;
    transaction_options transaction_options_;
    std::optional<pooled_connection> pinned_connection_;
};

} // namespace cnetmod::mongodb
