export module cnetmod.protocol.postgresql:connection_pool;

import std;
import :connection;
import :connection_options;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;

export namespace cnetmod::postgresql {

struct connection_pool_options
{
    connection_options connection;
    std::size_t minimum_connections = 1;
    std::size_t maximum_connections = 16;
    std::chrono::milliseconds acquire_timeout{5000};
};

class connection_pool;

class pooled_connection
{
public:
    pooled_connection() noexcept = default;
    pooled_connection(pooled_connection&&) noexcept;
    auto operator=(pooled_connection&&) noexcept -> pooled_connection&;
    pooled_connection(const pooled_connection&) = delete;
    auto operator=(const pooled_connection&) -> pooled_connection& = delete;
    ~pooled_connection();
    [[nodiscard]] auto valid() const noexcept -> bool;
    auto operator->() noexcept -> client*;
    auto get() noexcept -> client&;
    void discard() noexcept;

private:
    friend class connection_pool;
    pooled_connection(connection_pool*, std::size_t, client*) noexcept;
    connection_pool* owner_{};
    std::size_t slot_{};
    client* connection_{};
};

class connection_pool
{
public:
    connection_pool(io_context&, connection_pool_options);
    connection_pool(const connection_pool&) = delete;
    auto operator=(const connection_pool&) -> connection_pool& = delete;
    auto warm_up() -> task<result_set>;
    auto acquire() -> task<std::expected<pooled_connection, std::error_code>>;
    auto acquire(cancel_token& cancellation)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto close() -> task<void>;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto idle_count() const noexcept -> std::size_t;
    [[nodiscard]] auto checked_out_count() const noexcept -> std::size_t;
    [[nodiscard]] auto waiter_count() const noexcept -> std::size_t;

private:
    friend class pooled_connection;

    struct slot
    {
        std::unique_ptr<client> connection;
        bool in_use{};
        bool discard{};
        bool connecting{};
    };

    struct waiter
    {
        std::coroutine_handle<> handle{};
        pooled_connection* result{};
        cancel_token* cancellation{};
        bool queued{};
    };

    io_context& context_;
    connection_pool_options options_;
    mutable std::mutex mutex_;
    std::deque<slot> slots_;
    std::deque<waiter*> waiters_;
    bool closing_{};
    void release(std::size_t, bool) noexcept;
    void remove_waiter(waiter*) noexcept;
    auto reconnect_discarded_slot(std::size_t) -> task<void>;
};

} // namespace cnetmod::postgresql
