module cnetmod.application.recovery_policy;

import std;

namespace cnetmod::application {

auto valid_recovery_policy(const recovery_policy& policy) noexcept -> bool
{
    const auto horizon = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::duration::max() / 2);
    return policy.initial_delay.count() > 0 &&
        policy.maximum_delay >= policy.initial_delay && policy.maximum_delay <= horizon &&
        policy.budget.count() >= 0 && policy.budget <= horizon &&
        std::isfinite(policy.multiplier) && policy.multiplier >= 1.0 &&
        std::isfinite(policy.jitter) && policy.jitter >= 0.0 && policy.jitter <= 1.0;
}

} // namespace cnetmod::application
