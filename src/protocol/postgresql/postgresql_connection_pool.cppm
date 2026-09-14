export module cnetmod.protocol.postgresql:connection_pool;

import std;
import :connection;
import :connection_options;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.mutex;
import cnetmod.coro.task_group;

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
    /**
     * @brief Acquires the minimum pool capacity with cancellable connections.
     * On failure, connections leased by this attempt are closed before return,
     * including reused idle connections. Other borrowers are not affected.
     * Discarded slots remain retryable; rollback does not close the pool.
     */
    auto warm_up(cancel_token& cancellation) -> task<std::expected<void, std::error_code>>;
    auto acquire() -> task<std::expected<pooled_connection, std::error_code>>;
    auto acquire(cancel_token& cancellation)
        -> task<std::expected<pooled_connection, std::error_code>>;
    /**
     * @brief Serializes shutdown callers until transport cleanup completes.
     * All close tasks must remain alive and be awaited to completion.
     */
    auto close() -> task<void>;
    /**
     * @brief Stops accepting leases and closes with cancellable settlement.
     * A failed close retains slot and callback ownership. Keep the pool alive
     * and retry close after outstanding borrowers return their leases.
     */
    auto close(cancel_token& cancellation) -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto idle_count() const noexcept -> std::size_t;
    [[nodiscard]] auto checked_out_count() const noexcept -> std::size_t;
    [[nodiscard]] auto waiter_count() const noexcept -> std::size_t;
    /**
     * @brief Returns a reconnect dispatch or completed-group failure without allocation.
     * Call on the owning executor, serialized with pool operations. A clear
     * result does not imply connectivity or completion of running children.
     * Dispatch failures take precedence and remain recorded until cancellable
     * warmup can retire the completed group and retry discarded slots.
     */
    [[nodiscard]] auto background_error() const noexcept -> std::error_code;

private:
    friend class pooled_connection;

    struct slot
    {
        std::unique_ptr<client> connection;
        bool in_use{};
        bool discard{};
        bool connecting{};
        post_node return_notification{};
        connection_pool* owner{};
        std::size_t index{};
        bool return_discard{};
    };

    struct waiter
    {
        std::coroutine_handle<> handle{};
        pooled_connection* result{};
        cancel_token* cancellation{};
        bool queued{};
        bool registered{};
        connection_pool* owner{};
        post_node cancellation_notification{};
    };

    io_context& context_;
    connection_pool_options options_;
    // The pool state is a coroutine-only critical section.  A lease never
    // holds this lock while the caller performs database I/O: it protects only
    // slot assignment, FIFO waiter ownership, and shutdown transitions.
    async_mutex state_mutex_;
    async_mutex close_mutex_;
    std::deque<slot> slots_;
    std::deque<waiter*> waiters_;
    bool closing_{};
    std::size_t active_waiters_{};
    std::unique_ptr<task_group> reconnect_tasks_;
    std::error_code reconnect_dispatch_error_;
    std::atomic<std::size_t> size_snapshot_{};
    std::atomic<std::size_t> idle_snapshot_{};
    std::atomic<std::size_t> checked_out_snapshot_{};
    std::atomic<std::size_t> waiter_snapshot_{};
    void refresh_snapshots_locked() noexcept;
    template <bool Cancellable>
    auto close_impl(cancel_token* cancellation)
        -> task<std::conditional_t<Cancellable, std::expected<void, std::error_code>, void>>;
    void release(std::size_t, bool) noexcept;
    void release_locked(std::size_t, bool) noexcept;
    static void dispatch_return(void*) noexcept;
    void remove_waiter(waiter*) noexcept;
    void post_waiter(waiter&) noexcept;
    static void dispatch_cancellation(void*) noexcept;
    auto reconnect_discarded_slot(std::size_t, cancel_token&)
        -> task<std::expected<void, std::error_code>>;
};

} // namespace cnetmod::postgresql
