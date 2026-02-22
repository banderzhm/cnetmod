module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.redis:pool;

import std;
import :types;
import :client;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.coro.mutex;

namespace cnetmod::redis {

// =============================================================================
// pool_params — Connection pool configuration
// =============================================================================

export struct pool_params {
    // Connection parameters
    std::string   host     = "127.0.0.1";
    std::uint16_t port     = 6379;
    std::string   password;            // AUTH password (empty = no auth)
    std::string   username;            // Redis 6+ ACL username (empty = default)
    std::uint32_t db       = 0;       // SELECT database number (0 = default)
    bool          resp3    = true;    // Use RESP3 protocol

    // Pool size
    std::size_t initial_size = 1;      // Initial connection count
    std::size_t max_size     = 16;     // Maximum connection count

    // Timeout and health check
    std::chrono::steady_clock::duration connect_timeout  = std::chrono::seconds(10);
    std::chrono::steady_clock::duration retry_interval   = std::chrono::seconds(30);
    std::chrono::steady_clock::duration ping_interval    = std::chrono::minutes(5);

    // TLS configuration
    bool        tls           = false;
    bool        tls_verify    = true;
    std::string tls_ca_file;
    std::string tls_cert_file;
    std::string tls_key_file;
    std::string tls_sni;
};

// =============================================================================
// Connection node state
// =============================================================================

enum class conn_state : std::uint8_t {
    idle,           // Idle and available
    in_use,         // Borrowed
    connecting,     // Connecting
    dead,           // Connection closed
};

struct conn_node {
    std::unique_ptr<client> conn;
    conn_state              state     = conn_state::dead;
    std::chrono::steady_clock::time_point last_used;
};

/// Waiter queue node — for asynchronously waiting for idle connections
struct pool_waiter {
    std::coroutine_handle<> handle{};
    std::size_t* result_idx = nullptr;
    pool_waiter* next = nullptr;
};

// =============================================================================
// pooled_connection — RAII borrowed connection
// =============================================================================
//
// Automatically returns connection to pool on destruction.

// Forward declare
export class connection_pool;

export class pooled_connection {
public:
    pooled_connection() noexcept = default;

    pooled_connection(pooled_connection&& o) noexcept
        : pool_(std::exchange(o.pool_, nullptr))
        , idx_(o.idx_)
        , conn_(std::exchange(o.conn_, nullptr))
    {}

    auto operator=(pooled_connection&& o) noexcept -> pooled_connection& {
        if (this != &o) {
            return_to_pool();
            pool_ = std::exchange(o.pool_, nullptr);
            idx_  = o.idx_;
            conn_ = std::exchange(o.conn_, nullptr);
        }
        return *this;
    }

    pooled_connection(const pooled_connection&) = delete;
    auto operator=(const pooled_connection&) -> pooled_connection& = delete;

    ~pooled_connection() { return_to_pool(); }

    auto valid() const noexcept -> bool { return conn_ != nullptr; }

    auto get() noexcept -> client& { return *conn_; }
    auto get() const noexcept -> const client& { return *conn_; }

    auto operator->() noexcept -> client* { return conn_; }
    auto operator->() const noexcept -> const client* { return conn_; }

private:
    friend class connection_pool;

    connection_pool* pool_ = nullptr;
    std::size_t      idx_  = 0;
    client*          conn_ = nullptr;

    pooled_connection(connection_pool* pool, std::size_t idx, client* c) noexcept
        : pool_(pool), idx_(idx), conn_(c) {}

    void return_to_pool();
};

// =============================================================================
// connection_pool — Async connection pool
// =============================================================================

export class connection_pool {
public:
    connection_pool(io_context& ctx, pool_params params)
        : ctx_(ctx), params_(std::move(params))
    {
        nodes_.reserve(params_.max_size);
    }

    connection_pool(const connection_pool&) = delete;
    auto operator=(const connection_pool&) -> connection_pool& = delete;

    // ── async_run — Start pool (establish initial connections + background health check) ──

    auto async_run() -> task<void> {
        running_ = true;

        // Establish initial connections
        for (std::size_t i = 0; i < params_.initial_size && i < params_.max_size; ++i) {
            co_await create_and_connect();
        }

        // Background health check loop
        while (running_) {
            co_await async_sleep(ctx_, params_.ping_interval);
            if (!running_) break;
            co_await health_check();
        }
    }

    // ── async_get_connection — Borrow an idle connection ──

    auto async_get_connection() -> task<pooled_connection> {
        co_await mtx_.lock();
        async_lock_guard guard(mtx_, std::adopt_lock);

        // 1) Find idle node
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (nodes_[i].state == conn_state::idle) {
                nodes_[i].state = conn_state::in_use;
                nodes_[i].last_used = std::chrono::steady_clock::now();
                co_return pooled_connection(this, i, nodes_[i].conn.get());
            }
        }

        // 2) No idle connections — try to create new
        if (nodes_.size() < params_.max_size) {
            guard.release();
            mtx_.unlock();

            auto idx = co_await create_and_connect();
            if (idx < nodes_.size()) {
                co_await mtx_.lock();
                async_lock_guard guard2(mtx_, std::adopt_lock);
                nodes_[idx].state = conn_state::in_use;
                nodes_[idx].last_used = std::chrono::steady_clock::now();
                co_return pooled_connection(this, idx, nodes_[idx].conn.get());
            }

            // Creation failed, reacquire lock
            co_await mtx_.lock();
            guard = async_lock_guard(mtx_, std::adopt_lock);

            // Double check: connections may have been returned during creation
            for (std::size_t i = 0; i < nodes_.size(); ++i) {
                if (nodes_[i].state == conn_state::idle) {
                    nodes_[i].state = conn_state::in_use;
                    nodes_[i].last_used = std::chrono::steady_clock::now();
                    co_return pooled_connection(this, i, nodes_[i].conn.get());
                }
            }
        }

        // 3) All connections busy — suspend on waiter queue
        std::size_t assigned_idx = nodes_.size();
        pool_waiter waiter;
        waiter.result_idx = &assigned_idx;

        struct waiter_awaitable {
            connection_pool& pool;
            pool_waiter& w;
            async_lock_guard& guard;

            auto await_ready() const noexcept -> bool { return false; }
            void await_suspend(std::coroutine_handle<> h) noexcept {
                w.handle = h;
                if (!pool.waiters_head_) {
                    pool.waiters_head_ = pool.waiters_tail_ = &w;
                } else {
                    pool.waiters_tail_->next = &w;
                    pool.waiters_tail_ = &w;
                }
                guard.release();
                pool.mtx_.unlock();
            }
            void await_resume() noexcept {}
        };

        co_await waiter_awaitable{*this, waiter, guard};

        if (assigned_idx < nodes_.size()) {
            co_return pooled_connection(this, assigned_idx, nodes_[assigned_idx].conn.get());
        }
        co_return pooled_connection{};
    }

    // ── cancel / close ──

    auto cancel() -> task<void> {
        running_ = false;
        co_await mtx_.lock();
        async_lock_guard guard(mtx_, std::adopt_lock);
        for (auto& node : nodes_) {
            if (node.conn && node.conn->is_open()) {
                node.conn->close();
                node.state = conn_state::dead;
            }
        }
    }

    auto size() const noexcept -> std::size_t { return nodes_.size(); }

    auto idle_count() const noexcept -> std::size_t {
        std::size_t n = 0;
        for (auto& nd : nodes_)
            if (nd.state == conn_state::idle) ++n;
        return n;
    }

private:
    friend class pooled_connection;

    io_context&  ctx_;
    pool_params  params_;
    std::vector<conn_node> nodes_;
    async_mutex  mtx_;
    bool         running_ = false;
    pool_waiter* waiters_head_ = nullptr;
    pool_waiter* waiters_tail_ = nullptr;

    auto make_connect_options() const -> connect_options {
        connect_options opts;
        opts.host         = params_.host;
        opts.port         = params_.port;
        opts.password     = params_.password;
        opts.username     = params_.username;
        opts.db           = params_.db;
        opts.resp3        = params_.resp3;
        opts.tls          = params_.tls;
        opts.tls_verify   = params_.tls_verify;
        opts.tls_ca_file  = params_.tls_ca_file;
        opts.tls_cert_file = params_.tls_cert_file;
        opts.tls_key_file = params_.tls_key_file;
        opts.tls_sni      = params_.tls_sni;
        return opts;
    }

    auto create_and_connect() -> task<std::size_t> {
        auto c = std::make_unique<client>(ctx_);
        auto opts = make_connect_options();

        auto rs = co_await c->connect(opts);
        if (!rs) {
            co_return nodes_.size(); // Return invalid index
        }

        co_await mtx_.lock();
        async_lock_guard guard(mtx_, std::adopt_lock);
        auto idx = nodes_.size();
        conn_node node;
        node.conn  = std::move(c);
        node.state = conn_state::idle;
        node.last_used = std::chrono::steady_clock::now();
        nodes_.push_back(std::move(node));
        co_return idx;
    }

    auto health_check() -> task<void> {
        auto now = std::chrono::steady_clock::now();

        co_await mtx_.lock();
        // Collect idle connection indices that need ping
        std::vector<std::size_t> to_ping;
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (nodes_[i].state == conn_state::idle &&
                (now - nodes_[i].last_used) >= params_.ping_interval) {
                to_ping.push_back(i);
            }
        }
        mtx_.unlock();

        for (auto idx : to_ping) {
            co_await mtx_.lock();
            if (idx >= nodes_.size() || nodes_[idx].state != conn_state::idle) {
                mtx_.unlock();
                continue;
            }
            auto* c = nodes_[idx].conn.get();
            mtx_.unlock();

            // Redis PING → expects PONG
            auto rs = co_await c->cmd({"PING"});
            co_await mtx_.lock();
            if (!rs || has_error(*rs)) {
                // Ping failed — mark as dead, try to reconnect
                nodes_[idx].state = conn_state::dead;
                mtx_.unlock();
                co_await reconnect(idx);
            } else {
                nodes_[idx].last_used = std::chrono::steady_clock::now();
                mtx_.unlock();
            }
        }
    }

    auto reconnect(std::size_t idx) -> task<void> {
        auto c = std::make_unique<client>(ctx_);
        auto opts = make_connect_options();
        auto rs = co_await c->connect(opts);

        co_await mtx_.lock();
        async_lock_guard guard(mtx_, std::adopt_lock);
        if (!rs) {
            nodes_[idx].state = conn_state::dead;
        } else {
            nodes_[idx].conn  = std::move(c);
            nodes_[idx].state = conn_state::idle;
            nodes_[idx].last_used = std::chrono::steady_clock::now();
        }
    }

    void return_connection(std::size_t idx) {
        if (idx >= nodes_.size())
            return;

        if (mtx_.try_lock()) {
            if (waiters_head_) {
                auto* w = waiters_head_;
                waiters_head_ = w->next;
                if (!waiters_head_)
                    waiters_tail_ = nullptr;

                nodes_[idx].state = conn_state::in_use;
                nodes_[idx].last_used = std::chrono::steady_clock::now();
                *w->result_idx = idx;
                mtx_.unlock();

                if (w->handle)
                    ctx_.post(w->handle);
                return;
            }
            nodes_[idx].state = conn_state::idle;
            nodes_[idx].last_used = std::chrono::steady_clock::now();
            mtx_.unlock();
        } else {
            nodes_[idx].state = conn_state::idle;
            nodes_[idx].last_used = std::chrono::steady_clock::now();
        }
    }
};

// =============================================================================
// pooled_connection method implementation
// =============================================================================

inline void pooled_connection::return_to_pool() {
    if (pool_ && conn_) {
        pool_->return_connection(idx_);
        pool_ = nullptr;
        conn_ = nullptr;
    }
}

} // namespace cnetmod::redis
