/**
 * @brief Lifecycle contract implemented by application-managed infrastructure.
 */
export module cnetmod.application.managed_service;

import std;
import cnetmod.application.recovery_policy;
import cnetmod.application.task_supervisor;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.io.io_context;
import cnetmod.observability;

namespace cnetmod::application {

export enum class service_requirement
{
    required,
    optional
};

export enum class service_health
{
    starting,
    up,
    degraded,
    down,
    stopping,
    stopped
};

/**
 * @brief Stable identity composed of integration type and instance name.
 */
export struct service_key
{
    std::string name;
    std::string instance{"default"};

    [[nodiscard]] auto canonical_name() const -> std::string
    {
        return std::format("{}:{}", name, instance);
    }

    auto operator==(const service_key&) const -> bool = default;
};

export struct service_key_hash
{
    [[nodiscard]] auto operator()(const service_key& key) const noexcept
        -> std::size_t
    {
        const auto first = std::hash<std::string>{}(key.name);
        const auto second = std::hash<std::string>{}(key.instance);
        return first ^ (second + 0x9e3779b9U + (first << 6U) + (first >> 2U));
    }
};

/**
 * @brief Result of one asynchronous component health probe.
 */
export struct health_report
{
    service_health status = service_health::starting;
    std::string message{"starting"};
    std::error_code error;
    std::chrono::system_clock::time_point checked_at =
        std::chrono::system_clock::now();
};

/**
 * @brief Runtime capabilities supplied to managed service operations.
 */
export struct service_context
{
    io_context& io;
    observability::telemetry_hub& telemetry;
    task_supervisor& supervisor;
    cancel_token& cancellation;
    deadline operation_deadline;
};

/**
 * @brief Uniform asynchronous lifecycle contract for application components.
 *
 * Implementations must honor cancellation and operation deadlines and must
 * register every critical long-running coroutine with the task supervisor.
 */
export class managed_service
{
public:
    virtual ~managed_service() = default;

    /**
     * @brief Returns the globally unique service key.
     */
    [[nodiscard]] virtual auto key() const -> service_key = 0;

    /**
     * @brief Returns services that must start before this service.
     */
    [[nodiscard]] virtual auto dependencies() const
        -> std::vector<service_key>
    {
        return {};
    }

    /**
     * @brief Returns whether failure is fatal or degrading.
     */
    [[nodiscard]] virtual auto requirement() const noexcept
        -> service_requirement = 0;

    /**
     * @brief Returns the bounded runtime recovery policy.
     */
    [[nodiscard]] virtual auto recovery() const noexcept -> recovery_policy
    {
        return {};
    }

    /**
     * @brief Acquires resources and establishes initial availability.
     */
    virtual auto start(service_context& context)
        -> task<std::expected<void, std::error_code>> = 0;

    /**
     * @brief Reports incomplete cleanup after an unsuccessful lifecycle operation.
     *
     * Called on the lifecycle executor after the operation settles. Override
     * when failed startup can retain resources. Return false after successful
     * stop; ordinary running resources are tracked by successful startup.
     */
    [[nodiscard]] virtual auto cleanup_required() const noexcept -> bool;

    /**
     * @brief Reports resources that must participate in ordered shutdown.
     *
     * The default delegates to cleanup_required(). Recoverable background
     * resources may require shutdown without requiring cleanup before retry.
     * Called after lifecycle operations settle on their owning executor.
     */
    [[nodiscard]] virtual auto shutdown_required() const noexcept -> bool;

    /**
     * @brief Cancels work and releases resources within the supplied deadline.
     */
    virtual auto stop(service_context& context)
        -> task<std::expected<void, std::error_code>> = 0;

    /**
     * @brief Performs one asynchronous availability probe.
     */
    virtual auto probe(service_context& context) -> task<health_report> = 0;
};

} // namespace cnetmod::application
