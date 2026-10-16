export module cnetmod.protocol.redis:pool;

import std;
import :value;
import :client;
import :pool_options;
import :pool_state;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.coro.mutex;
import cnetmod.coro.wait_group;

namespace cnetmod::redis {

export class connection_pool;
export class sharded_connection_pool;

export class pooled_connection
{
public:
    pooled_connection() noexcept;
    pooled_connection(pooled_connection&& other) noexcept;
    auto operator=(pooled_connection&& other) noexcept -> pooled_connection&;
    pooled_connection(const pooled_connection&) = delete;
    auto operator=(const pooled_connection&) -> pooled_connection& = delete;
    ~pooled_connection();

    [[nodiscard]] auto valid() const noexcept -> bool;
    auto get() noexcept -> client&;
    auto get() const noexcept -> const client&;
    auto operator->() noexcept -> client*;
    auto operator->() const noexcept -> const client*;

private:
    friend class connection_pool;
    friend class sharded_connection_pool;
    connection_pool* pool_ = nullptr;
    conn_node* node_ = nullptr;

    pooled_connection(connection_pool* pool, conn_node* node) noexcept;
    void return_to_pool();
};

export class connection_pool
{
public:
    connection_pool(io_context& ctx, pool_params params);
    connection_pool(const connection_pool&) = delete;
    auto operator=(const connection_pool&) -> connection_pool& = delete;

    auto async_run() -> task<void>;
    /**
     * @brief Permanently requests shutdown without waiting for owned maintenance.
     *
     * May be called from another thread, including before async_run starts.
     * Await async_run or cancel before destroying the pool.
     */
    void request_stop() noexcept;
    auto async_get_connection(cancel_token& token)
        -> task<std::expected<pooled_connection, std::error_code>>;
    /// Acquires a connection using the remaining request budget.
    auto async_get_connection(cnetmod::deadline value)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection()
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto try_get_connection()
        -> std::expected<pooled_connection, std::error_code>;
    auto cancel() -> task<void>;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto idle_count() const noexcept -> std::size_t;
    /**
     * Counts outstanding leases, including leases retained after cancellation.
     * Query on the owning execution thread; acquisition needs no extra counter.
     */
    [[nodiscard]] auto checked_out_count() const noexcept -> std::size_t;
    [[nodiscard]] auto waiter_count() const noexcept -> std::size_t;
    /**
     * @brief Returns maintenance operations whose completion is still owned by the pool.
     */
    [[nodiscard]] auto pending_maintenance() const noexcept -> int;
    [[nodiscard]] auto execution_context() noexcept -> io_context&;

private:
    friend class pooled_connection;
    auto async_get_connection_impl(cancel_token& token)
        -> task<std::expected<pooled_connection, std::error_code>>;
    io_context& ctx_;
    pool_params params_;
    std::deque<conn_node> conns_;
    async_mutex mtx_;
    bool running_ = false;
    bool stopped_ = false;
    cancel_token run_cancellation_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> run_active_{false};
    async_wait_group maintenance_;
    std::exception_ptr maintenance_failure_;
    pool_waiter* waiters_head_ = nullptr;
    pool_waiter* waiters_tail_ = nullptr;
    std::size_t num_pending_requests_ = 0;
    std::atomic<std::size_t> waiters_count_{0};

    static constexpr std::size_t bitmap_bits = 64;
    static constexpr std::size_t max_bitmaps = 8;
    std::atomic<std::uint64_t> idle_bitmap_[max_bitmaps];

    [[nodiscard]] auto make_connect_options() const -> connect_options;
    [[nodiscard]] auto count_ready_connections() const noexcept -> std::size_t;
    auto connection_task(conn_node& node) -> task<void>;
    void spawn_connection();
    void set_idle_bit(std::size_t index);
    void clear_idle_bit(std::size_t index);
    [[nodiscard]] auto count_pending_conns() const -> std::size_t;
    void create_connections_if_needed();
    static void dec_if_positive(std::atomic<std::size_t>& counter);
    auto try_get_idle_locked() -> conn_node*;
    void notify_waiters_with_idle_locked();
    auto remove_waiter(pool_waiter* target) -> bool;
    void return_connection(conn_node& node);
    /**
     * Completes a queued return without allocating a detached coroutine.
     */
    static void dispatch_return(void* argument) noexcept;
    /**
     * @brief Releases exclusive ownership without publishing a closed client.
     */
    auto release_connection(conn_node& node) -> bool;
};

export class sharded_connection_pool
{
public:
    sharded_connection_pool(io_context& ctx, pool_params params,
        std::size_t num_shards = 4);
    sharded_connection_pool(std::vector<io_context*> worker_contexts,
        pool_params params);
    sharded_connection_pool(std::vector<io_context*> worker_contexts,
        pool_params params, std::size_t num_shards);
    sharded_connection_pool(const sharded_connection_pool&) = delete;
    auto operator=(const sharded_connection_pool&)
        -> sharded_connection_pool& = delete;

    auto async_run() -> task<void>;
    /** Thread-safe stop request for every loop-local shard. */
    void request_stop() noexcept;
    auto async_get_connection()
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(cancel_token& token)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(cnetmod::deadline value)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(io_context& io)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(io_context& io, cancel_token& token)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto cancel() -> task<void>;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto idle_count() const noexcept -> std::size_t;
    [[nodiscard]] auto checked_out_count() const noexcept -> std::size_t;
    [[nodiscard]] auto shard_count() const noexcept -> std::size_t;

private:
    pool_params base_params_;
    std::atomic<bool> run_active_{false};
    std::vector<std::unique_ptr<connection_pool>> shards_;
    std::vector<io_context*> shard_ctxs_;
    std::unordered_map<io_context*, std::size_t> shard_by_ctx_;
    std::atomic<std::size_t> next_shard_{0};
    io_context* fallback_ctx_ = nullptr;

    auto try_borrow_immediate(std::size_t primary_index)
        -> std::expected<pooled_connection, std::error_code>;
    auto select_wait_shard(std::size_t preferred_index) -> std::size_t;
    void init_shards(const std::vector<io_context*>& worker_contexts,
        std::size_t num_shards);
};

} // namespace cnetmod::redis
