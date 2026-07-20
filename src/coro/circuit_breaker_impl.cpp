module cnetmod.coro.circuit_breaker;

import std;

namespace cnetmod
{
    namespace detail
    {
        auto cb_error_category_impl::name() const noexcept -> const char* { return "circuit_breaker"; }

        auto cb_error_category_impl::message(int ev) const -> std::string
        {
            return static_cast<circuit_breaker_errc>(ev) == circuit_breaker_errc::success
                       ? "success"
                       : static_cast<circuit_breaker_errc>(ev) == circuit_breaker_errc::circuit_open
                       ? "circuit breaker is open"
                       : "unknown circuit breaker error";
        }

        auto cb_category_instance() -> const std::error_category&
        {
            static const cb_error_category_impl instance;
            return instance;
        }
    } // namespace detail

    auto make_error_code(circuit_breaker_errc e) noexcept -> std::error_code
    {
        return {static_cast<int>(e), detail::cb_category_instance()};
    }

    circuit_breaker::circuit_breaker(circuit_breaker_options opts) noexcept : opts_(opts)
    {
    }

    auto circuit_breaker::state() const noexcept -> circuit_breaker_state
    {
        std::lock_guard lock(mtx_);
        return maybe_transition_state();
    }

    auto circuit_breaker::failure_count() const noexcept -> std::uint32_t
    {
        std::lock_guard lock(mtx_);
        return failure_count_;
    }

    auto circuit_breaker::success_count() const noexcept -> std::uint32_t
    {
        std::lock_guard lock(mtx_);
        return success_count_;
    }

    void circuit_breaker::reset() noexcept
    {
        std::lock_guard lock(mtx_);
        state_ = circuit_breaker_state::closed;
        failure_count_ = success_count_ = 0;
    }

    void circuit_breaker::trip() noexcept
    {
        std::lock_guard lock(mtx_);
        state_ = circuit_breaker_state::open;
        open_time_ = std::chrono::steady_clock::now();
    }

    auto circuit_breaker::pre_execute() noexcept -> execute_action
    {
        std::lock_guard lock(mtx_);
        return maybe_transition_state() == circuit_breaker_state::open ? execute_action::reject : execute_action::allow;
    }

    void circuit_breaker::on_success() noexcept
    {
        std::lock_guard lock(mtx_);
        const auto effective = maybe_transition_state();
        if (effective == circuit_breaker_state::half_open && ++success_count_ >= opts_.success_threshold)
        {
            state_ = circuit_breaker_state::closed;
            failure_count_ = success_count_ = 0;
        }
        else if (effective == circuit_breaker_state::closed) failure_count_ = 0;
    }

    void circuit_breaker::on_failure() noexcept
    {
        std::lock_guard lock(mtx_);
        const auto effective = maybe_transition_state();
        if (effective == circuit_breaker_state::half_open)
        {
            state_ = circuit_breaker_state::open;
            open_time_ = std::chrono::steady_clock::now();
            success_count_ = 0;
        }
        else if (effective == circuit_breaker_state::closed && ++failure_count_ >= opts_.failure_threshold)
        {
            state_ = circuit_breaker_state::open;
            open_time_ = std::chrono::steady_clock::now();
        }
    }

    auto circuit_breaker::maybe_transition_state() const noexcept -> circuit_breaker_state
    {
        if (state_ == circuit_breaker_state::open && std::chrono::steady_clock::now() - open_time_ >= opts_.timeout)
        {
            state_ = circuit_breaker_state::half_open;
            success_count_ = 0;
        }
        return state_;
    }
} // namespace cnetmod