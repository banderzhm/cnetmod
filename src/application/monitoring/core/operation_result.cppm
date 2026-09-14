/**
 * @brief Terminal outcomes independent of protocols and telemetry exporters.
 */
export module cnetmod.instrumentation.operation_result;

import std;

namespace cnetmod::instrumentation {

/**
 * @brief Distinguishes business completion from cancellation and lost ownership.
 */
export enum class operation_status
{
    success,
    error,
    cancelled,
    timeout,
    abandoned
};

/**
 * @brief Preserves the original error code without capturing sensitive messages.
 */
export struct operation_result
{
    operation_status status{operation_status::success};
    std::error_code error;
};

} // namespace cnetmod::instrumentation
