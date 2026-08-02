export module cnetmod.protocol.mongodb:connection_pool;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
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
    auto acquire() -> task<result<pooled_connection>>;
    auto acquire(std::stop_token cancellation) -> task<result<pooled_connection>>;
    auto health_check() -> task<void>;
    auto run_maintenance(std::stop_token stop) -> task<void>;
    void close() noexcept;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto idle_count() const noexcept -> std::size_t;
    [[nodiscard]] auto checked_out_count() const noexcept -> std::size_t;
    [[nodiscard]] auto waiter_count() const noexcept -> std::size_t;
    [[nodiscard]] auto context() noexcept -> io_context&;

private:
    auto create_connection() -> task<result<std::shared_ptr<connection_pool_slot>>>;
    std::shared_ptr<connection_pool_state> state_;
};

} // namespace cnetmod::mongodb
