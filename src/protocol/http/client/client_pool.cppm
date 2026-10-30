export module cnetmod.protocol.http.client.pool;

import std;

import cnetmod.protocol.http;
import cnetmod.coro.cancel;
import cnetmod.coro.mutex;
import cnetmod.coro.task;
import cnetmod.io.io_context;

export namespace cnetmod::http {
/// Per-endpoint HTTP client reuse pool.  Connections remain owned by clients;
/// HTTP/2 clients multiplex streams while HTTP/1 clients are leased
/// exclusively.
struct client_pool_key
{
    std::string host;
    std::uint16_t port{};
    bool tls{};

    [[nodiscard]] auto operator==(const client_pool_key&) const noexcept
        -> bool = default;
    [[nodiscard]] auto
    operator<=>(const client_pool_key&) const noexcept = default;
};

class client_pool
{
public:
    client_pool(cnetmod::io_context& context, client_options options = {},
        std::size_t max_idle = 64);

    [[nodiscard]] auto acquire() -> std::unique_ptr<client>;

    void release(std::unique_ptr<client> value);

    [[nodiscard]] auto acquire(client_pool_key key) -> std::unique_ptr<client>;

    void release(client_pool_key key, std::unique_ptr<client> value);

    void clear() noexcept;

    [[nodiscard]] auto idle_count() const noexcept -> std::size_t;

private:
    cnetmod::io_context* context_{};
    client_options options_;
    std::size_t max_idle_{};
    std::vector<std::unique_ptr<client>> idle_;
    std::map<client_pool_key, std::vector<std::unique_ptr<client>>>
        endpoint_idle_;
};

/// Capacity and wait policy for a bounded outbound HTTP client pool.
struct bounded_client_pool_options
{
    client_options client;
    std::size_t maximum_clients = 256;
    std::size_t maximum_waiters = 1024;
    std::chrono::milliseconds acquire_timeout{5000};
};

class bounded_client_pool;

/// Exclusive RAII lease for one outbound HTTP client.
class pooled_client
{
public:
    pooled_client() noexcept = default;
    pooled_client(pooled_client&&) noexcept;
    auto operator=(pooled_client&&) noexcept -> pooled_client&;
    pooled_client(const pooled_client&) = delete;
    auto operator=(const pooled_client&) -> pooled_client& = delete;
    ~pooled_client();

    [[nodiscard]] auto valid() const noexcept -> bool;
    [[nodiscard]] auto operator->() noexcept -> client*;
    [[nodiscard]] auto get() noexcept -> client&;

    /// Close the current transport before returning this slot to the pool.
    void discard() noexcept;

private:
    friend class bounded_client_pool;
    pooled_client(bounded_client_pool* owner, std::size_t slot,
        client* value) noexcept;

    bounded_client_pool* owner_{};
    std::size_t slot_{};
    client* value_{};
};

/// Per-event-loop client pool with bounded concurrency and FIFO backpressure.
///
/// One instance belongs to one io_context. A gateway creates one instance per
/// worker and per upstream. Call close() and await it before destroying the
/// pool; close waits for outstanding leases and cancellation callbacks.
class bounded_client_pool
{
public:
    bounded_client_pool(cnetmod::io_context& context,
        bounded_client_pool_options options = {});
    bounded_client_pool(const bounded_client_pool&) = delete;
    auto operator=(const bounded_client_pool&)
        -> bounded_client_pool& = delete;

    [[nodiscard]] auto acquire()
        -> task<std::expected<pooled_client, std::error_code>>;
    [[nodiscard]] auto acquire(cancel_token& cancellation)
        -> task<std::expected<pooled_client, std::error_code>>;

    /// Stop new acquisition, wake queued callers, and wait for leases to return.
    [[nodiscard]] auto close() -> task<void>;

    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto idle_count() const noexcept -> std::size_t;
    [[nodiscard]] auto checked_out_count() const noexcept -> std::size_t;
    [[nodiscard]] auto waiter_count() const noexcept -> std::size_t;

private:
    friend class pooled_client;

    struct slot
    {
        std::unique_ptr<client> value;
        bool checked_out{};
        post_node return_notification{};
        bounded_client_pool* owner{};
        std::size_t index{};
        bool return_discard{};
    };

    struct waiter
    {
        std::coroutine_handle<> handle{};
        pooled_client* result{};
        cancel_token* cancellation{};
        bool queued{};
        bool registered{};
        bounded_client_pool* owner{};
        post_node cancellation_notification{};
    };

    cnetmod::io_context& context_;
    bounded_client_pool_options options_;
    async_mutex state_mutex_;
    async_mutex close_mutex_;
    std::deque<slot> slots_;
    std::deque<waiter*> waiters_;
    bool closing_{};
    std::size_t active_waiters_{};
    std::size_t idle_{};
    std::size_t checked_out_{};
    std::atomic<std::size_t> size_snapshot_{};
    std::atomic<std::size_t> idle_snapshot_{};
    std::atomic<std::size_t> checked_out_snapshot_{};
    std::atomic<std::size_t> waiter_snapshot_{};

    void refresh_snapshots_locked() noexcept;
    void release(std::size_t slot, bool discard) noexcept;
    void release_locked(std::size_t slot, bool discard) noexcept;
    static void dispatch_return(void* argument) noexcept;
    void remove_waiter(waiter* target) noexcept;
    void post_waiter(waiter& pending) noexcept;
    static void dispatch_cancellation(void* argument) noexcept;
};
} // namespace cnetmod::http
