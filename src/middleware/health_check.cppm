/**
 * @file health_check.cppm
 * @brief K8s-style health check endpoints — /health (liveness) + /ready (readiness)
 *
 * Provides handler factory functions for use with route registration.
 * check_fn returns health_status struct, supports custom check logic (database connections, external services, etc.).
 *
 * Usage example:
 *   import cnetmod.middleware.health_check;
 *
 *   // Simple liveness check (always OK)
 *   router.get("/health", health_check());
 *
 *   // Readiness probe with custom check
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
// health_status — Health check result
// =============================================================================

export struct health_status {
    bool ok = true;
    std::string message = "ok";
};

// =============================================================================
// health_check — Liveness probe
// =============================================================================
//
// K8s livenessProbe: returns 200 to indicate process is alive.
// On failure (503), K8s will restart the Pod.

/// Create simple liveness check handler (always returns 200)
export inline auto health_check() -> http::handler_fn
{
    return [](http::request_context& ctx) -> task<void> {
        ctx.json(http::status::ok,
            R"({"status":"UP","message":"ok"})");
        co_return;
    };
}

/// Create liveness probe with custom check logic
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
// readiness_check — Readiness probe
// =============================================================================
//
// K8s readinessProbe: returns 200 to indicate ready to receive traffic.
// On failure (503), K8s removes the Pod from Service endpoints (no longer forwards traffic).

/// Create readiness probe with custom check logic
export inline auto readiness_check(std::function<health_status()> check_fn)
    -> http::handler_fn
{
    return [check_fn = std::move(check_fn)]
           (http::request_context& ctx) -> task<void>
    {
        auto result = check_fn();
        auto status = result.ok ? http::status::ok
                                : http::status::service_unavailable;

        // Readiness probe returns more detailed information
        ctx.json(status, std::format(
            R"({{"status":"{}","message":"{}","ready":{}}})",
            result.ok ? "UP" : "DOWN",
            result.message,
            result.ok ? "true" : "false"));
        co_return;
    };
}

/// Create combined readiness probe (returns 200 only if all checks pass)
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
