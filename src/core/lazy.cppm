/// Cold, single-consumer coroutine values.
///
/// Unlike cnetmod::task, a lazy coroutine is not started by merely creating it.
/// It is intended for deferred composition: awaiting it transfers directly to
/// the child coroutine and resumes the awaiting coroutine at final suspend.
export module cnetmod.core.lazy;

import std;

namespace cnetmod {

namespace detail {

    class lazy_promise_base
    {
    public:
        auto initial_suspend() noexcept -> std::suspend_always
        {
            return {};
        }

        struct final_awaiter
        {
            auto await_ready() const noexcept -> bool
            {
                return false;
            }

            template <typename Promise>
            auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> std::coroutine_handle<>
            {
                auto continuation = handle.promise().continuation_;
                return continuation ? continuation : std::noop_coroutine();
            }

            void await_resume() noexcept {}
        };

        auto final_suspend() noexcept -> final_awaiter
        {
            return {};
        }

        void unhandled_exception() noexcept
        {
            exception_ = std::current_exception();
        }

        void set_continuation(std::coroutine_handle<> continuation) noexcept
        {
            continuation_ = continuation;
        }

    protected:
        void rethrow_if_exception() const
        {
            if (exception_)
                std::rethrow_exception(exception_);
        }

    private:
        std::coroutine_handle<> continuation_{};
        std::exception_ptr exception_{};
    };

} // namespace detail

/// A move-only, cold coroutine result. A lazy value has exactly one consumer:
/// either call start()/result() manually, or co_await std::move(value).
export template <typename T> class lazy
{
public:
    struct promise_type : detail::lazy_promise_base
    {
        auto get_return_object() noexcept -> lazy
        {
            return lazy{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        template <typename U>
        requires std::convertible_to<U, T>
        void return_value(U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        {
            value_.emplace(std::forward<U>(value));
        }

        auto result() -> T
        {
            rethrow_if_exception();
            if (!value_)
                throw std::logic_error("lazy coroutine completed without a value");
            return std::move(*value_);
        }

    private:
        std::optional<T> value_{};
    };

    lazy() noexcept = default;

    ~lazy()
    {
        if (handle_)
            handle_.destroy();
    }

    lazy(const lazy&) = delete;
    auto operator=(const lazy&) -> lazy& = delete;

    lazy(lazy&& other) noexcept
        : handle_(std::exchange(other.handle_, {})), started_(std::exchange(other.started_, false)) {}

    auto operator=(lazy&& other) noexcept -> lazy&
    {
        if (this != &other)
        {
            if (handle_)
                handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
            started_ = std::exchange(other.started_, false);
        }
        return *this;
    }

    /// Start a cold coroutine without attaching a continuation. It may suspend
    /// on an asynchronous operation; in that case the owner remains responsible
    /// for its lifetime and may call result() after done() becomes true.
    void start()
    {
        if (!handle_)
            throw std::logic_error("start() on an empty lazy coroutine");
        if (started_)
            throw std::logic_error("a lazy coroutine can only be started once");
        started_ = true;
        handle_.resume();
    }

    [[nodiscard]] auto done() const noexcept -> bool
    {
        return !handle_ || handle_.done();
    }

    [[nodiscard]] auto handle() const noexcept -> std::coroutine_handle<promise_type>
    {
        return handle_;
    }

    auto result() -> T
    {
        if (!handle_ || !handle_.done())
            throw std::logic_error("result() requires a completed lazy coroutine");
        return handle_.promise().result();
    }

    struct awaiter
    {
        std::coroutine_handle<promise_type> handle{};

        ~awaiter()
        {
            if (handle)
                handle.destroy();
        }

        auto await_ready() const noexcept -> bool
        {
            return false;
        }

        auto await_suspend(std::coroutine_handle<> continuation) noexcept -> std::coroutine_handle<>
        {
            handle.promise().set_continuation(continuation);
            return handle;
        }

        auto await_resume() -> T
        {
            return handle.promise().result();
        }
    };

    auto operator co_await() && -> awaiter
    {
        if (!handle_)
            throw std::logic_error("co_await on an empty lazy coroutine");
        if (started_)
            throw std::logic_error("a started lazy coroutine cannot be awaited");
        started_ = true;
        return {std::exchange(handle_, {})};
    }

    auto operator co_await() & = delete;

private:
    explicit lazy(std::coroutine_handle<promise_type> handle) noexcept
        : handle_(handle) {}

    std::coroutine_handle<promise_type> handle_{};
    bool started_{};
};

template <> class lazy<void>
{
public:
    struct promise_type : detail::lazy_promise_base
    {
        auto get_return_object() noexcept -> lazy
        {
            return lazy{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        void return_void() noexcept {}

        void result()
        {
            rethrow_if_exception();
        }
    };

    lazy() noexcept = default;

    ~lazy()
    {
        if (handle_)
            handle_.destroy();
    }

    lazy(const lazy&) = delete;
    auto operator=(const lazy&) -> lazy& = delete;

    lazy(lazy&& other) noexcept
        : handle_(std::exchange(other.handle_, {})), started_(std::exchange(other.started_, false)) {}

    auto operator=(lazy&& other) noexcept -> lazy&
    {
        if (this != &other)
        {
            if (handle_)
                handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
            started_ = std::exchange(other.started_, false);
        }
        return *this;
    }

    void start()
    {
        if (!handle_)
            throw std::logic_error("start() on an empty lazy coroutine");
        if (started_)
            throw std::logic_error("a lazy coroutine can only be started once");
        started_ = true;
        handle_.resume();
    }

    [[nodiscard]] auto done() const noexcept -> bool
    {
        return !handle_ || handle_.done();
    }

    [[nodiscard]] auto handle() const noexcept -> std::coroutine_handle<promise_type>
    {
        return handle_;
    }

    void result()
    {
        if (!handle_ || !handle_.done())
            throw std::logic_error("result() requires a completed lazy coroutine");
        handle_.promise().result();
    }

    struct awaiter
    {
        std::coroutine_handle<promise_type> handle{};

        ~awaiter()
        {
            if (handle)
                handle.destroy();
        }

        auto await_ready() const noexcept -> bool
        {
            return false;
        }

        auto await_suspend(std::coroutine_handle<> continuation) noexcept -> std::coroutine_handle<>
        {
            handle.promise().set_continuation(continuation);
            return handle;
        }

        void await_resume()
        {
            handle.promise().result();
        }
    };

    auto operator co_await() && -> awaiter
    {
        if (!handle_)
            throw std::logic_error("co_await on an empty lazy coroutine");
        if (started_)
            throw std::logic_error("a started lazy coroutine cannot be awaited");
        started_ = true;
        return {std::exchange(handle_, {})};
    }

    auto operator co_await() & = delete;

private:
    explicit lazy(std::coroutine_handle<promise_type> handle) noexcept
        : handle_(handle) {}

    std::coroutine_handle<promise_type> handle_{};
    bool started_{};
};

} // namespace cnetmod
