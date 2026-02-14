/// cnetmod example — High-level HTTP Server (Plus)
/// 演示功能：
///   1. 踢人下线（kick）— 管理员通过 /admin/kick/:token 踢掉指定会话
///   2. 请求统计（stats）— /admin/stats 返回实时请求统计 JSON
///   3. 限流（rate limit）— Token Bucket 算法，O(1) 判定，支持突发，带 Retry-After
///   4. 会话管理 — CSPRNG 令牌、TTL 过期、同 IP 会话限制、后台 GC 清理
///   5. 协程锁（async_mutex）— 保护共享数据的并发安全
///   6. channel 异步任务队列 — 用户提交耗时任务，后台 worker 消费处理，轮询查状态
///   7. 反向代理 IP 解析 — 支持 X-Forwarded-For / X-Real-IP
///
/// Server 端注册多条路由，Client 端发送多个请求验证各功能

#include <cnetmod/config.hpp>

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.address;
import cnetmod.core.socket;
import cnetmod.core.net_init;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.coro.mutex;
import cnetmod.coro.channel;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;
import cnetmod.protocol.tcp;
import cnetmod.protocol.http;

namespace cn = cnetmod;
namespace http = cnetmod::http;

constexpr std::uint16_t PORT = 19090;

// =============================================================================
// 通用 enum ↔ string 映射模板（traits 特化模式）
// =============================================================================

/// 主模板 — 每个 enum 特化提供 static constexpr map
template <typename E>
struct enum_traits;

/// 泛型 to_string / from_string — 通过 enum_traits 自动查表
template <typename E>
constexpr auto to_string(E val, std::string_view fallback = "unknown") -> std::string_view {
    for (auto& [k, v] : enum_traits<E>::map)
        if (k == val) return v;
    return fallback;
}

template <typename E>
constexpr auto from_string(std::string_view s) -> std::optional<E> {
    for (auto& [k, v] : enum_traits<E>::map)
        if (v == s) return k;
    return std::nullopt;
}

// =============================================================================
// 异步任务 — 通过 channel 传递给后台 worker 处理
// =============================================================================

enum class task_type {
    generate_report,
    export_data,
    send_email,
};

template <> struct enum_traits<task_type> {
    static constexpr std::pair<task_type, std::string_view> map[] = {
        {task_type::generate_report, "generate_report"},
        {task_type::export_data,     "export_data"},
        {task_type::send_email,      "send_email"},
    };
};

/// 提交到 channel 的任务载荷
struct task_job {
    std::string task_id;
    task_type   type;
    std::string params;   // 任务参数（如 "user_id=42&month=2026-01"）
};

/// 任务状态记录（存储在 shared_state 中，worker 更新，handler 查询）
enum class task_status { pending, processing, done, failed };

template <> struct enum_traits<task_status> {
    static constexpr std::pair<task_status, std::string_view> map[] = {
        {task_status::pending,    "pending"},
        {task_status::processing, "processing"},
        {task_status::done,       "done"},
        {task_status::failed,     "failed"},
    };
};

enum class session_status { active, kicked, expired };

template <> struct enum_traits<session_status> {
    static constexpr std::pair<session_status, std::string_view> map[] = {
        {session_status::active,  "active"},
        {session_status::kicked,  "kicked"},
        {session_status::expired, "expired"},
    };
};

struct task_record {
    std::string task_id;
    task_type   type;
    std::string params;
    task_status status = task_status::pending;
    std::string result;   // 完成后的结果
    std::string submitted_by;  // 提交者 token
};

/// 全局任务 channel（生产者：HTTP handler，消费者：后台 worker）
static cn::channel<task_job>* g_task_ch = nullptr;

// =============================================================================
// 服务器配置
// =============================================================================

struct server_config {
    // 限流 — Token Bucket
    double rate_limit_rate  = 10.0;    // 每秒补充令牌数
    double rate_limit_burst = 20.0;    // 桶容量（最大突发请求数）

    // 会话
    std::chrono::seconds session_ttl{1800};          // 空闲超时（默认 30 分钟）
    std::chrono::seconds session_gc_interval{60};    // GC 扫描间隔
    std::size_t max_sessions_per_ip = 10;            // 同一 IP 最大并发会话数

    // 限流条目过期清理（长时间无请求的 IP 桶回收）
    std::chrono::seconds rate_limit_entry_ttl{300};  // 5 分钟无请求则回收
};

// =============================================================================
// 请求统计
// =============================================================================

struct server_stats {
    std::atomic<std::uint64_t> total_requests{0};
    std::atomic<std::uint64_t> total_success{0};       // 2xx + 3xx
    std::atomic<std::uint64_t> total_client_err{0};    // 4xx
    std::atomic<std::uint64_t> total_server_err{0};    // 5xx
    std::atomic<std::uint64_t> total_rejected{0};      // 被限流拒绝
    std::atomic<std::uint64_t> total_kicked{0};
    std::atomic<std::uint64_t> sessions_created{0};
    std::atomic<std::uint64_t> sessions_expired{0};
    std::atomic<std::uint64_t> active_sessions{0};     // 实时活跃会话数，login++ kick/expire--
    std::atomic<std::uint64_t> rate_limit_buckets{0};  // 限流桶数量，try_emplace++ erase--
    std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
};

// =============================================================================
// 会话管理
// =============================================================================

struct session_info {
    std::string token;
    std::string client_ip;
    std::string user_agent;
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point last_activity;
    session_status status = session_status::active;
};

// =============================================================================
// Token Bucket 限流器（O(1) 判定，业界标准算法）
// =============================================================================
//
// 相比滑动窗口的优势：
//   - O(1) 时间和空间，不存储每个请求的时间戳
//   - 自然支持突发流量（burst）
//   - 平滑限速，不会出现窗口边界的突变

struct token_bucket {
    double tokens;
    std::chrono::steady_clock::time_point last_refill;

    /// 补充令牌并尝试消费 1 个。返回 true 表示放行。
    auto try_consume(double rate, double burst,
                     std::chrono::steady_clock::time_point now) -> bool {
        auto elapsed = std::chrono::duration<double>(now - last_refill).count();
        tokens = std::min(burst, tokens + elapsed * rate);
        last_refill = now;
        if (tokens >= 1.0) {
            tokens -= 1.0;
            return true;
        }
        return false;
    }

    /// 距下一个令牌可用的秒数（用于 Retry-After 响应头）
    [[nodiscard]] auto retry_after(double rate) const -> double {
        if (tokens >= 1.0) return 0.0;
        return (1.0 - tokens) / rate;
    }
};

// =============================================================================
// 共享状态 — 跨路由共享，用 async_mutex 保护
// =============================================================================

struct shared_state {
    server_config config;
    server_stats  stats;       // atomic 计数器，无锁读写

    // --- 会话域（session_mtx 保护）---
    cn::async_mutex session_mtx;
    std::unordered_map<std::string, session_info> sessions;
    std::unordered_map<std::string, std::size_t>  sessions_per_ip;  // O(1) IP 会话计数

    // --- 限流域（rate_limit_mtx 保护）---
    cn::async_mutex rate_limit_mtx;
    std::unordered_map<std::string, token_bucket> rate_limits;

    // --- 任务域（task_mtx 保护）---
    cn::async_mutex task_mtx;
    std::unordered_map<std::string, task_record> tasks;
    std::uint64_t next_task_id = 1;
};

/// 全局共享状态（server 和 handler 都通过指针引用它）
static shared_state* g_state = nullptr;

// =============================================================================
// 工具函数
// =============================================================================

/// CSPRNG 安全令牌（hex 编码）
/// MSVC 的 std::random_device 底层使用 BCryptGenRandom，密码学安全
/// GCC/Clang 使用 /dev/urandom，同样安全
auto generate_secure_token(std::size_t bytes = 32) -> std::string {
    static thread_local std::random_device rd;
    static constexpr char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i) {
        auto byte = static_cast<std::uint8_t>(rd() & 0xFF);
        token.push_back(hex[(byte >> 4) & 0x0F]);
        token.push_back(hex[byte & 0x0F]);
    }
    return token;
}

/// 解析客户端真实 IP（支持反向代理场景）
/// 优先级：X-Forwarded-For（最左 = 原始客户端） > X-Real-IP > 回退 "unknown"
auto resolve_client_ip(http::request_context& ctx) -> std::string {
    // X-Forwarded-For: client, proxy1, proxy2
    if (auto xff = ctx.get_header("X-Forwarded-For"); !xff.empty()) {
        auto comma = xff.find(',');
        auto ip = (comma != std::string_view::npos) ? xff.substr(0, comma) : xff;
        while (!ip.empty() && ip.front() == ' ') ip.remove_prefix(1);
        while (!ip.empty() && ip.back() == ' ')  ip.remove_suffix(1);
        if (!ip.empty()) return std::string(ip);
    }
    if (auto xri = ctx.get_header("X-Real-IP"); !xri.empty())
        return std::string(xri);
    return "unknown";
}

// =============================================================================
// 会话 GC — 定期扫描清理过期会话 + 限流条目
// =============================================================================

/// 后台 GC 协程：周期性清理过期 / 已踢会话，回收不活跃的限流桶
auto session_gc(cn::io_context& io) -> cn::task<void> {
    constexpr std::size_t batch_size = 128;

    while (true) {
        co_await cn::async_sleep(io, g_state->config.session_gc_interval);

        auto now = std::chrono::steady_clock::now();
        auto ttl = g_state->config.session_ttl;

        // 清理会话（session_mtx）— 分批释放锁
        std::size_t swept = 0;
        {
            co_await g_state->session_mtx.lock();
            cn::async_lock_guard guard(g_state->session_mtx, std::adopt_lock);

            std::size_t count = 0;
            for (auto it = g_state->sessions.begin(); it != g_state->sessions.end(); ) {
                auto& s = it->second;
                bool remove = false;

                if (s.status == session_status::kicked) {
                    remove = true;
                } else if (s.status == session_status::active &&
                           (now - s.last_activity) > ttl) {
                    s.status = session_status::expired;
                    remove = true;
                    g_state->stats.sessions_expired.fetch_add(1, std::memory_order_relaxed);
                    g_state->stats.active_sessions.fetch_sub(1, std::memory_order_relaxed);
                }

                if (remove) {
                    // 维护 sessions_per_ip
                    auto& cnt = g_state->sessions_per_ip[s.client_ip];
                    if (cnt > 0) --cnt;
                    if (cnt == 0) g_state->sessions_per_ip.erase(s.client_ip);

                    it = g_state->sessions.erase(it);
                    ++swept;
                } else {
                    ++it;
                }

                // 分批释放锁，避免长时间阻塞其他协程
                if (++count % batch_size == 0) {
                    guard.~async_lock_guard();
                    co_await cn::async_sleep(io, std::chrono::milliseconds{0});
                    co_await g_state->session_mtx.lock();
                    new (&guard) cn::async_lock_guard(g_state->session_mtx, std::adopt_lock);
                    now = std::chrono::steady_clock::now();  // 刷新时间
                }
            }
        }

        // 回收不活跃的限流桶（rate_limit_mtx）
        {
            co_await g_state->rate_limit_mtx.lock();
            cn::async_lock_guard guard(g_state->rate_limit_mtx, std::adopt_lock);

            auto rl_ttl = g_state->config.rate_limit_entry_ttl;
            std::size_t erased = 0;
            for (auto it = g_state->rate_limits.begin(); it != g_state->rate_limits.end(); ) {
                if ((now - it->second.last_refill) > rl_ttl) {
                    it = g_state->rate_limits.erase(it);
                    ++erased;
                } else {
                    ++it;
                }
            }
            if (erased > 0)
                g_state->stats.rate_limit_buckets.fetch_sub(erased, std::memory_order_relaxed);
        }

        if (swept > 0)
            std::println("  [GC] swept {} sessions, {} rate-limit buckets active",
                         swept, g_state->rate_limits.size());
    }
}

// =============================================================================
// 任务 worker — 后台消费者协程（channel 的核心使用场景）
// =============================================================================

/// 从 channel 持续接收任务，模拟耗时处理，更新 shared_state 中的任务状态
/// 典型生产者-消费者：HTTP handler 提交任务（生产），worker 异步执行（消费）
auto task_worker(cn::io_context& io, cn::channel<task_job>& ch) -> cn::task<void> {
    std::println("  [task-worker] started, waiting for jobs...");
    int count = 0;

    while (true) {
        // 1. 阻塞等待 channel 中的新任务
        auto job = co_await ch.receive();
        if (!job) break;  // channel closed → 优雅退出

        ++count;
        std::println("  [task-worker] #{} picked up task={} type={} params={}",
            count, job->task_id, to_string(job->type), job->params);

        // 2. 更新状态为 processing（task_mtx）
        {
            co_await g_state->task_mtx.lock();
            cn::async_lock_guard guard(g_state->task_mtx, std::adopt_lock);
            auto it = g_state->tasks.find(job->task_id);
            if (it != g_state->tasks.end())
                it->second.status = task_status::processing;
        }

        // 3. 模拟耗时处理（不同任务类型耗时不同）
        auto process_time = std::chrono::milliseconds{200};
        switch (job->type) {
            case task_type::generate_report: process_time = std::chrono::milliseconds{500}; break;
            case task_type::export_data:     process_time = std::chrono::milliseconds{300}; break;
            case task_type::send_email:      process_time = std::chrono::milliseconds{100}; break;
        }
        co_await cn::async_sleep(io, process_time);

        // 4. 生成结果，更新状态为 done
        auto result = std::format("completed in {}ms, output=/{}_result.csv",
            process_time.count(), job->task_id);

        {
            co_await g_state->task_mtx.lock();
            cn::async_lock_guard guard(g_state->task_mtx, std::adopt_lock);
            auto it = g_state->tasks.find(job->task_id);
            if (it != g_state->tasks.end()) {
                it->second.status = task_status::done;
                it->second.result = std::move(result);
            }
        }

        std::println("  [task-worker] #{} task={} done", count, job->task_id);
    }

    std::println("  [task-worker] channel closed, total {} jobs processed", count);
}

// =============================================================================
// 中间件 1：日志 + 分类统计
// =============================================================================

auto stats_middleware() -> http::middleware_fn {
    return [](http::request_context& ctx, http::next_fn next) -> cn::task<void> {
        // atomic — 无锁
        g_state->stats.total_requests.fetch_add(1, std::memory_order_relaxed);

        std::println("  [MW:stats] {} {}", ctx.method(), ctx.uri());
        co_await next();

        // 按状态码分类统计 — atomic 无锁
        auto status = ctx.resp().status_code();
        if (status >= 200 && status < 400)
            g_state->stats.total_success.fetch_add(1, std::memory_order_relaxed);
        else if (status >= 400 && status < 500)
            g_state->stats.total_client_err.fetch_add(1, std::memory_order_relaxed);
        else if (status >= 500)
            g_state->stats.total_server_err.fetch_add(1, std::memory_order_relaxed);

        std::println("  [MW:stats] → {}", status);
    };
}

// =============================================================================
// 中间件 2：IP 限流（Token Bucket）
// =============================================================================

auto rate_limit_middleware() -> http::middleware_fn {
    return [](http::request_context& ctx, http::next_fn next) -> cn::task<void> {
        auto client_ip = resolve_client_ip(ctx);

        bool allowed = false;
        double retry_after = 0;
        {
            co_await g_state->rate_limit_mtx.lock();
            cn::async_lock_guard guard(g_state->rate_limit_mtx, std::adopt_lock);

            auto now = std::chrono::steady_clock::now();
            auto& cfg = g_state->config;
            auto [it, inserted] = g_state->rate_limits.try_emplace(client_ip,
                token_bucket{cfg.rate_limit_burst, now});
            if (inserted)
                g_state->stats.rate_limit_buckets.fetch_add(1, std::memory_order_relaxed);

            allowed = it->second.try_consume(cfg.rate_limit_rate, cfg.rate_limit_burst, now);
            if (!allowed) {
                retry_after = it->second.retry_after(cfg.rate_limit_rate);
                g_state->stats.total_rejected.fetch_add(1, std::memory_order_relaxed);
            }
        }

        if (!allowed) {
            auto ra = static_cast<int>(std::ceil(retry_after));
            std::println("  [MW:rate] REJECTED {} (IP: {}, retry_after={}s)",
                         ctx.uri(), client_ip, ra);
            ctx.resp().set_header("Retry-After", std::format("{}", ra));
            ctx.json(http::status::too_many_requests, std::format(
                R"({{"error":"rate limit exceeded","retry_after_seconds":{}}})", ra));
            co_return;
        }

        co_await next();
    };
}

// =============================================================================
// 路由 handler：POST /login — 模拟登录，创建会话
// =============================================================================

auto handle_login(http::request_context& ctx) -> cn::task<void> {
    auto client_ip = resolve_client_ip(ctx);
    auto user_agent = std::string(ctx.get_header("User-Agent"));

    std::string token;
    {
        co_await g_state->session_mtx.lock();
        cn::async_lock_guard guard(g_state->session_mtx, std::adopt_lock);

        // O(1) IP 会话数检查
        auto ip_it = g_state->sessions_per_ip.find(client_ip);
        std::size_t ip_sessions = (ip_it != g_state->sessions_per_ip.end()) ? ip_it->second : 0;

        if (ip_sessions >= g_state->config.max_sessions_per_ip) {
            ctx.json(http::status::too_many_requests, std::format(
                R"({{"error":"too many sessions from this IP","limit":{}}})",
                g_state->config.max_sessions_per_ip));
            co_return;
        }

        auto now = std::chrono::steady_clock::now();
        token = generate_secure_token();
        g_state->sessions[token] = session_info{
            .token         = token,
            .client_ip     = client_ip,
            .user_agent    = std::move(user_agent),
            .created_at    = now,
            .last_activity = now,
            .status        = session_status::active,
        };
        g_state->sessions_per_ip[client_ip]++;
        g_state->stats.sessions_created.fetch_add(1, std::memory_order_relaxed);
        g_state->stats.active_sessions.fetch_add(1, std::memory_order_relaxed);
    }

    // 日志仅输出 token 前 16 字符（安全考量）
    auto short_tok = token.substr(0, 16) + "...";
    std::println("  [handler] /login → token={} ip={}", short_tok, client_ip);
    ctx.json(http::status::ok,
        std::format(R"({{"token":"{}","message":"login success"}})", token));
    co_return;
}

// =============================================================================
// 路由 handler：GET /api/data — 需要有效 token，被踢下线的无法访问
// =============================================================================

auto handle_data(http::request_context& ctx) -> cn::task<void> {
    auto token = std::string(ctx.get_header("Authorization"));
    if (token.empty()) {
        ctx.json(http::status::unauthorized,
            R"({"error":"missing Authorization header"})");
        co_return;
    }

    {
        co_await g_state->session_mtx.lock();
        cn::async_lock_guard guard(g_state->session_mtx, std::adopt_lock);

        auto it = g_state->sessions.find(token);
        if (it == g_state->sessions.end()) {
            ctx.json(http::status::unauthorized,
                R"({"error":"invalid or expired token"})");
            co_return;
        }

        auto& sess = it->second;
        auto now = std::chrono::steady_clock::now();

        // 检查会话状态
        if (sess.status == session_status::kicked) {
            ctx.json(http::status::forbidden,
                R"({"error":"session terminated by administrator"})");
            co_return;
        }
        if (sess.status == session_status::expired ||
            (now - sess.last_activity) > g_state->config.session_ttl) {
            sess.status = session_status::expired;
            g_state->stats.active_sessions.fetch_sub(1, std::memory_order_relaxed);
            ctx.json(http::status::unauthorized,
                R"({"error":"session expired, please login again"})");
            co_return;
        }

        // 刷新活跃时间
        sess.last_activity = now;
    }

    ctx.json(http::status::ok,
        std::format(R"({{"data":"secret payload","token":"{}"}})", token));
    co_return;
}

// =============================================================================
// 路由 handler：POST /admin/kick/:token — 踢人下线
// =============================================================================

auto handle_kick(http::request_context& ctx) -> cn::task<void> {
    auto target_token = std::string(ctx.param("token"));

    bool found = false;
    std::string kicked_ip;
    {
        co_await g_state->session_mtx.lock();
        cn::async_lock_guard guard(g_state->session_mtx, std::adopt_lock);

        auto it = g_state->sessions.find(target_token);
        if (it != g_state->sessions.end() &&
            it->second.status == session_status::active) {
            it->second.status = session_status::kicked;
            kicked_ip = it->second.client_ip;
            g_state->stats.total_kicked.fetch_add(1, std::memory_order_relaxed);
            g_state->stats.active_sessions.fetch_sub(1, std::memory_order_relaxed);
            found = true;
        }
    }

    if (found) {
        auto short_tok = target_token.substr(0, 16) + "...";
        std::println("  [handler] /admin/kick → kicked token={} ip={}",
                     short_tok, kicked_ip);
        ctx.json(http::status::ok,
            std::format(R"({{"kicked":"{}","message":"session terminated"}})",
                        target_token));
    } else {
        ctx.json(http::status::not_found,
            std::format(R"({{"error":"active session not found","token":"{}"}})",
                        target_token));
    }
    co_return;
}

// =============================================================================
// 路由 handler：GET /admin/stats — 查看统计信息
// =============================================================================

auto handle_stats(http::request_context& ctx) -> cn::task<void> {
    // 全 atomic 读取 — 无锁
    auto& s = g_state->stats;
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - s.start_time).count();

    auto json = std::format(
        R"({{"uptime_seconds":{},"total_requests":{},"total_success":{},"total_client_errors":{},"total_server_errors":{},"total_rejected":{},"total_kicked":{},"sessions_created":{},"sessions_expired":{},"active_sessions":{},"rate_limit_buckets":{}}})",
        uptime,
        s.total_requests.load(std::memory_order_relaxed),
        s.total_success.load(std::memory_order_relaxed),
        s.total_client_err.load(std::memory_order_relaxed),
        s.total_server_err.load(std::memory_order_relaxed),
        s.total_rejected.load(std::memory_order_relaxed),
        s.total_kicked.load(std::memory_order_relaxed),
        s.sessions_created.load(std::memory_order_relaxed),
        s.sessions_expired.load(std::memory_order_relaxed),
        s.active_sessions.load(std::memory_order_relaxed),
        s.rate_limit_buckets.load(std::memory_order_relaxed));

    ctx.json(http::status::ok, json);
    co_return;
}

// =============================================================================
// 路由 handler：POST /api/task — 提交异步任务（生产者 → channel）
// =============================================================================

auto handle_submit_task(http::request_context& ctx) -> cn::task<void> {
    auto token = std::string(ctx.get_header("Authorization"));
    if (token.empty()) {
        ctx.json(http::status::unauthorized,
            R"({"error":"missing Authorization header"})");
        co_return;
    }

    // 从 query string 解析任务类型：/api/task?type=generate_report&params=month=2026-01
    auto qs = std::string(ctx.query_string());
    std::string type_str, params_str;

    // 简单解析 query string
    auto parse_qs = [](std::string_view qs, std::string_view key) -> std::string {
        auto search = std::string(key) + "=";
        auto pos = qs.find(search);
        if (pos == std::string_view::npos) return "";
        auto start = pos + search.size();
        auto end = qs.find('&', start);
        return std::string(end != std::string_view::npos
            ? qs.substr(start, end - start) : qs.substr(start));
    };

    type_str = parse_qs(qs, "type");
    params_str = parse_qs(qs, "params");

    auto tt = from_string<task_type>(type_str);
    if (!tt) {
        ctx.json(http::status::bad_request,
            R"({"error":"invalid task type, use: generate_report, export_data, send_email"})");
        co_return;
    }

    // 创建任务记录并立即返回 task_id（非阻塞）
    std::string task_id;
    {
        co_await g_state->task_mtx.lock();
        cn::async_lock_guard guard(g_state->task_mtx, std::adopt_lock);

        task_id = std::format("task_{}", g_state->next_task_id++);
        g_state->tasks[task_id] = task_record{
            .task_id      = task_id,
            .type         = *tt,
            .params       = params_str,
            .status       = task_status::pending,
            .result       = {},
            .submitted_by = token,
        };
    }

    // 投递到 channel → 后台 worker 会异步消费处理
    co_await g_task_ch->send(task_job{
        .task_id = task_id,
        .type    = *tt,
        .params  = params_str,
    });

    std::println("  [handler] /api/task → submitted {} type={}", task_id, type_str);
    ctx.json(http::status::accepted, std::format(
        R"({{"task_id":"{}","type":"{}","status":"pending","message":"task submitted, poll GET /api/task/{}"}})",
        task_id, type_str, task_id));
    co_return;
}

// =============================================================================
// 路由 handler：GET /api/task/:id — 查询任务状态（轮询）
// =============================================================================

auto handle_task_status(http::request_context& ctx) -> cn::task<void> {
    auto task_id = std::string(ctx.param("id"));

    {
        co_await g_state->task_mtx.lock();
        cn::async_lock_guard guard(g_state->task_mtx, std::adopt_lock);

        auto it = g_state->tasks.find(task_id);
        if (it == g_state->tasks.end()) {
            ctx.json(http::status::not_found, std::format(
                R"({{"error":"task not found","task_id":"{}"}})", task_id));
            co_return;
        }

        auto& t = it->second;
        auto json = std::format(
            R"({{"task_id":"{}","type":"{}","status":"{}","params":"{}","result":"{}","submitted_by":"{}"}})",
            t.task_id, to_string(t.type), to_string(t.status),
            t.params, t.result, t.submitted_by);
        ctx.json(http::status::ok, json);
    }
    co_return;
}

// =============================================================================
// 路由 handler：GET /admin/sessions — 查看所有会话
// =============================================================================

auto handle_sessions(http::request_context& ctx) -> cn::task<void> {
    std::string json;
    {
        co_await g_state->session_mtx.lock();
        cn::async_lock_guard guard(g_state->session_mtx, std::adopt_lock);

        auto now = std::chrono::steady_clock::now();
        json = "[";
        bool first = true;
        for (auto& [tok, sess] : g_state->sessions) {
            if (!first) json += ",";

            auto idle_secs = std::chrono::duration_cast<std::chrono::seconds>(
                now - sess.last_activity).count();
            auto age_secs = std::chrono::duration_cast<std::chrono::seconds>(
                now - sess.created_at).count();

            // token 仅暴露前 16 字符（安全考量）
            auto short_tok = tok.substr(0, 16) + "...";
            json += std::format(
                R"({{"token":"{}","ip":"{}","user_agent":"{}","status":"{}","idle_seconds":{},"age_seconds":{}}})",
                short_tok, sess.client_ip, sess.user_agent,
                to_string(sess.status), idle_secs, age_secs);
            first = false;
        }
        json += "]";
    }

    ctx.json(http::status::ok, json);
    co_return;
}

// =============================================================================
// 客户端：发送一个 HTTP 请求并打印响应
// =============================================================================

auto send_request(cn::io_context& ctx, http::http_method method,
                  std::string_view path,
                  std::vector<std::pair<std::string, std::string>> extra_headers = {},
                  std::string_view body = {})
    -> cn::task<std::string>  // 返回响应 body
{
    auto sock_r = cn::socket::create(cn::address_family::ipv4,
                                     cn::socket_type::stream);
    if (!sock_r) co_return "";
    auto sock = std::move(*sock_r);

    auto ep = cn::endpoint{cn::ipv4_address::loopback(), PORT};
    auto cr = co_await cn::async_connect(ctx, sock, ep);
    if (!cr) { std::println("    connect failed"); co_return ""; }

    http::request req(method, path);
    req.set_header("Host", std::format("127.0.0.1:{}", PORT));
    req.set_header("Connection", "close");
    for (auto& [k, v] : extra_headers) {
        req.set_header(k, v);
    }
    if (!body.empty()) {
        req.set_body(std::string(body));
    }

    auto req_data = req.serialize();
    auto wr = co_await cn::async_write(ctx, sock,
        cn::const_buffer{req_data.data(), req_data.size()});
    if (!wr) { sock.close(); co_return ""; }

    // 读响应
    http::response_parser rp;
    std::array<std::byte, 8192> buf{};
    while (!rp.ready()) {
        auto rd = co_await cn::async_read(ctx, sock, cn::buffer(buf));
        if (!rd || *rd == 0) break;
        auto c = rp.consume(reinterpret_cast<const char*>(buf.data()), *rd);
        if (!c) break;
    }

    std::string resp_body;
    if (rp.ready()) {
        std::println("    {} {} → {} {}",
                     http::method_to_string(method), path,
                     rp.status_code(), rp.status_message());
        resp_body = rp.body();
        if (!resp_body.empty())
            std::println("    Body: {}", resp_body);
    }
    sock.close();
    co_return resp_body;
}

// =============================================================================
// 辅助：从 JSON 中提取 token 值（简单字符串查找）
// =============================================================================

auto extract_token(const std::string& json) -> std::string {
    auto pos = json.find("\"token\":\"");
    if (pos == std::string::npos) return "";
    pos += 9;  // skip "token":"
    auto end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

// =============================================================================
// 客户端协程：依次验证各功能
// =============================================================================

auto run_client(cn::io_context& ctx, http::server& srv) -> cn::task<void> {
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{50});

    std::println("\n========== Client: Testing All Features ==========\n");

    // -------------------------------------------------------
    // 1. 登录获取 token
    // -------------------------------------------------------
    std::println("  [1] POST /login (user A)");
    auto resp1 = co_await send_request(ctx, http::http_method::POST, "/login",
        {{"X-Forwarded-For", "192.168.1.100"}, {"User-Agent", "TestClient/1.0"}});
    auto token_a = extract_token(resp1);
    std::println("    → token_a = {}...", token_a.substr(0, 16));

    std::println("\n  [2] POST /login (user B)");
    auto resp2 = co_await send_request(ctx, http::http_method::POST, "/login",
        {{"X-Forwarded-For", "192.168.1.200"}, {"User-Agent", "TestClient/2.0"}});
    auto token_b = extract_token(resp2);
    std::println("    → token_b = {}...", token_b.substr(0, 16));

    // -------------------------------------------------------
    // 2. 用 token 访问受保护数据
    // -------------------------------------------------------
    std::println("\n  [3] GET /api/data (user A, valid token)");
    co_await send_request(ctx, http::http_method::GET, "/api/data",
        {{"Authorization", token_a}});

    std::println("\n  [4] GET /api/data (no token → 401)");
    co_await send_request(ctx, http::http_method::GET, "/api/data");

    std::println("\n  [5] GET /api/data (invalid token → 401)");
    co_await send_request(ctx, http::http_method::GET, "/api/data",
        {{"Authorization", "fake_token"}});

    // -------------------------------------------------------
    // 3. 查看当前会话列表
    // -------------------------------------------------------
    std::println("\n  [6] GET /admin/sessions");
    co_await send_request(ctx, http::http_method::GET, "/admin/sessions");

    // -------------------------------------------------------
    // 4. 踢人下线
    // -------------------------------------------------------
    std::println("\n  [7] POST /admin/kick/{} (kick user A)", token_a);
    co_await send_request(ctx, http::http_method::POST,
        std::format("/admin/kick/{}", token_a));

    // 被踢后访问受保护资源 → 403
    std::println("\n  [8] GET /api/data (user A after kicked → 403)");
    co_await send_request(ctx, http::http_method::GET, "/api/data",
        {{"Authorization", token_a}});

    // user B 仍可访问
    std::println("\n  [9] GET /api/data (user B still valid)");
    co_await send_request(ctx, http::http_method::GET, "/api/data",
        {{"Authorization", token_b}});

    // -------------------------------------------------------
    // 5. 限流测试：同一 IP 快速发 6 次请求
    // -------------------------------------------------------
    std::println("\n  [10] Rate limit test: 6 rapid requests from same IP");
    for (int i = 1; i <= 6; ++i) {
        std::println("\n    --- request #{} ---", i);
        co_await send_request(ctx, http::http_method::GET, "/api/data",
            {{"Authorization", token_b}, {"X-Forwarded-For", "10.0.0.99"}});
    }

    // -------------------------------------------------------
    // 6. 异步任务队列（channel 生产-消费）
    // -------------------------------------------------------
    std::println("\n  [11] POST /api/task — submit report generation");
    auto task_resp1 = co_await send_request(ctx, http::http_method::POST,
        "/api/task?type=generate_report&params=month=2026-01",
        {{"Authorization", token_b}});

    std::println("\n  [12] POST /api/task — submit data export");
    auto task_resp2 = co_await send_request(ctx, http::http_method::POST,
        "/api/task?type=export_data&params=user_id=42",
        {{"Authorization", token_b}});

    std::println("\n  [13] POST /api/task — submit email");
    co_await send_request(ctx, http::http_method::POST,
        "/api/task?type=send_email&params=to=test@example.com",
        {{"Authorization", token_b}});

    // 立即查询 → 应该是 pending 或 processing
    // 从响应中提取 task_id
    auto extract_task_id = [](const std::string& json) -> std::string {
        auto pos = json.find("\"task_id\":\"");
        if (pos == std::string::npos) return "";
        pos += 11;
        auto end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    };

    auto tid1 = extract_task_id(task_resp1);
    auto tid2 = extract_task_id(task_resp2);

    std::println("\n  [14] GET /api/task/{} — poll immediately (should be pending/processing)", tid1);
    co_await send_request(ctx, http::http_method::GET,
        std::format("/api/task/{}", tid1));

    // 等待 worker 处理完成
    std::println("\n  ... waiting 800ms for worker to finish ...");
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{800});

    // 再次轮询 → 应该是 done
    std::println("\n  [15] GET /api/task/{} — poll again (should be done)", tid1);
    co_await send_request(ctx, http::http_method::GET,
        std::format("/api/task/{}", tid1));

    std::println("\n  [16] GET /api/task/{} — poll task 2 (should be done)", tid2);
    co_await send_request(ctx, http::http_method::GET,
        std::format("/api/task/{}", tid2));

    // -------------------------------------------------------
    // 7. 查看统计信息
    // -------------------------------------------------------
    std::println("\n  [17] GET /admin/stats");
    co_await send_request(ctx, http::http_method::GET, "/admin/stats");

    std::println("\n========== Client: All Tests Done ==========\n");

    // 关闭任务 channel，让 worker 优雅退出
    g_task_ch->close();
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{50});

    srv.stop();
    ctx.stop();
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::println("=== cnetmod: High-level HTTP Plus Demo ===");
    std::println("  Features: kick-offline, stats, rate-limit, shared-state, "
                 "async_mutex, channel task-queue\n");

    cn::net_init net;
    auto ctx = cn::make_io_context();

    // 初始化共享状态（生产级配置）
    shared_state state;
    state.config.rate_limit_rate  = 2.0;                       // 每秒 2 个令牌
    state.config.rate_limit_burst = 5.0;                       // 突发容量 5
    state.config.session_ttl = std::chrono::seconds{60};       // demo: 60s 空闲超时
    state.config.session_gc_interval = std::chrono::seconds{5}; // demo: 5s GC 间隔
    state.config.max_sessions_per_ip = 5;
    state.config.rate_limit_entry_ttl = std::chrono::seconds{60};
    g_state = &state;

    // 初始化任务 channel（容量 16，带背压：满时提交方会挂起等待）
    cn::channel<task_job> task_ch(16);
    g_task_ch = &task_ch;

    // 构建路由
    http::router router;

    // GET / — 欢迎页
    router.get("/", [](http::request_context& ctx) -> cn::task<void> {
        ctx.html(http::status::ok,
            "<h1>cnetmod HTTP Plus Demo</h1>"
            "<p>Features: kick, stats, rate-limit, shared-state, async_mutex</p>"
            "<ul>"
            "<li>POST /login</li>"
            "<li>GET /api/data (Authorization: token)</li>"
            "<li>POST /admin/kick/:token</li>"
            "<li>GET /admin/stats</li>"
            "<li>GET /admin/sessions</li>"
            "<li>POST /api/task?type=...&params=...</li>"
            "<li>GET /api/task/:id</li>"
            "</ul>");
        co_return;
    });

    // POST /login — 模拟登录
    router.post("/login", handle_login);

    // GET /api/data — 受保护数据
    router.get("/api/data", handle_data);

    // POST /admin/kick/:token — 踢人下线
    router.post("/admin/kick/:token", handle_kick);

    // GET /admin/stats — 统计信息
    router.get("/admin/stats", handle_stats);

    // GET /admin/sessions — 会话列表
    router.get("/admin/sessions", handle_sessions);

    // POST /api/task — 提交异步任务（→ channel → worker）
    router.post("/api/task", handle_submit_task);

    // GET /api/task/:id — 查询任务状态（轮询）
    router.get("/api/task/:id", handle_task_status);

    // 构建服务器
    http::server srv(*ctx);
    auto listen_r = srv.listen("127.0.0.1", PORT);
    if (!listen_r) {
        std::println("Listen failed: {}", listen_r.error().message());
        return 1;
    }

    // 注册中间件（洋葱模型：限流 → 统计 → handler）
    srv.use(rate_limit_middleware());
    srv.use(stats_middleware());
    srv.set_router(std::move(router));

    std::println("  Server listening on *********:{}", PORT);
    std::println("  Rate limit: {:.0f} req/s, burst={:.0f}",
                 state.config.rate_limit_rate, state.config.rate_limit_burst);
    std::println("  Session TTL: {}s, GC interval: {}s, max per IP: {}\n",
                 state.config.session_ttl.count(),
                 state.config.session_gc_interval.count(),
                 state.config.max_sessions_per_ip);

    // 启动后台协程
    cn::spawn(*ctx, session_gc(*ctx));         // 会话 GC
    cn::spawn(*ctx, task_worker(*ctx, task_ch)); // 任务 worker

    // 启动服务器和客户端
    cn::spawn(*ctx, srv.run());
    cn::spawn(*ctx, run_client(*ctx, srv));

    ctx->run();
    std::println("Done.");
    return 0;
}
