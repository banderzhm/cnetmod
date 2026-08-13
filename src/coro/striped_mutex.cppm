export module cnetmod.coro.striped_mutex;

import std;
import cnetmod.coro.mutex;

namespace cnetmod {

/// Coroutine-friendly keyed exclusion with bounded memory. The same key always
/// selects the same asynchronous mutex. Contention suspends the coroutine
/// instead of blocking an I/O worker; unrelated keys normally select distinct
/// cache-line-isolated stripes.
export template <class Key, class Hash = std::hash<Key>>
class striped_async_mutex
{
private:
    struct alignas(64) stripe
    {
        async_mutex mutex;
    };

public:
    class scoped_lock
    {
    public:
        scoped_lock() = default;

        explicit scoped_lock(stripe& target) noexcept : target_(&target) {}

        ~scoped_lock()
        {
            unlock();
        }

        scoped_lock(const scoped_lock&) = delete;
        auto operator=(const scoped_lock&) -> scoped_lock& = delete;

        scoped_lock(scoped_lock&& other) noexcept : target_(std::exchange(other.target_, nullptr)) {}

        auto operator=(scoped_lock&& other) noexcept -> scoped_lock&
        {
            if (this != &other)
            {
                unlock();
                target_ = std::exchange(other.target_, nullptr);
            }
            return *this;
        }

        void unlock() noexcept
        {
            if (target_)
            {
                target_->mutex.unlock();
                target_ = nullptr;
            }
        }

    private:
        stripe* target_{};
    };

    class [[nodiscard]] lock_awaitable
    {
    public:
        explicit lock_awaitable(stripe& target) noexcept : target_(&target), awaitable_(target.mutex.lock()) {}

        [[nodiscard]] auto await_ready() noexcept -> bool
        {
            return awaitable_.await_ready();
        }

        auto await_suspend(std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>
        {
            return awaitable_.await_suspend(handle);
        }

        auto await_resume() noexcept -> scoped_lock
        {
            awaitable_.await_resume();
            return scoped_lock{*target_};
        }

    private:
        stripe* target_{};
        async_mutex::lock_awaitable awaitable_;
    };

    explicit striped_async_mutex(std::size_t stripe_count = 0U)
    {
        const auto requested = stripe_count == 0U ? std::thread::hardware_concurrency() * 4U : stripe_count;
        stripe_count_ = std::bit_ceil(std::max<std::size_t>(requested, 8U));
        stripes_ = std::make_unique<stripe[]>(stripe_count_);
    }

    [[nodiscard]] auto lock(const Key& key) noexcept -> lock_awaitable
    {
        return lock_awaitable{stripes_[hash_(key) & (stripe_count_ - 1U)]};
    }

    [[nodiscard]] auto stripe_count() const noexcept -> std::size_t
    {
        return stripe_count_;
    }

private:
    std::unique_ptr<stripe[]> stripes_;
    std::size_t stripe_count_{};
    [[no_unique_address]] Hash hash_{};
};

} // namespace cnetmod
