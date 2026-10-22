module cnetmod.coro.mutex;

import std;

namespace cnetmod {
void async_mutex::spinlock::lock() noexcept
{
    std::uint32_t expected = 0U;
    if (state_.compare_exchange_strong(expected, 1U, std::memory_order_acquire,
            std::memory_order_relaxed))
        return;
    // Advertise a sleeper before waiting; the exchange is a full barrier, so
    // an unlock that observes 1 cannot race past a contender about to sleep.
    if (expected != 2U)
        expected = state_.exchange(2U, std::memory_order_acquire);
    while (expected != 0U)
    {
        state_.wait(2U, std::memory_order_relaxed);
        expected = state_.exchange(2U, std::memory_order_acquire);
    }
}

void async_mutex::spinlock::unlock() noexcept
{
    if (state_.exchange(0U, std::memory_order_release) == 2U)
        state_.notify_one();
}

async_mutex::auto_lock::auto_lock(spinlock& lock) noexcept
    : lk_(lock)
{
    lk_.lock();
}

async_mutex::auto_lock::~auto_lock()
{
    lk_.unlock();
}

auto async_mutex::lock_awaitable::await_suspend(
    std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>
{
    node_.handle = handle;
    node_.event_loop = io_context::current();
    node_.next = nullptr;
    auto_lock guard(mtx_.lock_);
    // Marking the mutex contended under the queue lock makes the owner's
    // unlock take the queue path, which needs this same lock. If the owner
    // released in the meantime the exchange observes `unlocked` and this
    // coroutine owns the mutex (still marked contended, which only costs the
    // next unlock one queue check).
    if (mtx_.state_.exchange(contended, std::memory_order_acquire) == unlocked)
        return handle;
    if (!mtx_.tail_)
    {
        mtx_.head_ = mtx_.tail_ = &node_;
    }
    else
    {
        mtx_.tail_->next = &node_;
        mtx_.tail_ = &node_;
    }
    return std::noop_coroutine();
}

void async_mutex::unlock_contended() noexcept
{
    std::coroutine_handle<> handle;
    io_context* event_loop = nullptr;
    {
        auto_lock guard(lock_);
        if (head_)
        {
            // Ownership passes straight to the first waiter; the state stays
            // contended while others queue, and plain locked otherwise.
            auto* waiter = head_;
            head_ = waiter->next;
            if (!head_)
            {
                tail_ = nullptr;
                state_.store(locked, std::memory_order_release);
            }
            handle = waiter->handle;
            event_loop = waiter->event_loop;
        }
        else
        {
            state_.store(unlocked, std::memory_order_release);
        }
    }
    if (handle)
    {
        if (event_loop && !event_loop->running_in_this_thread())
            event_loop->post(handle);
        else
            handle.resume();
    }
}

async_lock_guard::async_lock_guard(async_lock_guard&& other) noexcept
    : mtx_(std::exchange(other.mtx_, nullptr))
{
}

auto async_lock_guard::operator=(async_lock_guard&& other) noexcept
    -> async_lock_guard&
{
    if (this != &other)
    {
        if (mtx_)
            mtx_->unlock();
        mtx_ = std::exchange(other.mtx_, nullptr);
    }
    return *this;
}
} // namespace cnetmod
