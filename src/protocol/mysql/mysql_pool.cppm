module;

#include <cnetmod/config.hpp>
#include <cstdint>
#if defined(_MSC_VER)
    #include <intrin.h>
#endif

export module cnetmod.protocol.mysql:pool;

import std;
import :types;
import :diagnostics;
import :connection_client;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.coro.mutex;
import cnetmod.coro.cancel;
import cnetmod.coro.wait_group;
import cnetmod.executor.async_op;

namespace cnetmod::mysql {
export struct pool_params
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 3306;
    std::string username;
    std::string password;
    std::string database;
    ssl_mode ssl = ssl_mode::enable;
    std::size_t initial_size = 1;
    std::size_t max_size = 16;
    std::chrono::steady_clock::duration connect_timeout =
        std::chrono::seconds(20);
    std::chrono::steady_clock::duration pool_timeout = std::chrono::seconds(5);
    std::chrono::steady_clock::duration retry_interval = std::chrono::seconds(30);
    std::chrono::steady_clock::duration ping_interval = std::chrono::hours(1);
    std::chrono::steady_clock::duration ping_timeout = std::chrono::seconds(10);
    bool tls_verify = false;
    std::string tls_ca_file;
};

enum class conn_state : std::uint8_t
{
    initial,
    connecting,
    idle,
    in_use,
    returning,
    resetting,
    pinging,
    dead
};

struct conn_node
{
    std::unique_ptr<client> conn;
    std::atomic<conn_state> state = conn_state::initial;
    cancel_token ping_sleep_token{};
    cancel_token network_token{};
    std::atomic<std::coroutine_handle<>> task_waiting{};
    post_node task_completion;
    std::size_t index = 0;
    conn_node() = default;
    conn_node(conn_node&&) = delete;
    conn_node& operator=(conn_node&&) = delete;
    conn_node(const conn_node&) = delete;
    conn_node& operator=(const conn_node&) = delete;
};

struct pool_waiter
{
    post_node completion;
    std::coroutine_handle<> handle{};
    conn_node** result_node = nullptr;
    pool_waiter* next = nullptr;
    cancel_token* token = nullptr;
};

export class connection_pool;

export class pooled_connection
{
public:
    pooled_connection() noexcept = default;
    pooled_connection(pooled_connection&& other) noexcept;
    auto operator=(pooled_connection&& other) noexcept -> pooled_connection&;
    pooled_connection(const pooled_connection&) = delete;
    auto operator=(const pooled_connection&) -> pooled_connection& = delete;
    ~pooled_connection();
    auto valid() const noexcept -> bool;
    auto get() noexcept -> client&;
    auto get() const noexcept -> const client&;
    auto operator->() noexcept -> client*;
    auto operator->() const noexcept -> const client*;
    void return_without_reset();

private:
    friend class connection_pool;
    connection_pool* pool_ = nullptr;
    conn_node* node_ = nullptr;
    pooled_connection(connection_pool* pool, conn_node* node) noexcept;
    void return_to_pool(bool needs_reset);
};

export class connection_pool
{
public:
    connection_pool(io_context& ctx, pool_params params);
    connection_pool(const connection_pool&) = delete;
    auto operator=(const connection_pool&) -> connection_pool& = delete;
    /**
     * @brief Runs maintenance and waits for all connection workers before returning.
     * The pool must outlive this task and every borrowed connection.
     * A concurrent run throws operation_in_progress without stopping the owner.
     */
    auto async_run() -> task<void>;
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
    /**
     * @brief Permanently requests maintenance shutdown without blocking.
     * Safe before async_run starts; pool ownership must outlive the run task.
     */
    void request_stop() noexcept;
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    /**
     * @brief Counts outstanding leases on the owning execution thread.
     * Scans existing node states without adding work to acquisition or return.
     */
    [[nodiscard]] auto checked_out_count() const noexcept -> std::size_t;
    auto waiter_count() const noexcept -> std::size_t;

private:
    friend class pooled_connection;
    io_context& ctx_;
    pool_params params_;
    std::deque<conn_node> conns_;
    async_mutex mtx_;
    bool running_ = false;
    std::atomic<bool> run_active_{false};
    std::atomic<bool> stop_requested_{false};
    cancel_token run_cancel_;
    std::exception_ptr maintenance_failure_;
    async_wait_group connection_workers_;
    pool_waiter* waiters_head_ = nullptr;
    pool_waiter* waiters_tail_ = nullptr;
    std::size_t num_pending_requests_ = 0;
    std::atomic<std::size_t> waiters_count_{0};
    static constexpr std::size_t BITMAP_BITS = 64;
    static constexpr std::size_t MAX_BITMAPS = 8;
    std::atomic<uint64_t> idle_bitmap_[MAX_BITMAPS];
    auto make_connect_options() const -> connect_options;
    auto count_ready_connections() const noexcept -> std::size_t;
    auto connection_task(conn_node& node) -> task<void>;
    /**
     * @brief Runs maintenance inside the primary task's exception boundary.
     */
    auto run_maintenance() -> task<void>;
    /**
     * @brief Registers a worker for joined completion and maintenance failure reporting.
     */
    void start_worker(task<void> work);
    void spawn_connection();
    void set_idle_bit(std::size_t index);
    void clear_idle_bit(std::size_t index);
    auto count_pending_conns() const -> std::size_t;
    void create_connections_if_needed();
    static void dec_if_positive(std::atomic<std::size_t>& counter);
    auto try_get_idle_locked() -> conn_node*;
    void notify_waiters_with_idle_locked();
    auto remove_waiter(pool_waiter* target) -> bool;
    void return_connection(conn_node& node, bool needs_reset);
    /**
     * @brief Publishes a returned lease and wakes its existing maintenance worker.
     * The connection state alone determines whether reset is required.
     */
    [[nodiscard]] auto publish_returned_connection(conn_node& node, conn_state reusable_state) -> bool;
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
    /**
     * @brief Runs all shards until stopped and joins them before returning.
     * Start this lifecycle task concurrently with requests; do not await it as
     * a startup-only barrier. All shard event loops must remain running.
     * Duplicate runs fail before dispatching shards or changing their state.
     */
    auto async_run() -> task<void>;
    /**
     * @brief Requests every shard to stop on its own event loop.
     * Thread-safe. Await the running async_run task to observe completion.
     */
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
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto shard_count() const noexcept -> std::size_t;

private:
    pool_params base_params_;
    std::atomic<bool> run_active_{false};
    std::vector<std::unique_ptr<connection_pool>> shards_;
    std::vector<io_context*> shard_ctxs_;
    std::unordered_map<io_context*, std::size_t> shard_by_ctx_;
    std::atomic<std::size_t> next_shard_{0};
    io_context* fallback_ctx_ = nullptr;
    auto get_shard_index(io_context& io) -> std::size_t;
    auto try_borrow_immediate(std::size_t primary_idx)
        -> std::expected<pooled_connection, std::error_code>;
    auto select_wait_shard(std::size_t preferred_idx) -> std::size_t;
    void init_shards(const std::vector<io_context*>& worker_contexts,
        std::size_t num_shards);
};
} // namespace cnetmod::mysql
