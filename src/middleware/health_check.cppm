/**
 * @file health_check.cppm
 * @brief K8s 风格健康检查端点 — /health (liveness) + /ready (readiness)
 *
 * 提供 handler 工厂函数，配合路由注册使用。
 * check_fn 返回 health_status 结构体，支持自定义检查逻辑（数据库连接、外部服务等）。
 *
 * 使用示例:
 *   import cnetmod.middleware.health_check;
 *
 *   // 简单存活检查（始终 OK）
 *   router.get("/health", health_check());
 *
 *   // 带自定义检查的就绪探针
 *   router.get("/ready", readiness_check([&db]() -> health_status {
 *       if (!db.is_connected()) return {false, "database disconnected"};
 *       return {true, "all systems operational"};
 *   }));
 */
export module cnetmod.middleware.health_check;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

// =============================================================================
// health_status — 健康检查结果
// =============================================================================

export struct health_status {
    bool ok = true;
    std::string message = "ok";
};

// =============================================================================
// health_check — 存活探针 (liveness probe)
// =============================================================================
//
// K8s livenessProbe: 返回 200 表示进程活着。
// 失败（503）时 K8s 会重启 Pod。

/// 创建简单存活检查 handler（始终返回 200）
export inline auto health_check() -> http::handler_fn
{
    return [](http::request_context& ctx) -> task<void> {
        ctx.json(http::status::ok,
            R"({"status":"UP","message":"ok"})");
        co_return;
    };
}

/// 创建带自定义检查逻辑的存活探针
export inline auto health_check(std::function<health_status()> check_fn)
    -> http::handler_fn
{
    return [check_fn = std::move(check_fn)]
           (http::request_context& ctx) -> task<void>
    {
        auto result = check_fn();
        auto status = result.ok ? http::status::ok
                                : http::status::service_unavailable;
        ctx.json(status, std::format(
            R"({{"status":"{}","message":"{}"}})",
            result.ok ? "UP" : "DOWN", result.message));
        co_return;
    };
}

// =============================================================================
// readiness_check — 就绪探针 (readiness probe)
// =============================================================================
//
// K8s readinessProbe: 返回 200 表示可以接收流量。
// 失败（503）时 K8s 从 Service 端点移除该 Pod（不再转发流量）。

/// 创建带自定义检查逻辑的就绪探针
export inline auto readiness_check(std::function<health_status()> check_fn)
    -> http::handler_fn
{
    return [check_fn = std::move(check_fn)]
           (http::request_context& ctx) -> task<void>
    {
        auto result = check_fn();
        auto status = result.ok ? http::status::ok
                                : http::status::service_unavailable;

        // 就绪探针返回更详细的信息
        ctx.json(status, std::format(
            R"({{"status":"{}","message":"{}","ready":{}}})",
            result.ok ? "UP" : "DOWN",
            result.message,
            result.ok ? "true" : "false"));
        co_return;
    };
}

/// 创建组合就绪探针（所有检查都通过才返回 200）
export inline auto readiness_check(
    std::vector<std::pair<std::string, std::function<health_status()>>> checks)
    -> http::handler_fn
{
    return [checks = std::move(checks)]
           (http::request_context& ctx) -> task<void>
    {
        bool all_ok = true;
        std::string details = "[";
        bool first = true;

        for (auto& [name, check_fn] : checks) {
            if (!first) details += ",";
            auto result = check_fn();
            if (!result.ok) all_ok = false;

            details += std::format(
                R"({{"name":"{}","status":"{}","message":"{}"}})",
                name, result.ok ? "UP" : "DOWN", result.message);
            first = false;
        }
        details += "]";

        auto status = all_ok ? http::status::ok
                             : http::status::service_unavailable;
        ctx.json(status, std::format(
            R"({{"status":"{}","checks":{}}})",
            all_ok ? "UP" : "DOWN", details));
        co_return;
    };
}

} // namespace cnetmod
