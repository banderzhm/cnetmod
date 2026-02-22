/**
 * @file circuit_breaker.cppm
 * @brief Circuit Breaker pattern — Prevent cascading failures
 *
 * Three states:
 *   closed    → Normal operation, failures counted
 *   open      → Requests rejected immediately, waits for timeout
 *   half_open → Probe requests allowed, success resets to closed
 *
 * Usage example:
 *   import cnetmod.coro.circuit_breaker;
 *
 *   circuit_breaker cb({
 *       .failure_threshold = 5,
 *       .success_threshold = 2,
 *       .timeout = 30s,
 *   });
 *
 *   auto result = co_await cb.execute([&]() -> task<expected<string, error_code>> {
 *       return mysql_query("SELECT ...");
 *   });
 */
module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.circuit_breaker;

import std;
import cnetmod.coro.task;

namespace cnetmod {

// =============================================================================
// circuit_breaker_options
// =============================================================================

export struct circuit_breaker_options {
    std::uint32_t failure_threshold = 5;   ///< Failures before opening
    std::uint32_t success_threshold = 2;   ///< Successes in half_open before closing
    std::chrono::steady_clock::duration
        timeout = std::chrono::seconds(30); ///< Time in open state before half_open
};

// =============================================================================
// circuit_breaker_state
// =============================================================================

export enum class circuit_breaker_state : std::uint8_t {
    closed,     ///< Normal — requests pass through, failures tracked
    open,       ///< Tripped — requests rejected immediately
    half_open,  ///< Probing — limited requests allowed to test recovery
};

// =============================================================================
// circuit_breaker_error
// =============================================================================

export enum class circuit_breaker_errc {
    success = 0,
    circuit_open,  ///< Circuit is open, request rejected
};

namespace detail {

class cb_error_category_impl : public std::error_category {
public:
    auto name() const noexcept -> const char* override { return "circuit_breaker"; }
    auto message(int ev) const -> std::string override {
        switch (static_cast<circuit_breaker_errc>(ev)) {
            case circuit_breaker_errc::success:      return "success";
            case circuit_breaker_errc::circuit_open:  return "circuit breaker is open";
            default:                                  return "unknown circuit breaker error";
        }
    }
};

inline auto cb_category_instance() -> const std::error_category& {
    static const cb_error_category_impl instance;
    return instance;
}

} // namespace detail

export inline auto make_error_code(circuit_breaker_errc e) noexcept -> std::error_code {
    return {static_cast<int>(e), detail::cb_category_instance()};
}

// =============================================================================
// circuit_breaker — Three-state circuit breaker
// =============================================================================

export class circuit_breaker {
public:
    explicit circuit_breaker(circuit_breaker_options opts = {}) noexcept
        : opts_(opts) {}

    circuit_breaker(const circuit_breaker&) = delete;
    auto operator=(const circuit_breaker&) -> circuit_breaker& = delete;

    /// Execute an operation through the circuit breaker
    /// Fn must return task<expected<T, E>>
    template <typename T, typename E, typename Fn>
        requires std::invocable<Fn> &&
                 std::same_as<std::invoke_result_t<Fn>, task<std::expected<T, E>>>
    auto execute(Fn fn) -> task<std::expected<T, E>> {
        // Check state transition: open → half_open on timeout
        auto action = pre_execute();

        if (action == execute_action::reject) {
            co_return std::unexpected(E{});
        }

        // Execute the operation
        auto result = co_await fn();

        if (result.has_value()) {
            on_success();
        } else {
            on_failure();
        }

        co_return std::move(result);
    }

    /// Execute with std::error_code as error type (convenience overload)
    template <typename T, typename Fn>
        requires std::invocable<Fn> &&
                 std::same_as<std::invoke_result_t<Fn>, task<std::expected<T, std::error_code>>>
    auto execute_ec(Fn fn) -> task<std::expected<T, std::error_code>> {
        auto action = pre_execute();

        if (action == execute_action::reject) {
            co_return std::unexpected(make_error_code(circuit_breaker_errc::circuit_open));
        }

        auto result = co_await fn();

        if (result.has_value()) {
            on_success();
        } else {
            on_failure();
        }

        co_return std::move(result);
    }

    /// Query current state
    [[nodiscard]] auto state() const noexcept -> circuit_breaker_state {
        std::lock_guard lock(mtx_);
        return maybe_transition_state();
    }

    /// Get failure count
    [[nodiscard]] auto failure_count() const noexcept -> std::uint32_t {
        std::lock_guard lock(mtx_);
        return failure_count_;
    }

    /// Get success count (in half_open state)
    [[nodiscard]] auto success_count() const noexcept -> std::uint32_t {
        std::lock_guard lock(mtx_);
        return success_count_;
    }

    /// Manually reset to closed state
    void reset() noexcept {
        std::lock_guard lock(mtx_);
        state_ = circuit_breaker_state::closed;
        failure_count_ = 0;
        success_count_ = 0;
    }

    /// Manually trip the circuit breaker to open state
    void trip() noexcept {
        std::lock_guard lock(mtx_);
        state_ = circuit_breaker_state::open;
        open_time_ = std::chrono::steady_clock::now();
    }

private:
    enum class execute_action { allow, reject };

    auto pre_execute() noexcept -> execute_action {
        std::lock_guard lock(mtx_);
        auto effective = maybe_transition_state();

        switch (effective) {
            case circuit_breaker_state::closed:
                return execute_action::allow;

            case circuit_breaker_state::open:
                return execute_action::reject;

            case circuit_breaker_state::half_open:
                // Allow probe request
                return execute_action::allow;
        }
        return execute_action::reject;
    }

    void on_success() noexcept {
        std::lock_guard lock(mtx_);
        auto effective = maybe_transition_state();

        if (effective == circuit_breaker_state::half_open) {
            ++success_count_;
            if (success_count_ >= opts_.success_threshold) {
                // Recovery confirmed — close circuit
                state_ = circuit_breaker_state::closed;
                failure_count_ = 0;
                success_count_ = 0;
            }
        } else if (effective == circuit_breaker_state::closed) {
            // Reset failure count on success
            failure_count_ = 0;
        }
    }

    void on_failure() noexcept {
        std::lock_guard lock(mtx_);
        auto effective = maybe_transition_state();

        if (effective == circuit_breaker_state::half_open) {
            // Probe failed — back to open
            state_ = circuit_breaker_state::open;
            open_time_ = std::chrono::steady_clock::now();
            success_count_ = 0;
        } else if (effective == circuit_breaker_state::closed) {
            ++failure_count_;
            if (failure_count_ >= opts_.failure_threshold) {
                // Threshold exceeded — open circuit
                state_ = circuit_breaker_state::open;
                open_time_ = std::chrono::steady_clock::now();
            }
        }
    }

    /// Check if open → half_open transition should occur (based on timeout)
    /// Must be called with mtx_ held
    auto maybe_transition_state() const noexcept -> circuit_breaker_state {
        if (state_ == circuit_breaker_state::open) {
            auto elapsed = std::chrono::steady_clock::now() - open_time_;
            if (elapsed >= opts_.timeout) {
                // Transition to half_open
                state_ = circuit_breaker_state::half_open;
                success_count_ = 0;
            }
        }
        return state_;
    }

    circuit_breaker_options opts_;
    mutable std::mutex mtx_;
    mutable circuit_breaker_state state_ = circuit_breaker_state::closed;
    mutable std::uint32_t failure_count_ = 0;
    mutable std::uint32_t success_count_ = 0;
    mutable std::chrono::steady_clock::time_point open_time_;
};

} // namespace cnetmod

template <>
struct std::is_error_code_enum<cnetmod::circuit_breaker_errc> : std::true_type {};
