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
// when_all — 并发等待多个 task
// =============================================================================

/// 并发执行两个 task，返回结果 tuple
export template <typename T1, typename T2>
auto when_all(task<T1> t1, task<T2> t2)
    -> task<std::tuple<T1, T2>>
{
    auto r1 = co_await std::move(t1);
    auto r2 = co_await std::move(t2);
    co_return std::tuple{std::move(r1), std::move(r2)};
}

/// 并发执行三个 task
export template <typename T1, typename T2, typename T3>
auto when_all(task<T1> t1, task<T2> t2, task<T3> t3)
    -> task<std::tuple<T1, T2, T3>>
{
    auto r1 = co_await std::move(t1);
    auto r2 = co_await std::move(t2);
    auto r3 = co_await std::move(t3);
    co_return std::tuple{std::move(r1), std::move(r2), std::move(r3)};
}

/// when_all for void tasks
export inline auto when_all(task<void> t1, task<void> t2)
    -> task<void>
{
    co_await std::move(t1);
    co_await std::move(t2);
}

} // namespace cnetmod
