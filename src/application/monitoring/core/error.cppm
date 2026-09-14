/**
 * @brief Classifies framework and standard errors without rewriting their codes.
 */
export module cnetmod.instrumentation.error;

import std;
export import cnetmod.instrumentation.operation_result;

namespace cnetmod::instrumentation {

/**
 * @brief Resolves cancellation and timeout semantics while retaining the source error.
 */
export [[nodiscard]] auto classify_error(std::error_code error) noexcept -> operation_result;

} // namespace cnetmod::instrumentation
