/**
 * @brief Cached, non-blocking application health and readiness registry.
 */
export module cnetmod.application.health_registry;

import std;
import cnetmod.application.managed_service;
import cnetmod.coro.task;

namespace cnetmod::application {

/**
 * @brief Probe scheduling and health-state hysteresis policy.
 */
export struct health_policy
{
    std::chrono::milliseconds interval{10000};
    std::chrono::milliseconds timeout{3000};
    std::size_t failures_before_down = 3;
    std::size_t successes_before_up = 2;
};

/**
 * @brief Cached health state for one named managed service.
 */
export struct service_health_snapshot
{
    service_key key;
    service_requirement requirement = service_requirement::required;
    health_report report;
    std::size_t consecutive_failures = 0;
    std::size_t consecutive_successes = 0;
    std::uint64_t revision = 0;
};

/**
 * @brief Performs asynchronous probes and serves cached health state.
 *
 * HTTP management handlers read snapshots only and never perform external I/O.
 */
export class health_registry
{
public:
    /**
     * @brief Creates a registry with the supplied probe policy.
     */
    explicit health_registry(health_policy policy = {});

    /**
     * @brief Adds a unique managed service to health monitoring.
     */
    [[nodiscard]] auto add(std::shared_ptr<managed_service> service)
        -> std::expected<void, std::error_code>;

    /**
     * @brief Marks the application as starting and not ready.
     */
    void mark_starting() noexcept;

    /**
     * @brief Marks startup complete, subject to component health.
     */
    void mark_running() noexcept;

    /**
     * @brief Revokes readiness before shutdown begins.
     */
    void mark_stopping() noexcept;

    /**
     * @brief Updates one cached report and applies hysteresis.
     */
    void update(const service_key& key, health_report report);

    /**
     * @brief Probes all registered services concurrently.
     */
    [[nodiscard]] auto refresh(service_context& context) -> task<void>;

    /**
     * @brief Reports process and event-loop liveness.
     */
    [[nodiscard]] auto live() const noexcept -> bool;

    /**
     * @brief Reports whether startup completed and every service is healthy.
     */
    [[nodiscard]] auto ready() const noexcept -> bool;

    /**
     * @brief Returns stable, sorted copies of all component snapshots.
     */
    [[nodiscard]] auto snapshots() const -> std::vector<service_health_snapshot>;

    /**
     * @brief Checks whether a borrowed snapshot still identifies the cached revision.
     *
     * This allocation-free check does not retain the registry lock on return.
     */
    [[nodiscard]] auto is_current(const service_health_snapshot& snapshot) const noexcept -> bool;

    /**
     * @brief Serializes aggregate health or readiness as sanitized JSON.
     */
    [[nodiscard]] auto json(bool readiness_only = false) const -> std::string;

    /**
     * @brief Returns a thread-safe copy of the active health policy.
     */
    [[nodiscard]] auto policy() const noexcept -> health_policy;

    /**
     * @brief Replaces the runtime-safe health policy after validation.
     */
    void update_policy(health_policy policy);

private:
    /**
     * @brief Commits a report only if its optional source revision is unchanged.
     */
    void update_report(const service_key& key, health_report report,
        std::optional<std::uint64_t> source_revision);

    class implementation;
    std::shared_ptr<implementation> implementation_;
};

} // namespace cnetmod::application
