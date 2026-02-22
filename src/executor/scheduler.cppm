module;

#include <cnetmod/config.hpp>
#include <new>
#include <stdexec/execution.hpp>

export module cnetmod.executor.scheduler;

import std;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.coro.task;

namespace cnetmod {

// Forward declaration
class schedule_sender;

// =============================================================================
// io_scheduler — stdexec scheduler backed by io_context
// =============================================================================

export class io_scheduler {
public:
    using scheduler_concept = stdexec::scheduler_t;

    explicit io_scheduler(io_context& ctx) noexcept
        : ctx_(&ctx) {}

    auto operator==(const io_scheduler&) const noexcept -> bool = default;

    [[nodiscard]] auto context() const noexcept -> io_context& {
        return *ctx_;
    }

    /// schedule() -> sender (defined after schedule_sender is complete)
    auto schedule() noexcept -> schedule_sender;

private:
    io_context* ctx_;
};

// =============================================================================
// schedule_env — completion scheduler query support
// =============================================================================

struct schedule_env {
    io_context* ctx_;

    auto query(stdexec::get_completion_scheduler_t<stdexec::set_value_t>) const noexcept
        -> io_scheduler
    {
        return io_scheduler{*ctx_};
    }
};

// =============================================================================
// schedule_sender — returned by io_scheduler::schedule()
// =============================================================================

class schedule_sender {
    template <typename Receiver>
    struct schedule_op {
        using operation_state_concept = stdexec::operation_state_t;
        io_context* ctx_;
        Receiver rcvr_;

        void start() noexcept {
            // Zero coroutine overhead: post to event loop via function pointer callback
            ctx_->post(&deliver, static_cast<void*>(this));
        }

    private:
        static void deliver(void* p) noexcept {
            auto* self = static_cast<schedule_op*>(p);
            stdexec::set_value(std::move(self->rcvr_));
        }
    };

public:
    using sender_concept = stdexec::sender_t;
    using completion_signatures = stdexec::completion_signatures<stdexec::set_value_t()>;

    explicit schedule_sender(io_context* ctx) noexcept
        : ctx_(ctx) {}

    template <typename Receiver>
    auto connect(Receiver rcvr) const noexcept -> schedule_op<Receiver> {
        return {ctx_, std::move(rcvr)};
    }

    auto get_env() const noexcept -> schedule_env {
        return {ctx_};
    }

private:
    io_context* ctx_;
};

inline auto io_scheduler::schedule() noexcept -> schedule_sender {
    return schedule_sender{ctx_};
}

// =============================================================================
// task_op_state<T, Receiver> — bridges task<T> execution to stdexec receiver
// =============================================================================

template <typename T, typename Receiver>
class task_op_state {
public:
    using operation_state_concept = stdexec::operation_state_t;

    task_op_state(task<T>&& t, Receiver rcvr)
        : task_(std::move(t))
        , rcvr_(std::move(rcvr)) {}

    task_op_state(const task_op_state&) = delete;
    auto operator=(const task_op_state&) -> task_op_state& = delete;
    task_op_state(task_op_state&&) = delete;
    auto operator=(task_op_state&&) -> task_op_state& = delete;

    ~task_op_state() {
        if (bridge_)
            bridge_.destroy();
    }

    void start() noexcept {
        auto coro = run_bridge(this);
        bridge_ = coro.handle_;
        bridge_.resume();
    }

private:
    // --- Result delivery (called from bridge's final_suspend) ---
    void deliver() noexcept {
        if (error_) {
            stdexec::set_error(std::move(rcvr_), std::move(error_));
        } else if constexpr (std::is_void_v<T>) {
            stdexec::set_value(std::move(rcvr_));
        } else {
            stdexec::set_value(std::move(rcvr_), std::move(*value_));
        }
    }

    // --- Bridge coroutine infrastructure ---
    struct bridge_promise;

    struct bridge_coro {
        using promise_type = bridge_promise;
        std::coroutine_handle<bridge_promise> handle_;
        // No destructor — lifetime managed by task_op_state::bridge_
    };

    struct bridge_promise {
        task_op_state* op_;

        // Promise is constructed with coroutine function arguments
        bridge_promise(task_op_state* op) : op_(op) {}

        auto get_return_object() noexcept -> bridge_coro {
            return {std::coroutine_handle<bridge_promise>::from_promise(*this)};
        }

        auto initial_suspend() noexcept -> std::suspend_always { return {}; }
        void return_void() noexcept {}

        void unhandled_exception() noexcept {
            // All exceptions caught explicitly in run_bridge body
            std::terminate();
        }

        // At final_suspend, the coroutine is suspended and safe to destroy.
        // Deliver the result to the receiver here.
        struct final_awaiter {
            auto await_ready() const noexcept -> bool { return false; }

            auto await_suspend(std::coroutine_handle<bridge_promise> h) noexcept
                -> std::coroutine_handle<>
            {
                h.promise().op_->deliver();
                return std::noop_coroutine();
            }

            void await_resume() noexcept {}
        };

        auto final_suspend() noexcept -> final_awaiter { return {}; }
    };

    // Bridge coroutine: co_awaits the task, stores result, then delivers via final_suspend
    static auto run_bridge(task_op_state* op) -> bridge_coro {
        try {
            if constexpr (std::is_void_v<T>) {
                co_await std::move(op->task_);
            } else {
                op->value_.emplace(co_await std::move(op->task_));
            }
        } catch (...) {
            op->error_ = std::current_exception();
        }
    }

    // --- Data members ---
    task<T> task_;
    Receiver rcvr_;

    using value_storage_t = std::conditional_t<
        std::is_void_v<T>, std::monostate, std::optional<T>>;
    [[no_unique_address]] value_storage_t value_{};

    std::exception_ptr error_;
    std::coroutine_handle<bridge_promise> bridge_{};
};

// =============================================================================
// task_sender<T> — wraps task<T> as stdexec sender
// =============================================================================

// Helper: avoid forming set_value_t(void) which is ill-formed on clang.
// Class-template specialization ensures set_value_t(T) is never instantiated
// when T = void.
namespace detail {

template <typename T>
struct task_completion_sigs {
    using type = stdexec::completion_signatures<
        stdexec::set_value_t(T),
        stdexec::set_error_t(std::exception_ptr)
    >;
};

template <>
struct task_completion_sigs<void> {
    using type = stdexec::completion_signatures<
        stdexec::set_value_t(),
        stdexec::set_error_t(std::exception_ptr)
    >;
};

} // namespace detail

export template <typename T>
class task_sender {
public:
    using sender_concept = stdexec::sender_t;

    using completion_signatures = typename detail::task_completion_sigs<T>::type;

    explicit task_sender(task<T> t) noexcept
        : task_(std::move(t)) {}

    task_sender(const task_sender&) = delete;
    auto operator=(const task_sender&) -> task_sender& = delete;
    task_sender(task_sender&&) noexcept = default;
    auto operator=(task_sender&&) noexcept -> task_sender& = default;

    template <typename Receiver>
    auto connect(Receiver rcvr) && -> task_op_state<T, Receiver> {
        return {std::move(task_), std::move(rcvr)};
    }

private:
    task<T> task_;
};

// =============================================================================
// as_sender — factory: convert task<T> to stdexec sender
// =============================================================================

export template <typename T>
auto as_sender(task<T> t) -> task_sender<T> {
    return task_sender<T>{std::move(t)};
}

// =============================================================================
// sync_wait_sender — run a task_sender synchronously
// =============================================================================

namespace detail {

/// Manual receiver: stores set_value / set_error result
template <typename T>
struct manual_receiver {
    using receiver_concept = stdexec::receiver_t;

    std::optional<T>* value_;
    std::exception_ptr* error_;

    void set_value(T val) noexcept {
        value_->emplace(std::move(val));
    }
    void set_error(std::exception_ptr e) noexcept {
        *error_ = std::move(e);
    }
    void set_stopped() noexcept {}

    struct env {};
    auto get_env() const noexcept -> env { return {}; }
};

/// void specialization
template <>
struct manual_receiver<void> {
    using receiver_concept = stdexec::receiver_t;

    bool* done_;
    std::exception_ptr* error_;

    void set_value() noexcept {
        *done_ = true;
    }
    void set_error(std::exception_ptr e) noexcept {
        *error_ = std::move(e);
    }
    void set_stopped() noexcept {}

    struct env {};
    auto get_env() const noexcept -> env { return {}; }
};

} // namespace detail

/// Synchronously run task_sender<T>, returns T
/// Implemented via manual connect + start, MSVC compatible
export template <typename T>
auto sync_wait_sender(task_sender<T>&& sender) -> T {
    if constexpr (std::is_void_v<T>) {
        bool done = false;
        std::exception_ptr error;
        auto op = std::move(sender).connect(
            detail::manual_receiver<void>{&done, &error});
        op.start();
        if (error)
            std::rethrow_exception(error);
    } else {
        std::optional<T> result;
        std::exception_ptr error;
        auto op = std::move(sender).connect(
            detail::manual_receiver<T>{&result, &error});
        op.start();
        if (error)
            std::rethrow_exception(error);
        if (!result)
            throw std::runtime_error("sync_wait_sender: no result");
        return std::move(*result);
    }
}

} // namespace cnetmod
