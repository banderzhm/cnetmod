/// cnetmod.protocol.modbus:pool — Modbus Connection Pool
/// High-performance connection pool for Modbus TCP clients

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.modbus:pool;

import std;
import :types;
import :tcp_client;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.coro.mutex;
import cnetmod.coro.cancel;

namespace cnetmod::modbus {

// =============================================================================
// pool_params
// =============================================================================

export struct pool_params {
    std::string host = "127.0.0.1";
    std::uint16_t port = 502;

    std::size_t initial_size = 1;
    std::size_t max_size = 16;

    std::chrono::steady_clock::duration connect_timeout = std::chrono::seconds(10);
    std::chrono::steady_clock::duration pool_timeout = std::chrono::seconds(5);
    std::chrono::steady_clock::duration retry_interval = std::chrono::seconds(30);
    std::chrono::steady_clock::duration health_check_interval = std::chrono::minutes(5);
};

// =============================================================================
// Connection node
// =============================================================================

enum class conn_state : std::uint8_t {
    initial,
    connecting,
    idle,
    in_use,
    dead,
};

struct conn_node {
    std::unique_ptr<tcp_client> conn;
    std::atomic<conn_state> state = conn_state::initial;
    std::chrono::steady_clock::time_point last_used;
    std::coroutine_handle<> task_waiting{};
    std::size_t index = 0;

    conn_node() = default;
    conn_node(conn_node&&) = delete;
    conn_node& operator=(conn_node&&) = delete;
    conn_node(const conn_node&) = delete;
    conn_node& operator=(const conn_node&) = delete;
};

struct pool_waiter {
    std::coroutine_handle<> handle{};
    conn_node** result_node = nullptr;
    pool_waiter* next = nullptr;
    cancel_token* token = nullptr;
};

// Forward declare
export class connection_pool;

// =============================================================================
// pooled_connection
// =============================================================================

export class pooled_connection {
public:
    pooled_connection() noexcept = default;

    pooled_connection(pooled_connection&& o) noexcept
        : pool_(std::exchange(o.pool_, nullptr))
        , node_(std::exchange(o.node_, nullptr))
    {}

    auto operator=(pooled_connection&& o) noexcept -> pooled_connection& {
        if (this != &o) {
            return_to_pool();
            pool_ = std::exchange(o.pool_, nullptr);
            node_ = std::exchange(o.node_, nullptr);
        }
        return *this;
    }

    pooled_connection(const pooled_connection&) = delete;
    auto operator=(const pooled_connection&) -> pooled_connection& = delete;

    ~pooled_connection() { return_to_pool(); }

    auto valid() const noexcept -> bool { return node_ != nullptr; }
    auto get() noexcept -> tcp_client& { return *node_->conn; }
    auto get() const noexcept -> const tcp_client& { return *node_->conn; }
    auto operator->() noexcept -> tcp_client* { return node_->conn.get(); }
    auto operator->() const noexcept -> const tcp_client* { return node_->conn.get(); }

private:
    friend class connection_pool;

    connection_pool* pool_ = nullptr;
    conn_node* node_ = nullptr;

    pooled_connection(connection_pool* pool, conn_node* node) noexcept
        : pool_(pool), node_(node) {}

    void return_to_pool();
};

// =============================================================================
// connection_pool
// =============================================================================

export class connection_pool {
public:
    connection_pool(io_context& ctx, pool_params params)
        : ctx_(ctx), params_(std::move(params)) {}

    connection_pool(const connection_pool&) = delete;
    auto operator=(const connection_pool&) -> connection_pool& = delete;

    auto async_run() -> task<void> {
        running_ = true;
        
        for (std::size_t i = 0; i < params_.initial_size && i < params_.max_size; ++i) {
            spawn_connection();
        }

        auto target = std::min(params_.initial_size, params_.max_size);
        if (target > 0) {
            auto deadline = std::chrono::steady_clock::now() + params_.connect_timeout;
            while (running_ && std::chrono::steady_clock::now() < deadline) {
                if (count_ready_connections() >= target) break;
                co_await async_sleep(ctx_, std::chrono::milliseconds(10));
            }
        }

        while (running_) {
            co_await async_sleep(ctx_, params_.health_check_interval);
        }
    }

    auto async_get_connection(cancel_token& token)
        -> task<std::expected<pooled_connection, std::error_code>>;

    auto async_get_connection()
        -> task<std::expected<pooled_connection, std::error_code>>
    {
        cancel_token token;
        co_return co_await with_timeout(ctx_, params_.pool_timeout,
            async_get_connection(token), token);
    }

    auto try_get_connection()
        -> std::expected<pooled_connection, std::error_code>
    {
        if (waiters_count_.load(std::memory_order_acquire) == 0) {
            if (auto* node = try_get_idle_lockfree()) {
                return pooled_connection(this, node);
            }
        }

        if (!mtx_.try_lock()) {
            return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
        }
        async_lock_guard guard(mtx_, std::adopt_lock);

        if (waiters_head_) {
            notify_waiters_with_idle_locked();
            return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
        }

        if (auto* node = try_get_idle_locked()) {
            return pooled_connection(this, node);
        }

        if (running_ && conns_.size() < params_.max_size) {
            spawn_connection();
        }

        return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
    }

    auto cancel() -> task<void> {
        running_ = false;
        co_await mtx_.lock();
        for (auto& node : conns_) {
            if (node.conn && node.conn->is_open())
                node.state.store(conn_state::dead, std::memory_order_release);
        }
        mtx_.unlock();
    }

    auto size() const noexcept -> std::size_t { return conns_.size(); }

    auto idle_count() const noexcept -> std::size_t {
        std::size_t n = 0;
        for (auto& nd : conns_) {
            if (nd.state.load(std::memory_order_acquire) == conn_state::idle)
                ++n;
        }
        return n;
    }

    auto waiter_count() const noexcept -> std::size_t {
        return waiters_count_.load(std::memory_order_acquire);
    }

private:
    friend class pooled_connection;

    io_context& ctx_;
    pool_params params_;
    std::deque<conn_node> conns_;
    async_mutex mtx_;
    bool running_ = false;
    pool_waiter* waiters_head_ = nullptr;
    pool_waiter* waiters_tail_ = nullptr;
    std::size_t num_pending_requests_ = 0;
    std::atomic<std::size_t> waiters_count_{0};

    auto count_ready_connections() const noexcept -> std::size_t {
        std::size_t ready = 0;
        for (auto& node : conns_) {
            auto st = node.state.load(std::memory_order_acquire);
            if (st == conn_state::idle || st == conn_state::in_use) {
                ++ready;
            }
        }
        return ready;
    }

    auto connection_task(conn_node& node) -> task<void> {
        auto max_retry = std::chrono::duration_cast<std::chrono::milliseconds>(params_.retry_interval);
        if (max_retry <= std::chrono::milliseconds::zero()) {
            max_retry = std::chrono::milliseconds(1);
        }
        auto retry_backoff = std::min(max_retry, std::chrono::milliseconds(100));

        while (running_) {
            auto state = node.state.load(std::memory_order_acquire);
            if (state == conn_state::initial || state == conn_state::dead) {
                node.state.store(conn_state::connecting, std::memory_order_release);
                auto rs = co_await node.conn->connect(params_.host, params_.port);

                co_await mtx_.lock();
                if (rs) {
                    node.state.store(conn_state::dead, std::memory_order_release);
                    mtx_.unlock();
                    co_await async_sleep(ctx_, retry_backoff);
                    retry_backoff = std::min(max_retry, retry_backoff * 2);
                    continue;
                }
                retry_backoff = std::min(max_retry, std::chrono::milliseconds(100));
                node.state.store(conn_state::idle, std::memory_order_release);
                node.last_used = std::chrono::steady_clock::now();
                notify_waiters_with_idle_locked();
                mtx_.unlock();
            }

            state = node.state.load(std::memory_order_acquire);
            if (state == conn_state::idle) {
                co_await async_sleep(ctx_, params_.health_check_interval);
                if (!running_) break;
            }

            state = node.state.load(std::memory_order_acquire);
            if (state == conn_state::in_use) {
                struct return_awaitable {
                    conn_node& n;
                    auto await_ready() const noexcept -> bool {
                        return n.state.load(std::memory_order_acquire) != conn_state::in_use;
                    }
                    void await_suspend(std::coroutine_handle<> h) noexcept {
                        n.task_waiting = h;
                    }
                    void await_resume() noexcept {}
                };
                co_await return_awaitable{node};
            }
        }
    }

    void spawn_connection() {
        std::size_t idx = conns_.size();
        conns_.emplace_back();
        auto& node = conns_.back();
        node.conn = std::make_unique<tcp_client>(ctx_);
        node.state.store(conn_state::initial, std::memory_order_release);
        node.last_used = std::chrono::steady_clock::now();
        node.index = idx;
        spawn(ctx_, connection_task(node));
    }

    static void dec_if_positive(std::atomic<std::size_t>& counter) {
        auto v = counter.load(std::memory_order_acquire);
        while (v > 0 && !counter.compare_exchange_weak(v, v - 1,
                                                       std::memory_order_release,
                                                       std::memory_order_relaxed)) {}
    }

    auto try_get_idle_locked() -> conn_node* {
        for (auto& node : conns_) {
            conn_state expected = conn_state::idle;
            if (node.state.compare_exchange_strong(expected, conn_state::in_use,
                                                   std::memory_order_acquire,
                                                   std::memory_order_relaxed)) {
                node.last_used = std::chrono::steady_clock::now();
                return &node;
            }
        }
        return nullptr;
    }

    auto try_get_idle_lockfree() -> conn_node* {
        for (auto& node : conns_) {
            conn_state expected = conn_state::idle;
            if (node.state.compare_exchange_strong(expected, conn_state::in_use,
                                                   std::memory_order_acquire,
                                                   std::memory_order_relaxed)) {
                node.last_used = std::chrono::steady_clock::now();
                return &node;
            }
        }
        return nullptr;
    }

    void notify_waiters_with_idle_locked() {
        while (waiters_head_) {
            auto* node = try_get_idle_locked();
            if (!node) break;

            auto* w = waiters_head_;
            waiters_head_ = w->next;
            if (!waiters_head_) waiters_tail_ = nullptr;
            dec_if_positive(waiters_count_);
            if (num_pending_requests_ > 0) --num_pending_requests_;

            if (w->token) {
                w->token->pending_.store(false, std::memory_order_release);
                w->token->cancel_fn_ = nullptr;
            }
            *w->result_node = node;
            if (w->handle) ctx_.post(w->handle);
        }
    }

    auto remove_waiter(pool_waiter* target) -> bool {
        pool_waiter* prev = nullptr;
        for (auto* w = waiters_head_; w; prev = w, w = w->next) {
            if (w == target) {
                if (prev) prev->next = w->next;
                else waiters_head_ = w->next;
                if (waiters_tail_ == w) waiters_tail_ = prev;
                w->next = nullptr;
                dec_if_positive(waiters_count_);
                if (num_pending_requests_ > 0) --num_pending_requests_;
                return true;
            }
        }
        return false;
    }

    void return_connection(conn_node& node) {
        auto mark_idle = [this, &node]() -> bool {
            conn_state expected = conn_state::in_use;
            if (!node.state.compare_exchange_strong(expected, conn_state::idle,
                                                    std::memory_order_release,
                                                    std::memory_order_relaxed)) {
                return false;
            }
            node.last_used = std::chrono::steady_clock::now();
            if (auto h = std::exchange(node.task_waiting, {}))
                ctx_.post(h);
            return true;
        };

        if (waiters_count_.load(std::memory_order_acquire) == 0) {
            if (mark_idle()) return;
        }

        if (mtx_.try_lock()) {
            if (mark_idle() || waiters_head_) {
                notify_waiters_with_idle_locked();
            }
            mtx_.unlock();
            return;
        }

        auto* node_ptr = &node;
        spawn(ctx_, [this, node_ptr]() -> task<void> {
            co_await mtx_.lock();
            conn_state expected = conn_state::in_use;
            if (node_ptr->state.compare_exchange_strong(expected, conn_state::idle,
                                                        std::memory_order_release,
                                                        std::memory_order_relaxed)) {
                node_ptr->last_used = std::chrono::steady_clock::now();
                if (auto h = std::exchange(node_ptr->task_waiting, {}))
                    ctx_.post(h);
            }
            if (waiters_head_) {
                notify_waiters_with_idle_locked();
            }
            mtx_.unlock();
        }());
    }
};

inline auto connection_pool::async_get_connection(cancel_token& token)
    -> task<std::expected<pooled_connection, std::error_code>>
{
    if (waiters_count_.load(std::memory_order_acquire) == 0) {
        if (auto* node = try_get_idle_lockfree()) {
            co_return pooled_connection(this, node);
        }
    }

    co_await mtx_.lock();
    async_lock_guard guard(mtx_, std::adopt_lock);

    if (waiters_head_) {
        notify_waiters_with_idle_locked();
    }

    if (!waiters_head_) {
        if (auto* node = try_get_idle_locked()) {
            co_return pooled_connection(this, node);
        }
    }

    ++num_pending_requests_;
    if (running_ && conns_.size() < params_.max_size) {
        spawn_connection();
    }

    conn_node* assigned = nullptr;
    pool_waiter waiter;
    waiter.result_node = &assigned;
    waiter.token = &token;

    struct waiter_awaitable {
        connection_pool& pool;
        pool_waiter& w;
        async_lock_guard& guard;
        cancel_token& token;

        auto await_ready() const noexcept -> bool { return false; }
        void await_suspend(std::coroutine_handle<> h) noexcept {
            w.handle = h;
            if (!pool.waiters_head_) {
                pool.waiters_head_ = pool.waiters_tail_ = &w;
            } else {
                pool.waiters_tail_->next = &w;
                pool.waiters_tail_ = &w;
            }
            pool.waiters_count_.fetch_add(1, std::memory_order_release);
            token.ctx_ = &pool;
            token.io_handle_ = &w;
            token.coroutine_ = h;
            token.cancel_fn_ = [](cancel_token& tok) noexcept {
                auto* p = static_cast<connection_pool*>(tok.ctx_);
                auto* wt = static_cast<pool_waiter*>(tok.io_handle_);
                if (p->mtx_.try_lock()) {
                    p->remove_waiter(wt);
                    p->mtx_.unlock();
                }
                p->ctx_.post(tok.coroutine_);
            };
            token.pending_.store(true, std::memory_order_release);
            guard.release();
            pool.mtx_.unlock();

            if (token.is_cancelled()) {
                if (pool.mtx_.try_lock()) {
                    pool.remove_waiter(&w);
                    pool.mtx_.unlock();
                }
                pool.ctx_.post(h);
            }
        }
        void await_resume() noexcept {
            token.pending_.store(false, std::memory_order_relaxed);
        }
    };

    co_await waiter_awaitable{*this, waiter, guard, token};

    if (token.is_cancelled()) {
        co_await mtx_.lock();
        remove_waiter(&waiter);
        mtx_.unlock();
        co_return std::unexpected(std::make_error_code(std::errc::timed_out));
    }

    if (assigned) {
        co_return pooled_connection(this, assigned);
    }
    co_return std::unexpected(std::make_error_code(std::errc::timed_out));
}

inline void pooled_connection::return_to_pool() {
    if (pool_ && node_) {
        pool_->return_connection(*node_);
        pool_ = nullptr;
        node_ = nullptr;
    }
}

} // namespace cnetmod::modbus
