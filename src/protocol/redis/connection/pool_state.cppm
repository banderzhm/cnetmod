export module cnetmod.protocol.redis:pool_state;

import std;
import :client;
import cnetmod.coro.cancel;
import cnetmod.io.io_context;

export namespace cnetmod::redis {
class connection_pool;
enum class conn_state : std::uint8_t
{
    initial,
    connecting,
    idle,
    in_use,
    pinging,
    dead,
    retired_in_use
};

struct conn_node
{
    std::unique_ptr<client> conn;
    std::atomic<conn_state> state = conn_state::initial;
    std::chrono::steady_clock::time_point last_used;
    std::coroutine_handle<> task_waiting{};
    /**
     * Owns the maintenance-coroutine resume queued when a lease is returned.
     * Keeping the queue node in conn_node makes the noexcept return path
     * allocation-free even when another coroutine holds the pool lock.
     */
    post_node task_completion;
    /**
     * @brief Cancels this node's maintenance wait or heartbeat, never user I/O.
     */
    cancel_token maintenance_cancellation;
    /**
     * Owns a contended lease-return notification until the pool dispatches it.
     */
    post_node return_notification;
    connection_pool* return_owner = nullptr;
    std::size_t index = 0;
    conn_node() = default;
    conn_node(conn_node&&) = delete;
    auto operator=(conn_node&&) -> conn_node& = delete;
    conn_node(const conn_node&) = delete;
    auto operator=(const conn_node&) -> conn_node& = delete;
};

struct pool_waiter
{
    std::coroutine_handle<> handle{};
    /**
     * @brief Owns the single claimed completion post until the waiter resumes.
     */
    post_node completion{};
    conn_node** result_node = nullptr;
    pool_waiter* next = nullptr;
    cancel_token* token = nullptr;
    bool pool_stopped = false;
};
} // namespace cnetmod::redis
