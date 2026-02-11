module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mysql:pool;

import std;
import :types;
import :diagnostics;
import :client;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.coro.mutex;

namespace cnetmod::mysql {

// =============================================================================
// pool_params — 连接池配置（参考 Boost.MySQL pool_params）
// =============================================================================

export struct pool_params {
    // 连接参数
    std::string host     = "127.0.0.1";
    std::uint16_t port   = 3306;
    std::string username;
    std::string password;
    std::string database;
    ssl_mode    ssl      = ssl_mode::enable;

    // 池大小
    std::size_t initial_size = 1;      // 初始连接数
    std::size_t max_size     = 16;     // 最大连接数

    // 超时与健康检查
    std::chrono::steady_clock::duration connect_timeout  = std::chrono::seconds(20);
    std::chrono::steady_clock::duration retry_interval   = std::chrono::seconds(30);
    std::chrono::steady_clock::duration ping_interval    = std::chrono::hours(1);
    std::chrono::steady_clock::duration ping_timeout     = std::chrono::seconds(10);

    // TLS 选项
    bool        tls_verify    = false;
    std::string tls_ca_file;
};

// =============================================================================
// 连接节点状态
// =============================================================================

enum class conn_state : std::uint8_t {
    idle,           // 空闲可用
    in_use,         // 已借出
    connecting,     // 正在连接
    dead,           // 连接已断开
};

struct conn_node {
    std::unique_ptr<client> conn;
    conn_state              state       = conn_state::dead;
    std::chrono::steady_clock::time_point last_used;
};

// =============================================================================
// pooled_connection — RAII 借出连接（参考 Boost.MySQL pooled_connection）
// =============================================================================
//
// 析构时自动归还连接到池中（标记需要 reset）。

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
            return_to_pool(true);
            pool_ = std::exchange(o.pool_, nullptr);
            idx_  = o.idx_;
            conn_ = std::exchange(o.conn_, nullptr);
        }
        return *this;
    }

    pooled_connection(const pooled_connection&) = delete;
    auto operator=(const pooled_connection&) -> pooled_connection& = delete;

    ~pooled_connection() { return_to_pool(true); }

    auto valid() const noexcept -> bool { return conn_ != nullptr; }

    auto get() noexcept -> client& { return *conn_; }
    auto get() const noexcept -> const client& { return *conn_; }

    auto operator->() noexcept -> client* { return conn_; }
    auto operator->() const noexcept -> const client* { return conn_; }

    /// 归还连接但跳过 reset（性能优化，需确保未修改会话状态）
    void return_without_reset();

private:
    friend class connection_pool;

    connection_pool* pool_ = nullptr;
    std::size_t      idx_  = 0;
    client*          conn_ = nullptr;

    pooled_connection(connection_pool* pool, std::size_t idx, client* c) noexcept
        : pool_(pool), idx_(idx), conn_(c) {}

    void return_to_pool(bool needs_reset);
};

// =============================================================================
// connection_pool — 异步连接池（参考 Boost.MySQL connection_pool）
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

    // ── async_run — 启动连接池（建立初始连接 + 后台健康检查）──

    auto async_run() -> task<void> {
        running_ = true;

        // 建立初始连接
        for (std::size_t i = 0; i < params_.initial_size && i < params_.max_size; ++i) {
            co_await create_and_connect();
        }

        // 后台健康检查循环
        while (running_) {
            co_await async_sleep(ctx_, params_.ping_interval);
            if (!running_) break;
            co_await health_check();
        }
    }

    // ── async_get_connection — 借出一个空闲连接 ──────────────

    auto async_get_connection() -> task<pooled_connection> {
        // 尝试获取空闲连接
        co_await mtx_.lock();
        async_lock_guard guard(mtx_, std::adopt_lock);

        // 查找空闲节点
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (nodes_[i].state == conn_state::idle) {
                nodes_[i].state = conn_state::in_use;
                nodes_[i].last_used = std::chrono::steady_clock::now();
                co_return pooled_connection(this, i, nodes_[i].conn.get());
            }
        }

        // 没有空闲连接 — 尝试创建新连接
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
        }

        // 所有连接都忙且已达最大 — 等待后重试
        guard.release();
        mtx_.unlock();

        for (int attempt = 0; attempt < 50; ++attempt) {
            co_await async_sleep(ctx_, std::chrono::milliseconds(100));

            co_await mtx_.lock();
            async_lock_guard retry_guard(mtx_, std::adopt_lock);
            for (std::size_t i = 0; i < nodes_.size(); ++i) {
                if (nodes_[i].state == conn_state::idle) {
                    nodes_[i].state = conn_state::in_use;
                    nodes_[i].last_used = std::chrono::steady_clock::now();
                    co_return pooled_connection(this, i, nodes_[i].conn.get());
                }
            }
        }

        // 超时 — 返回无效 pooled_connection
        co_return pooled_connection{};
    }

    // ── cancel / 关闭 ───────────────────────────────────────

    auto cancel() -> task<void> {
        running_ = false;
        co_await mtx_.lock();
        async_lock_guard guard(mtx_, std::adopt_lock);
        for (auto& node : nodes_) {
            if (node.conn && node.conn->is_open()) {
                // 不等待 quit 完成
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

    auto create_and_connect() -> task<std::size_t> {
        auto c = std::make_unique<client>(ctx_);
        auto opts = make_connect_options();

        auto rs = co_await c->connect(opts);
        if (rs.is_err()) {
            // 连接失败 — 不添加到池中
            co_return nodes_.size(); // 返回无效索引
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
        // 收集需要 ping 的空闲连接索引
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

            auto rs = co_await c->ping();
            co_await mtx_.lock();
            if (rs.is_err()) {
                // ping 失败 — 标记死亡，尝试重连
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
        if (rs.is_err()) {
            nodes_[idx].state = conn_state::dead;
        } else {
            nodes_[idx].conn  = std::move(c);
            nodes_[idx].state = conn_state::idle;
            nodes_[idx].last_used = std::chrono::steady_clock::now();
        }
    }

    void return_connection(std::size_t idx, [[maybe_unused]] bool needs_reset) {
        // 注意：此函数同步调用，不加锁（由调用者保证线程安全或在析构中调用）
        if (idx < nodes_.size()) {
            nodes_[idx].state = conn_state::idle;
            nodes_[idx].last_used = std::chrono::steady_clock::now();
            // needs_reset: 实际生产中应异步调用 reset_connection
            // 简化版直接标记为 idle
        }
    }
};

// =============================================================================
// pooled_connection 方法实现
// =============================================================================

inline void pooled_connection::return_to_pool(bool needs_reset) {
    if (pool_ && conn_) {
        pool_->return_connection(idx_, needs_reset);
        pool_ = nullptr;
        conn_ = nullptr;
    }
}

inline void pooled_connection::return_without_reset() {
    return_to_pool(false);
}

} // namespace cnetmod::mysql
