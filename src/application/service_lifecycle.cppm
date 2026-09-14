/**
 * @brief Dependency-aware transactional startup and bounded reverse shutdown.
 */
export module cnetmod.application.service_lifecycle;

import std;
import cnetmod.application.health_registry;
import cnetmod.application.managed_service;
import cnetmod.application.recovery_policy;
import cnetmod.application.service_registry;
import cnetmod.application.task_supervisor;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.io.io_context;
import cnetmod.observability;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod::application {

/**
 * @brief Time limits for startup, draining, shutdown, and telemetry flushing.
 */
export struct lifecycle_policy
{
    std::chrono::milliseconds service_start_timeout{30000};
    std::chrono::milliseconds total_start_timeout{120000};
    std::chrono::milliseconds service_stop_timeout{10000};
    std::chrono::milliseconds total_stop_timeout{30000};
    std::chrono::milliseconds http_drain_timeout{10000};
    std::chrono::milliseconds telemetry_flush_timeout{5000};
};

/**
 * @brief Lifecycle phase in which a service failure occurred.
 */
export enum class lifecycle_phase
{
    startup,
    health_recovery,
    shutdown,
    rollback
};

/**
 * @brief Structured failure retaining service identity and original error code.
 */
export struct lifecycle_failure
{
    service_key service;
    lifecycle_phase phase = lifecycle_phase::startup;
    std::error_code error;
};

/**
 * @brief Coordinates transactional service startup, recovery, and shutdown.
 *
 * Services start in parallel topological layers. Shutdown and rollback process
 * only successfully started services in strict reverse dependency order.
 */
export class service_lifecycle
{
public:
    /**
     * @brief Creates a lifecycle coordinator over a frozen service registry.
     */
    service_lifecycle(io_context& io, observability::telemetry_hub& telemetry,
        service_registry& services, task_supervisor& supervisor,
        health_registry& health, lifecycle_policy policy = {});

    /**
     * @brief Starts all services according to dependency and requirement rules.
     * @param rollback_reserve Time retained for the caller after startup rollback.
     * Must be nonnegative and no greater than the configured total stop timeout.
     */
    [[nodiscard]] auto start(std::chrono::milliseconds rollback_reserve = {})
        -> task<std::expected<void, std::error_code>>;

    /**
     * @brief Stops successfully started services within the caller's budget.
     * @param budget Absolute deadline shared with preceding shutdown phases.
     *
     * The configured shutdown limit can shorten, but never extend, this budget.
     * Each service receives an independent cancellation token. Deadline expiry
     * cancels the operation and waits for its cooperative completion; it never
     * destroys a suspended service coroutine. Services must honor cancellation.
     */
    [[nodiscard]] auto stop(deadline budget = {})
        -> task<std::expected<void, std::error_code>>;

    /**
     * @brief Schedules bounded supervised recovery for one service.
     */
    [[nodiscard]] auto schedule_recovery(const service_key& key)
        -> std::expected<void, std::error_code>;

    /**
     * @brief Reconciles confirmed health with the current recovery episode.
     *
     * Only confirmed up ends an episode. Reconnection alone never replenishes
     * its budget; pending confirmation is checked for expiry on each refresh.
     */
    [[nodiscard]] auto reconcile_health(const service_health_snapshot& snapshot)
        -> std::expected<void, std::error_code>;

    /**
     * @brief Atomically applies validated recovery policy overrides.
     * Allocation or validation failure leaves all existing overrides unchanged.
     */
    [[nodiscard]] auto update_recovery_policies(
        std::span<const std::pair<service_key, recovery_policy>> updates)
        -> std::expected<void, std::error_code>;

    /**
     * @brief Returns one explicitly installed policy override under the state lock.
     */
    [[nodiscard]] auto recovery_policy_override(const service_key& key) const
        -> std::optional<recovery_policy>;

    /**
     * @brief Returns a thread-safe snapshot of successfully started services.
     */
    [[nodiscard]] auto started_services() const -> std::vector<service_key>;

    /**
     * @brief Counts retained service ownership without allocating a snapshot.
     */
    [[nodiscard]] auto active_service_count() const noexcept -> std::size_t;

    /**
     * @brief Returns the most recent structured lifecycle failure.
     */
    [[nodiscard]] auto last_failure() const
        -> std::optional<lifecycle_failure>;

    /**
     * @brief Returns rollback failure without replacing the initiating failure.
     */
    [[nodiscard]] auto last_rollback_failure() const
        -> std::optional<lifecycle_failure>;

    /**
     * @brief Returns the last rollback deadline for subsequent host cleanup.
     *
     * An unlimited deadline means no rollback has started. Reading the value
     * never replenishes the budget already consumed by service cleanup.
     */
    [[nodiscard]] auto rollback_deadline() const noexcept -> deadline;

    /**
     * @brief Returns rollback failure even when no component could be identified.
     */
    [[nodiscard]] auto last_rollback_error() const noexcept -> std::error_code;

private:
    void mark_started(const service_key& key);
    [[nodiscard]] auto rollback(std::chrono::milliseconds reserve)
        -> task<std::expected<void, std::error_code>>;

    io_context& io_;
    observability::telemetry_hub& telemetry_;
    service_registry& services_;
    task_supervisor& supervisor_;
    health_registry& health_;
    lifecycle_policy policy_;
    std::vector<std::vector<service_key>> started_layers_;
    std::unordered_map<service_key, bool, service_key_hash> service_started_;
    std::unordered_map<service_key, deadline, service_key_hash> recovery_deadlines_;
    std::unordered_map<service_key, recovery_policy, service_key_hash>
        recovery_overrides_;
    std::optional<lifecycle_failure> last_failure_;
    std::optional<lifecycle_failure> rollback_failure_;
    deadline rollback_deadline_;
    std::error_code rollback_error_;
    mutable concurrent_containers::atomic_rw_latch started_latch_;
};

} // namespace cnetmod::application
