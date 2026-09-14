/**
 * @brief Bounded retry policy shared by managed services and supervised tasks.
 */
export module cnetmod.application.recovery_policy;

import std;

namespace cnetmod::application {

/**
 * @brief Exponential backoff, jitter, and total recovery budget settings.
 */
export struct recovery_policy
{
    std::chrono::milliseconds initial_delay{500};
    std::chrono::milliseconds maximum_delay{30000};
    std::chrono::milliseconds budget{120000};
    double multiplier = 2.0;
    double jitter = 0.2;
};

/**
 * @brief Validates finite backoff parameters and representable scheduling durations.
 *
 * A zero budget disables retries. Delay values must be positive and ordered.
 */
export [[nodiscard]] auto valid_recovery_policy(const recovery_policy& policy) noexcept -> bool;

} // namespace cnetmod::application
