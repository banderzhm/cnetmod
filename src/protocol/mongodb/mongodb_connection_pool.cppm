export module cnetmod.protocol.mongodb:connection_pool;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import cnetmod.coro.cancel;
import :error;
import :connection;
import :connection_options;

export namespace cnetmod::mongodb {

struct connection_pool_options
{
    connection_options connection;
    std::size_t minimum_size = 0;
    std::size_t maximum_size = 32;
    std::size_t maximum_connecting = 2;
    std::chrono::milliseconds wait_queue_timeout{10000};
    std::chrono::milliseconds maximum_idle_time{60000};
    std::chrono::milliseconds health_check_interval{30000};
};

class connection_pool_state;
class connection_pool_slot;

class pooled_connection
{
public:
    pooled_connection() noexcept = default;
    pooled_connection(pooled_connection&& other) noexcept;
    auto operator=(pooled_connection&& other) noexcept -> pooled_connection&;
    pooled_connection(const pooled_connection&) = delete;
    auto operator=(const pooled_connection&) -> pooled_connection& = delete;
    ~pooled_connection();
    [[nodiscard]] auto valid() const noexcept -> bool;
    auto get() noexcept -> connection&;
    auto operator->() noexcept -> connection*;
    void discard() noexcept;

private:
    friend class connection_pool;
    std::shared_ptr<connection_pool_state> state_;
    std::shared_ptr<connection_pool_slot> slot_;
    pooled_connection(std::shared_ptr<connection_pool_state> state,
        std::shared_ptr<connection_pool_slot> slot) noexcept;
    void release() noexcept;
};

class connection_pool
{
public:
    connection_pool(io_context& context, connection_pool_options options);
    ~connection_pool();
    connection_pool(const connection_pool&) = delete;
    auto operator=(const connection_pool&) -> connection_pool& = delete;

    auto warm_up() -> task<result<void>>;
    /**
     * Warms the pool while cancelling only connections created by this call.
     * The exclusive token and pool must outlive the operation. Cancellation
     * does not cancel other callers waiting for or creating connections.
     */
    auto warm_up(cancel_token& cancellation) -> task<result<void>>;
    auto acquire() -> task<result<pooled_connection>>;
    auto acquire(std::stop_token cancellation) -> task<result<pooled_connection>>;
    auto health_check() -> task<void>;
    /**
     * Checks idle connections with cancellable pings. If none are idle, uses
     * the existing checkout queue and creation limits to obtain a candidate.
     * Queue timeout is not reported as verified health. The pool and
     * exclusive token must outlive the returned operation.
     */
    auto health_check(cancel_token& cancellation) -> task<result<void>>;
    auto run_maintenance(std::stop_token stop) -> task<void>;
    void close() noexcept;
    /**
     * Waits for pool closure and already-registered deferred lease returns.
     * The returned task retains shared state, not the connection_pool object.
     * This does not join independently started maintenance or borrower tasks.
     */
    [[nodiscard]] auto async_close() -> task<void>;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto idle_count() const noexcept -> std::size_t;
    [[nodiscard]] auto checked_out_count() const noexcept -> std::size_t;
    /**
     * Returns the number of admitted connection attempts awaiting settlement.
     * Pool closure rejects new attempts but does not complete existing ones.
     */
    [[nodiscard]] auto connecting_count() const noexcept -> std::size_t;
    [[nodiscard]] auto waiter_count() const noexcept -> std::size_t;
    [[nodiscard]] auto context() noexcept -> io_context&;

private:
    auto create_connection(cancel_token* cancellation = nullptr, std::stop_token stop = {}) -> task<result<std::shared_ptr<connection_pool_slot>>>;
    auto checkout_for_health(cancel_token& cancellation) -> task<result<pooled_connection>>;
    template <bool Cancellable>
    auto warm_connections(cancel_token* cancellation) -> task<result<void>>;
    template <bool Cancellable>
    auto check_connections(cancel_token* cancellation)
        -> task<std::conditional_t<Cancellable, result<void>, void>>;
    std::shared_ptr<connection_pool_state> state_;
};

} // namespace cnetmod::mongodb
