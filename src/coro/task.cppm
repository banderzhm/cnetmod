module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.task;

import std;

namespace cnetmod {

// =============================================================================
// promise_base — Shared promise base class
// =============================================================================

class promise_base {
public:
    auto initial_suspend() noexcept -> std::suspend_always { return {}; }

    struct final_awaiter {
        auto await_ready() const noexcept -> bool { return false; }

        template <typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> h) noexcept
            -> std::coroutine_handle<>
        {
            auto caller = h.promise().caller_;
            if (caller)
                return caller;
            return std::noop_coroutine();
        }

        void await_resume() noexcept {}
    };

    auto final_suspend() noexcept -> final_awaiter { return {}; }

    void set_caller(std::coroutine_handle<> caller) noexcept {
        caller_ = caller;
    }

protected:
    std::coroutine_handle<> caller_;
    std::exception_ptr exception_;
};

// =============================================================================
// task<T> implementation
// =============================================================================

// =============================================================================
// task_awaiter — Namespace-level template (avoid MSVC COMDAT duplication)
// =============================================================================

template <typename T, typename Promise>
struct task_awaiter {
    std::coroutine_handle<Promise> h;
    auto await_ready() const noexcept -> bool { return false; }
    auto await_suspend(std::coroutine_handle<> caller) noexcept
        -> std::coroutine_handle<>
    {
        h.promise().set_caller(caller);
        return h;
    }
    auto await_resume() -> T { return h.promise().result(); }
};

/// void specialization
template <typename Promise>
struct task_awaiter<void, Promise> {
    std::coroutine_handle<Promise> h;
    auto await_ready() const noexcept -> bool { return false; }
    auto await_suspend(std::coroutine_handle<> caller) noexcept
        -> std::coroutine_handle<>
    {
        h.promise().set_caller(caller);
        return h;
    }
    void await_resume() { h.promise().result(); }
};

// =============================================================================
// task<T> implementation
// =============================================================================

export template <typename T>
class task {
public:
    struct promise_type : promise_base {
        auto get_return_object() noexcept -> task {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        void return_value(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
            value_.template emplace<1>(std::move(value));
        }

        void unhandled_exception() noexcept {
            exception_ = std::current_exception();
        }

        auto result() -> T {
            if (exception_)
                std::rethrow_exception(exception_);
            return std::move(std::get<1>(value_));
        }

    private:
        // index 0 = monostate (not set), index 1 = value
        std::variant<std::monostate, T> value_;
    };

    task() noexcept = default;
    ~task() {
        if (handle_)
            handle_.destroy();
    }

    // Non-copyable
    task(const task&) = delete;
    auto operator=(const task&) -> task& = delete;

    // Movable
    task(task&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}
    auto operator=(task&& other) noexcept -> task& {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    /// co_await task<T>
    auto operator co_await() const noexcept -> task_awaiter<T, promise_type> {
        return {handle_};
    }

    /// Get underlying coroutine_handle
    [[nodiscard]] auto handle() const noexcept
        -> std::coroutine_handle<promise_type>
    {
        return handle_;
    }

private:
    explicit task(std::coroutine_handle<promise_type> h) noexcept
        : handle_(h) {}

    std::coroutine_handle<promise_type> handle_;
};

// =============================================================================
// task<void> specialization
// =============================================================================

export template <>
class task<void> {
public:
    struct promise_type : promise_base {
        auto get_return_object() noexcept -> task {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        void return_void() noexcept {}

        void unhandled_exception() noexcept {
            exception_ = std::current_exception();
        }

        void result() {
            if (exception_)
                std::rethrow_exception(exception_);
        }
    };

    task() noexcept = default;
    ~task() {
        if (handle_)
            handle_.destroy();
    }

    task(const task&) = delete;
    auto operator=(const task&) -> task& = delete;

    task(task&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}
    auto operator=(task&& other) noexcept -> task& {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    auto operator co_await() const noexcept -> task_awaiter<void, promise_type> {
        return {handle_};
    }

    [[nodiscard]] auto handle() const noexcept
        -> std::coroutine_handle<promise_type>
    {
        return handle_;
    }

private:
    explicit task(std::coroutine_handle<promise_type> h) noexcept
        : handle_(h) {}

    std::coroutine_handle<promise_type> handle_;
};

// =============================================================================
// sync_wait — Synchronously wait for coroutine completion
// =============================================================================

/// Block current thread waiting for task completion and return result
export template <typename T>
auto sync_wait(task<T> t) -> T {
    auto handle = t.handle();
    handle.resume();

    // Coroutine should have run to final_suspend
    return handle.promise().result();
}

/// sync_wait<void> specialization
export inline void sync_wait(task<void> t) {
    auto handle = t.handle();
    handle.resume();
    handle.promise().result();
}

// =============================================================================
// when_all — True concurrent waiting for multiple tasks
// =============================================================================

namespace detail {

/// Shared state: atomic counter + caller handle
struct when_all_state {
    std::atomic<int> remaining;
    std::coroutine_handle<> caller{};

    explicit when_all_state(int n) noexcept : remaining(n) {}

    /// Called when a subtask completes, last one resumes caller
    void notify_one_done() noexcept {
        if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            if (caller)
                caller.resume();
        }
    }
};

/// when_all subtask wrapper — notify state at final_suspend instead of resuming caller
template <typename T>
class when_all_task {
public:
    struct promise_type {
        when_all_state* state_ = nullptr;
        std::variant<std::monostate, T> value_;
        std::exception_ptr exception_;

        auto get_return_object() noexcept -> when_all_task {
            return when_all_task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        auto initial_suspend() noexcept -> std::suspend_always { return {}; }

        void return_value(T v) noexcept(std::is_nothrow_move_constructible_v<T>) {
            value_.template emplace<1>(std::move(v));
        }

        void unhandled_exception() noexcept {
            exception_ = std::current_exception();
        }

        struct final_awaiter {
            auto await_ready() const noexcept -> bool { return false; }
            void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                h.promise().state_->notify_one_done();
            }
            void await_resume() noexcept {}
        };

        auto final_suspend() noexcept -> final_awaiter { return {}; }

        auto result() -> T {
            if (exception_) std::rethrow_exception(exception_);
            return std::move(std::get<1>(value_));
        }
    };

    when_all_task() noexcept = default;
    ~when_all_task() { if (handle_) handle_.destroy(); }

    when_all_task(const when_all_task&) = delete;
    auto operator=(const when_all_task&) -> when_all_task& = delete;
    when_all_task(when_all_task&& o) noexcept : handle_(std::exchange(o.handle_, nullptr)) {}
    auto operator=(when_all_task&& o) noexcept -> when_all_task& {
        if (this != &o) { if (handle_) handle_.destroy(); handle_ = std::exchange(o.handle_, nullptr); }
        return *this;
    }

    void set_state(when_all_state* s) noexcept { handle_.promise().state_ = s; }
    void start() noexcept { handle_.resume(); }
    auto result() -> T { return handle_.promise().result(); }

private:
    explicit when_all_task(std::coroutine_handle<promise_type> h) noexcept : handle_(h) {}
    std::coroutine_handle<promise_type> handle_;
};

/// void specialization
template <>
class when_all_task<void> {
public:
    struct promise_type {
        when_all_state* state_ = nullptr;
        std::exception_ptr exception_;

        auto get_return_object() noexcept -> when_all_task {
            return when_all_task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        auto initial_suspend() noexcept -> std::suspend_always { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { exception_ = std::current_exception(); }

        struct final_awaiter {
            auto await_ready() const noexcept -> bool { return false; }
            void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                h.promise().state_->notify_one_done();
            }
            void await_resume() noexcept {}
        };

        auto final_suspend() noexcept -> final_awaiter { return {}; }

        void result() {
            if (exception_) std::rethrow_exception(exception_);
        }
    };

    when_all_task() noexcept = default;
    ~when_all_task() { if (handle_) handle_.destroy(); }

    when_all_task(const when_all_task&) = delete;
    auto operator=(const when_all_task&) -> when_all_task& = delete;
    when_all_task(when_all_task&& o) noexcept : handle_(std::exchange(o.handle_, nullptr)) {}
    auto operator=(when_all_task&& o) noexcept -> when_all_task& {
        if (this != &o) { if (handle_) handle_.destroy(); handle_ = std::exchange(o.handle_, nullptr); }
        return *this;
    }

    void set_state(when_all_state* s) noexcept { handle_.promise().state_ = s; }
    void start() noexcept { handle_.resume(); }
    void result() { handle_.promise().result(); }

private:
    explicit when_all_task(std::coroutine_handle<promise_type> h) noexcept : handle_(h) {}
    std::coroutine_handle<promise_type> handle_;
};

/// Wrap task<T> as when_all_task<T>
template <typename T>
auto make_when_all_task(task<T> t) -> when_all_task<T> {
    co_return co_await std::move(t);
}

inline auto make_when_all_task(task<void> t) -> when_all_task<void> {
    co_await std::move(t);
}

/// when_all awaiter: suspend caller and concurrently start all subtasks
template <typename... Tasks>
struct when_all_awaiter {
    when_all_state state_;
    std::tuple<Tasks...> tasks_;

    explicit when_all_awaiter(Tasks... ts)
        : state_(sizeof...(Tasks))
        , tasks_(std::move(ts)...) {}

    auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<> caller) noexcept {
        state_.caller = caller;
        // Set state and start all subtasks
        std::apply([this](auto&... t) {
            (t.set_state(&state_), ...);
        }, tasks_);
        std::apply([](auto&... t) {
            (t.start(), ...);
        }, tasks_);
    }

    void await_resume() {}
};

} // namespace detail

/// Concurrently execute two tasks, return result tuple
export template <typename T1, typename T2>
auto when_all(task<T1> t1, task<T2> t2)
    -> task<std::tuple<T1, T2>>
{
    auto w1 = detail::make_when_all_task(std::move(t1));
    auto w2 = detail::make_when_all_task(std::move(t2));

    detail::when_all_awaiter<detail::when_all_task<T1>, detail::when_all_task<T2>> aw{std::move(w1), std::move(w2)};
    co_await aw;

    auto& [a, b] = aw.tasks_;
    co_return std::tuple{a.result(), b.result()};
}

/// Concurrently execute three tasks
export template <typename T1, typename T2, typename T3>
auto when_all(task<T1> t1, task<T2> t2, task<T3> t3)
    -> task<std::tuple<T1, T2, T3>>
{
    auto w1 = detail::make_when_all_task(std::move(t1));
    auto w2 = detail::make_when_all_task(std::move(t2));
    auto w3 = detail::make_when_all_task(std::move(t3));

    detail::when_all_awaiter<detail::when_all_task<T1>, detail::when_all_task<T2>, detail::when_all_task<T3>> aw{std::move(w1), std::move(w2), std::move(w3)};
    co_await aw;

    auto& [a, b, c] = aw.tasks_;
    co_return std::tuple{a.result(), b.result(), c.result()};
}

/// Variadic when_all for N >= 4 tasks of any types
export template <typename... Ts>
    requires (sizeof...(Ts) >= 4)
auto when_all(task<Ts>... ts) -> task<std::tuple<Ts...>> {
    using AwaiterT = detail::when_all_awaiter<detail::when_all_task<Ts>...>;
    AwaiterT aw{detail::make_when_all_task(std::move(ts))...};
    co_await aw;
    co_return std::apply([](auto&... w) {
        return std::tuple<Ts...>{w.result()...};
    }, aw.tasks_);
}

/// when_all: void + non-void → returns non-void result (void task runs concurrently)
export template <typename T2>
    requires (!std::is_void_v<T2>)
auto when_all(task<void> t1, task<T2> t2) -> task<T2>
{
    auto w1 = detail::make_when_all_task(std::move(t1));
    auto w2 = detail::make_when_all_task(std::move(t2));

    detail::when_all_awaiter<detail::when_all_task<void>, detail::when_all_task<T2>> aw{std::move(w1), std::move(w2)};
    co_await aw;

    auto& [a, b] = aw.tasks_;
    a.result(); // propagate exception if any
    co_return b.result();
}

/// when_all: non-void + void → returns non-void result
export template <typename T1>
    requires (!std::is_void_v<T1>)
auto when_all(task<T1> t1, task<void> t2) -> task<T1>
{
    auto w1 = detail::make_when_all_task(std::move(t1));
    auto w2 = detail::make_when_all_task(std::move(t2));

    detail::when_all_awaiter<detail::when_all_task<T1>, detail::when_all_task<void>> aw{std::move(w1), std::move(w2)};
    co_await aw;

    auto& [a, b] = aw.tasks_;
    b.result(); // propagate exception if any
    co_return a.result();
}

/// when_all for void tasks
export inline auto when_all(task<void> t1, task<void> t2)
    -> task<void>
{
    auto w1 = detail::make_when_all_task(std::move(t1));
    auto w2 = detail::make_when_all_task(std::move(t2));

    detail::when_all_awaiter<detail::when_all_task<void>, detail::when_all_task<void>> aw{std::move(w1), std::move(w2)};
    co_await aw;

    auto& [a, b] = aw.tasks_;
    a.result();
    b.result();
}

} // namespace cnetmod
