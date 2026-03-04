module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mysql:pool;

import std;
import :types;
import :diagnostics;
import :client;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.coro.mutex;
import cnetmod.coro.cancel;

namespace cnetmod::mysql {

// =============================================================================
// pool_params
// =============================================================================

export struct pool_params {
    std::string host     = "127.0.0.1";
    std::uint16_t port   = 3306;
    std::string username;
    std::string password;
    std::string database;
    ssl_mode    ssl      = ssl_mode::enable;

    std::size_t initial_size = 1;
    std::size_t max_size     = 16;

    std::chrono::steady_clock::duration connect_timeout  = std::chrono::seconds(20);
    std::chrono::steady_clock::duration pool_timeout     = std::chrono::seconds(5);
    std::chrono::steady_clock::duration retry_interval   = std::chrono::seconds(30);
    std::chrono::steady_clock::duration ping_interval    = std::chrono::hours(1);
    std::chrono::steady_clock::duration ping_timeout     = std::chrono::seconds(10);

    bool        tls_verify    = false;
    std::string tls_ca_file;
};

// =============================================================================
// Connection node — each node has its own lifecycle task (P0: Boost pattern)
// =============================================================================

enum class conn_state : std::uint8_t {
    initial,
    connecting,
    idle,
    in_use,
    resetting,
    pinging,
    dead,
};

struct conn_node {
    std::unique_ptr<client> conn;
    conn_state              state       = conn_state::initial;
    std::chrono::steady_clock::time_point last_used;
    bool                    needs_reset = false;
    std::coroutine_handle<> task_waiting{};  // task suspends here when in_use
};

/// Waiter queue node
struct pool_waiter {
    std::coroutine_handle<> handle{};
    conn_node**             result_node = nullptr;  // P2: pointer instead of index
    pool_waiter*            next = nullptr;
    cancel_token*           token = nullptr;
};

// Forward declare
export class connection_pool;

// =============================================================================
// pooled_connection — RAII borrowed connection (P2: stores conn_node* not index)
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
            return_to_pool(true);
            pool_ = std::exchange(o.pool_, nullptr);
            node_ = std::exchange(o.node_, nullptr);
        }
        return *this;
    }

    pooled_connection(const pooled_connection&) = delete;
    auto operator=(const pooled_connection&) -> pooled_connection& = delete;

    ~pooled_connection() { return_to_pool(true); }

    auto valid() const noexcept -> bool { return node_ != nullptr; }
    auto get() noexcept -> client& { return *node_->conn; }
    auto get() const noexcept -> const client& { return *node_->conn; }
    auto operator->() noexcept -> client* { return node_->conn.get(); }
    auto operator->() const noexcept -> const client* { return node_->conn.get(); }

    void return_without_reset();

private:
    friend class connection_pool;

    connection_pool* pool_ = nullptr;
    conn_node*       node_ = nullptr;

    pooled_connection(connection_pool* pool, conn_node* node) noexcept
        : pool_(pool), node_(node) {}

    void return_to_pool(bool needs_reset);
};

// =============================================================================
// connection_pool — P0: per-connection task, P1: demand-driven scaling, P2: std::list
// =============================================================================

export class connection_pool {
public:
    connection_pool(io_context& ctx, pool_params params)
        : ctx_(ctx), params_(std::move(params)) {}

    connection_pool(const connection_pool&) = delete;
    auto operator=(const connection_pool&) -> connection_pool& = delete;

    // ── async_run — spawn initial connection tasks, then health-check loop ──

    auto async_run() -> task<void> {
        running_ = true;
        // Spawn initial connection tasks
        for (std::size_t i = 0; i < params_.initial_size && i < params_.max_size; ++i) {
            spawn_connection();
        }
        // Background: periodic scan for dead connections that have no task
        while (running_) {
            co_await async_sleep(ctx_, params_.ping_interval);
        }
    }

    // ── async_get_connection ──

    auto async_get_connection(cancel_token& token)
        -> task<std::expected<pooled_connection, std::error_code>>;

    auto async_get_connection()
        -> task<std::expected<pooled_connection, std::error_code>>
    {
        cancel_token token;
        co_return co_await with_timeout(ctx_, params_.pool_timeout,
            async_get_connection(token), token);
    }

    auto cancel() -> task<void> {
        running_ = false;
        co_await mtx_.lock();
        for (auto& node : conns_) {
            if (node.conn && node.conn->is_open())
                node.state = conn_state::dead;
        }
        mtx_.unlock();
    }

    auto size() const noexcept -> std::size_t { return conns_.size(); }

    auto idle_count() const noexcept -> std::size_t {
        std::size_t n = 0;
        for (auto& nd : conns_)
            if (nd.state == conn_state::idle) ++n;
        return n;
    }

private:
    friend class pooled_connection;

    io_context&            ctx_;
    pool_params            params_;
    std::list<conn_node>   conns_;          // P2: stable addresses
    async_mutex            mtx_;
    bool                   running_ = false;
    pool_waiter*           waiters_head_ = nullptr;
    pool_waiter*           waiters_tail_ = nullptr;
    std::size_t            num_pending_requests_ = 0;  // P1: for demand formula

    auto make_connect_options() const -> connect_options {
        connect_options opts;
        opts.host     = params_.host;
        opts.port     = params_.port;
        opts.username = params_.username;
        opts.password = params_.password;
        opts.database = params_.database;
        opts.ssl      = params_.ssl;
        opts.tls_verify = params_.tls_verify;
        opts.tls_ca_file = params_.tls_ca_file;
        return opts;
    }

    // ── P0: Per-connection autonomous lifecycle task ──

    auto connection_task(conn_node& node) -> task<void> {
        while (running_) {
            // Phase 1: Ensure connected
            if (node.state == conn_state::initial || node.state == conn_state::dead) {
                node.state = conn_state::connecting;
                auto opts = make_connect_options();
                auto rs = co_await node.conn->connect(opts);

                co_await mtx_.lock();
                if (rs.is_err()) {
                    node.state = conn_state::dead;
                    mtx_.unlock();
                    co_await async_sleep(ctx_, params_.retry_interval);
                    continue;
                }
                node.state = conn_state::idle;
                node.last_used = std::chrono::steady_clock::now();
                // Hand to waiter if any, otherwise stays idle
                try_notify_one_waiter(node);
                mtx_.unlock();
            }

            // Phase 2: Idle — wait ping_interval then ping
            if (node.state == conn_state::idle) {
                co_await async_sleep(ctx_, params_.ping_interval);
                if (!running_) break;

                co_await mtx_.lock();
                if (node.state != conn_state::idle) {
                    mtx_.unlock();
                    continue;  // Was borrowed during sleep
                }
                node.state = conn_state::pinging;
                mtx_.unlock();

                auto rs = co_await node.conn->ping();

                co_await mtx_.lock();
                if (rs.is_err()) {
                    node.state = conn_state::dead;
                    mtx_.unlock();
                    continue;  // Will reconnect next iteration
                }
                node.state = conn_state::idle;
                node.last_used = std::chrono::steady_clock::now();
                mtx_.unlock();
            }

            // If in_use, suspend until returned (zero-cost wait)
            if (node.state == conn_state::in_use) {
                struct return_awaitable {
                    conn_node& n;
                    auto await_ready() const noexcept -> bool {
                        return n.state != conn_state::in_use;
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
        conns_.emplace_back();
        auto& node = conns_.back();
        node.conn = std::make_unique<client>(ctx_);
        node.state = conn_state::initial;
        node.last_used = std::chrono::steady_clock::now();
        spawn(ctx_, connection_task(node));
    }

    // ── P1: Demand-driven scaling (Boost formula) ──

    std::size_t count_pending_conns() const {
        std::size_t n = 0;
        for (auto& nd : conns_)
            if (nd.state == conn_state::connecting || nd.state == conn_state::initial)
                ++n;
        return n;
    }

    void create_connections_if_needed() {
        // Must hold lock
        std::size_t pending = count_pending_conns();
        std::size_t room = params_.max_size - conns_.size();
        std::size_t needed = (num_pending_requests_ > pending)
                           ? (num_pending_requests_ - pending) : 0;
        std::size_t to_create = std::min(needed, room);
        for (std::size_t i = 0; i < to_create; ++i)
            spawn_connection();
    }

    // ── Waiter queue helpers ──

    void try_notify_one_waiter(conn_node& node) {
        // Must hold lock. If waiter exists, hand connection directly.
        if (!waiters_head_ || node.state != conn_state::idle) return;

        auto* w = waiters_head_;
        waiters_head_ = w->next;
        if (!waiters_head_) waiters_tail_ = nullptr;

        if (w->token) {
            w->token->pending_.store(false, std::memory_order_release);
            w->token->cancel_fn_ = nullptr;
        }

        node.state = conn_state::in_use;
        node.last_used = std::chrono::steady_clock::now();
        *w->result_node = &node;

        if (w->handle) ctx_.post(w->handle);
    }

    auto remove_waiter(pool_waiter* target) -> bool {
        pool_waiter* prev = nullptr;
        for (auto* w = waiters_head_; w; prev = w, w = w->next) {
            if (w == target) {
                if (prev) prev->next = w->next;
                else waiters_head_ = w->next;
                if (waiters_tail_ == w) waiters_tail_ = prev;
                w->next = nullptr;
                return true;
            }
        }
        return false;
    }

    // ── Return connection to pool ──

    // ── testOnBorrow: validate stale connections ──

    auto try_get_idle(async_lock_guard& guard)
        -> task<conn_node*>
    {
        // Must hold lock
        auto now = std::chrono::steady_clock::now();
        auto stale_threshold = params_.ping_interval / 2;

        for (auto& node : conns_) {
            if (node.state != conn_state::idle) continue;

            if ((now - node.last_used) >= stale_threshold) {
                node.state = conn_state::pinging;
                auto* c = node.conn.get();
                guard.release();
                mtx_.unlock();

                auto rs = co_await c->ping();

                co_await mtx_.lock();
                guard = async_lock_guard(mtx_, std::adopt_lock);
                if (rs.is_err()) {
                    node.state = conn_state::dead;
                    continue;
                }
                node.state = conn_state::in_use;
                node.last_used = std::chrono::steady_clock::now();
                co_return &node;
            }

            node.state = conn_state::in_use;
            node.last_used = std::chrono::steady_clock::now();
            co_return &node;
        }
        co_return nullptr;
    }

    void return_connection(conn_node& node, bool needs_reset) {
        if (mtx_.try_lock()) {
            if (node.state == conn_state::in_use) {
                node.needs_reset = needs_reset;
                node.state = conn_state::idle;
                node.last_used = std::chrono::steady_clock::now();
                try_notify_one_waiter(node);
                // Wake connection_task
                if (auto h = std::exchange(node.task_waiting, {}))
                    ctx_.post(h);
            }
            mtx_.unlock();
        } else {
            spawn(ctx_, [this, &node, needs_reset]() -> task<void> {
                co_await mtx_.lock();
                if (node.state == conn_state::in_use) {
                    node.needs_reset = needs_reset;
                    node.state = conn_state::idle;
                    node.last_used = std::chrono::steady_clock::now();
                    try_notify_one_waiter(node);
                    if (auto h = std::exchange(node.task_waiting, {}))
                        ctx_.post(h);
                }
                mtx_.unlock();
            }());
        }
    }
};

// =============================================================================
// async_get_connection — out-of-class definition
// =============================================================================

inline auto connection_pool::async_get_connection(cancel_token& token)
    -> task<std::expected<pooled_connection, std::error_code>>
{
    co_await mtx_.lock();
    async_lock_guard guard(mtx_, std::adopt_lock);

    // 1) Try to find an idle connection (with testOnBorrow for stale ones)
    if (auto* node = co_await try_get_idle(guard)) {
        co_return pooled_connection(this, node);
    }

    // 2) P1: Record pending request + demand-driven scaling
    ++num_pending_requests_;
    if (running_) create_connections_if_needed();

    // 3) Wait in queue (cancellable via cancel_token)
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

    // Decrement pending requests
    co_await mtx_.lock();
    if (num_pending_requests_ > 0) --num_pending_requests_;
    mtx_.unlock();

    if (token.is_cancelled()) {
        co_await mtx_.lock();
        remove_waiter(&waiter);
        mtx_.unlock();
        co_return std::unexpected(make_error_code(std::errc::timed_out));
    }

    if (assigned) {
        co_return pooled_connection(this, assigned);
    }
    co_return std::unexpected(make_error_code(std::errc::timed_out));
}

// =============================================================================
// pooled_connection method implementations
// =============================================================================

inline void pooled_connection::return_to_pool(bool needs_reset) {
    if (pool_ && node_) {
        pool_->return_connection(*node_, needs_reset);
        pool_ = nullptr;
        node_ = nullptr;
    }
}

inline void pooled_connection::return_without_reset() {
    return_to_pool(false);
}

} // namespace cnetmod::mysql