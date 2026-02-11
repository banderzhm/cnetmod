module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.task;

import std;

namespace cnetmod {

// =============================================================================
// promise_base — 共享的 promise 基类
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
// task<T> 实现
// =============================================================================

// =============================================================================
// task_awaiter — 命名空间级别模板（避免 MSVC COMDAT 重复）
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

/// void 特化
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
// task<T> 实现
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
        // index 0 = monostate (未设置), index 1 = 值
        std::variant<std::monostate, T> value_;
    };

    task() noexcept = default;
    ~task() {
        if (handle_)
            handle_.destroy();
    }

    // 不可复制
    task(const task&) = delete;
    auto operator=(const task&) -> task& = delete;

    // 可移动
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

    /// 获取底层 coroutine_handle
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
// task<void> 特化
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
// sync_wait — 同步等待协程完成
// =============================================================================

/// 在当前线程阻塞等待 task 完成并返回结果
export template <typename T>
auto sync_wait(task<T> t) -> T {
    auto handle = t.handle();
    handle.resume();

    // 协程应该已经运行到 final_suspend
    return handle.promise().result();
}

/// sync_wait<void> 特化
export inline void sync_wait(task<void> t) {
    auto handle = t.handle();
    handle.resume();
    handle.promise().result();
}

// =============================================================================
// when_all — 真正并发等待多个 task
// =============================================================================

namespace detail {

/// 共享状态：原子计数器 + 调用者 handle
struct when_all_state {
    std::atomic<int> remaining;
    std::coroutine_handle<> caller{};

    explicit when_all_state(int n) noexcept : remaining(n) {}

    /// 某个子任务完成时调用，最后一个完成者恢复调用者
    void notify_one_done() noexcept {
        if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            if (caller)
                caller.resume();
        }
    }
};

/// when_all 子任务包装器 — final_suspend 时通知 state 而非恢复 caller
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

/// void 特化
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

/// 将 task<T> 包装为 when_all_task<T>
template <typename T>
auto make_when_all_task(task<T> t) -> when_all_task<T> {
    co_return co_await std::move(t);
}

inline auto make_when_all_task(task<void> t) -> when_all_task<void> {
    co_await std::move(t);
}

/// when_all awaiter：挂起调用者，并发启动所有子任务
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
        // 设置 state 并启动所有子任务
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

/// 并发执行两个 task，返回结果 tuple
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

/// 并发执行三个 task
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
