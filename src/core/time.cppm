export module cnetmod.core.time;

import std;

export namespace cnetmod {

/**
 * Returns the current Unix timestamp in whole seconds.
 */
[[nodiscard]] auto unix_time_seconds() noexcept -> std::int64_t;

} // namespace cnetmod
