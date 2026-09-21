export module cnetmod.core.time;

import std;

export namespace cnetmod {

/**
 * @brief Returns the current Unix timestamp in whole UTC seconds.
 */
[[nodiscard]] auto unix_time_seconds() noexcept -> std::int64_t;

} // namespace cnetmod
