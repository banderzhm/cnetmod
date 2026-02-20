/**
 * @file ip_firewall.cppm
 * @brief IP 自适应防火墙 — 频率检测 + 自动封禁，基于 cache_store
 *
 * 自动追踪 IP 违规行为（高频请求、大量 4xx/5xx、路径扫描等），
 * 超阈值后自动将 IP 加入黑名单，封禁期到后自动解除。
 * 底层使用 cache_store 接口存储状态，可无缝切换 memory_cache / redis_cache。
 *
 * 使用示例:
 *   import cnetmod.middleware.ip_firewall;
 *   import cnetmod.middleware.cache;
 *
 *   cnetmod::cache::memory_cache store({.max_entries = 50000});
 *
 *   cnetmod::ip_firewall fw(store, {
 *       .max_violations     = 10,               // 窗口内最多 10 次违规
 *       .violation_window   = std::chrono::minutes{5},  // 5 分钟统计窗口
 *       .ban_duration       = std::chrono::hours{1},    // 封禁 1 小时
 *       .track_4xx          = true,              // 4xx 响应计入违规
 *       .track_5xx          = false,             // 5xx 不计入（服务端问题）
 *       .track_rate_limit   = true,              // 429 计入违规
 *   });
 *
 *   // 封禁检查（放在中间件链最前面）
 *   svr.use(fw.check_middleware());
 *
 *   // 违规追踪（放在中间件链最后面，handler 执行完后统计）
 *   svr.use(fw.track_middleware());
 *
 *   // 手动举报违规（如配合 rate_limiter 回调）
 *   co_await fw.report_violation(client_ip);
 *
 *   // 手动封禁/解封
 *   co_await fw.ban(ip);
 *   co_await fw.unban(ip);
 *
 *   // 查询状态
 *   bool banned = co_await fw.is_banned(ip);
 *   int count = co_await fw.violation_count(ip);
 *
 *   // handler: 暴露封禁状态 API (可选)
 *   router.get("/admin/firewall/:ip", cnetmod::firewall_status_handler(fw));
 *   router.post("/admin/firewall/:ip/ban", cnetmod::firewall_ban_handler(fw));
 *   router.del("/admin/firewall/:ip/ban", cnetmod::firewall_unban_handler(fw));
 */
export module cnetmod.middleware.ip_firewall;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.middleware.cache_store;  // 仅需抽象接口，避免引入 redis 等重量级依赖
// 注意: 不 import cnetmod.core.log，避免 MSVC C1605（对象文件超4GB）
// 改用 std::println(std::cerr, ...) 直接输出

namespace cnetmod {

// =============================================================================
// ip_firewall_options — 防火墙配置
// =============================================================================

export struct ip_firewall_options {
    /// 违规阈值: 窗口内累计达到此值则封禁
    int max_violations = 10;

    /// 统计窗口: 违规计数器的 TTL
    std::chrono::seconds violation_window{300};  // 5 分钟

    /// 封禁时长: 自动解封 TTL
    std::chrono::seconds ban_duration{3600};     // 1 小时

    /// 是否追踪 4xx 响应 (除 429 外)
    bool track_4xx = true;

    /// 是否追踪 5xx 响应
    bool track_5xx = false;

    /// 是否追踪 429 (Too Many Requests) — 配合 rate_limiter
    bool track_rate_limit = true;

    /// 特定路径的违规权重 (如扫描敏感路径加重处罚)
    /// key: 路径前缀, value: 违规权重 (默认每次 +1)
    std::vector<std::pair<std::string, int>> path_weights;

    /// 缓存 key 前缀
    std::string key_prefix = "fw:";

    /// 被封禁时的响应状态码
    int banned_status = http::status::forbidden;
};

// =============================================================================
// ip_firewall — 自适应 IP 防火墙
// =============================================================================

export class ip_firewall {
public:
    explicit ip_firewall(cache::cache_store& store,
                         ip_firewall_options opts = {}) noexcept
        : store_(store), opts_(std::move(opts)) {}

    // =========================================================================
    // check_middleware — 封禁检查 (放在中间件链头部)
    // =========================================================================

    auto check_middleware() -> http::middleware_fn {
        return [this](http::request_context& ctx,
                      http::next_fn next) -> task<void>
        {
            auto ip = http::resolve_client_ip(ctx);
            auto banned = co_await is_banned(ip);
            if (banned) {
                // 获取剩余封禁时间（尽力估算）
                std::println(std::cerr, "[firewall] blocked banned IP: {}", ip);
                ctx.resp().set_header("Connection", "close");
                ctx.json(opts_.banned_status, std::format(
                    R"({{"error":"ip banned","ip":"{}"}})", ip));
                co_return;
            }
            co_await next();
        };
    }

    // =========================================================================
    // track_middleware — 违规追踪 (放在中间件链尾部)
    // =========================================================================
    //
    // handler 执行完后检查响应状态码，满足条件则累加违规次数。
    // 超过阈值自动封禁。

    auto track_middleware() -> http::middleware_fn {
        return [this](http::request_context& ctx,
                      http::next_fn next) -> task<void>
        {
            co_await next();

            auto status = ctx.resp().status_code();
            int weight = 0;

            // 429 检测
            if (opts_.track_rate_limit && status == 429) {
                weight = 1;
            }
            // 4xx 检测 (排除 429，已单独处理)
            else if (opts_.track_4xx && status >= 400 && status < 500
                     && status != 429)
            {
                weight = 1;
            }
            // 5xx 检测
            else if (opts_.track_5xx && status >= 500) {
                weight = 1;
            }

            if (weight <= 0) co_return;

            // 检查路径权重
            auto path = ctx.path();
            for (auto& [prefix, w] : opts_.path_weights) {
                if (path.starts_with(prefix)) {
                    weight = w;
                    break;
                }
            }

            auto ip = http::resolve_client_ip(ctx);
            co_await add_violation(ip, weight);
        };
    }

    // =========================================================================
    // 编程式 API
    // =========================================================================

    /// 手动举报违规 (如从 rate_limiter 回调中使用)
    auto report_violation(std::string_view ip, int weight = 1) -> task<void> {
        co_await add_violation(std::string(ip), weight);
    }

    /// 手动封禁
    auto ban(std::string_view ip) -> task<void> {
        co_await ban(std::string(ip), opts_.ban_duration);
    }

    /// 手动封禁（自定义时长）
    auto ban(std::string_view ip, std::chrono::seconds duration) -> task<void> {
        auto key = ban_key(std::string(ip));
        co_await store_.set(key, "1", duration);
        std::println(std::cerr, "[firewall] banned IP: {} for {}s", ip, duration.count());
    }

    /// 手动解封
    auto unban(std::string_view ip) -> task<void> {
        auto key = ban_key(std::string(ip));
        co_await store_.del(key);
        // 同时清除违规计数
        auto vk = violation_key(std::string(ip));
        co_await store_.del(vk);
        std::println(std::cerr, "[firewall] unbanned IP: {}", ip);
    }

    /// 查询 IP 是否被封禁
    auto is_banned(std::string_view ip) -> task<bool> {
        auto key = ban_key(std::string(ip));
        co_return co_await store_.exists(key);
    }

    /// 查询 IP 当前违规次数
    auto violation_count(std::string_view ip) -> task<int> {
        auto key = violation_key(std::string(ip));
        auto val = co_await store_.get(key);
        if (!val) co_return 0;
        int count = 0;
        auto [ptr, ec] = std::from_chars(val->data(),
                                          val->data() + val->size(), count);
        co_return (ec == std::errc{}) ? count : 0;
    }

private:
    cache::cache_store& store_;
    ip_firewall_options opts_;

    // =========================================================================
    // cache key 生成
    // =========================================================================

    auto ban_key(const std::string& ip) const -> std::string {
        return opts_.key_prefix + "ban:" + ip;
    }

    auto violation_key(const std::string& ip) const -> std::string {
        return opts_.key_prefix + "v:" + ip;
    }

    // =========================================================================
    // 违规累加 + 自动封禁判定
    // =========================================================================

    auto add_violation(const std::string& ip, int weight) -> task<void> {
        // 已封禁的 IP 无需继续累加
        if (co_await is_banned(ip)) co_return;

        auto key = violation_key(ip);

        // 读取当前计数
        int count = 0;
        auto val = co_await store_.get(key);
        if (val) {
            auto [ptr, ec] = std::from_chars(val->data(),
                                              val->data() + val->size(), count);
            if (ec != std::errc{}) count = 0;
        }

        count += weight;

        // 写回（刷新 TTL = violation_window）
        co_await store_.set(key, std::to_string(count), opts_.violation_window);

        // debug 级别日志，仅在需要时启用
        // std::println(std::cerr, "[firewall] violation: ip={} count={}/{}", ip, count,
        //              opts_.max_violations);

        // 超阈值 → 封禁
        if (count >= opts_.max_violations) {
            co_await ban(ip, opts_.ban_duration);
            // 清除违规计数（封禁后重新开始）
            co_await store_.del(key);
        }
    }

};

} // namespace cnetmod

namespace cnetmod {

/// GET /admin/firewall/:ip — 查询 IP 封禁状态
export inline auto firewall_status_handler(ip_firewall& fw) -> http::handler_fn {
    return [&fw](http::request_context& ctx) -> task<void> {
        auto ip = ctx.param("ip");
        if (ip.empty()) {
            ctx.json(http::status::bad_request,
                     R"({"error":"missing ip param"})");
            co_return;
        }
        auto banned = co_await fw.is_banned(ip);
        auto count  = co_await fw.violation_count(ip);
        ctx.json(http::status::ok, std::format(
            R"({{"ip":"{}","banned":{},"violations":{}}})",
            ip, banned ? "true" : "false", count));
    };
}

/// POST /admin/firewall/:ip/ban — 手动封禁
export inline auto firewall_ban_handler(ip_firewall& fw) -> http::handler_fn {
    return [&fw](http::request_context& ctx) -> task<void> {
        auto ip = ctx.param("ip");
        if (ip.empty()) {
            ctx.json(http::status::bad_request,
                     R"({"error":"missing ip param"})");
            co_return;
        }
        co_await fw.ban(ip);
        ctx.json(http::status::ok, std::format(
            R"({{"ip":"{}","banned":true}})", ip));
    };
}

/// DELETE /admin/firewall/:ip/ban — 手动解封
export inline auto firewall_unban_handler(ip_firewall& fw) -> http::handler_fn {
    return [&fw](http::request_context& ctx) -> task<void> {
        auto ip = ctx.param("ip");
        if (ip.empty()) {
            ctx.json(http::status::bad_request,
                     R"({"error":"missing ip param"})");
            co_return;
        }
        co_await fw.unban(ip);
        ctx.json(http::status::ok, std::format(
            R"({{"ip":"{}","banned":false}})", ip));
    };
}

} // namespace cnetmod
