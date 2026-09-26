module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.channel;

import std;
import cnetmod.io.io_context;

namespace cnetmod {

// =============================================================================
// channel<T> — Bounded async channel
// =============================================================================
//
// Optimized design:
//  • Ring buffer: pre-allocated aligned slots, O(1) push/pop, zero per-send allocation
//  • Adaptive spinlock (atomic_flag + C++20 wait/notify): lightweight for short critical sections
//  • Direct handoff: bypasses buffer when sender meets waiting receiver (or vice versa)
//  • coroutine_handle-returning await_suspend: eliminates close() vs suspend race
//
// Invariants (hold while lock is held):
//  • count_ > 0        ⟹ recv_head_ == nullptr  (buffer non-empty → no waiting receivers)
//  • recv_head_ != nil ⟹ count_ == 0            (waiting receivers → buffer empty)
//  • send_head_ != nil ⟹ count_ == capacity_    (waiting senders → buffer full)
//  • close() does NOT drain buffer; subsequent receive() can still read remaining data
//
// Concurrency note:
//  The single spinlock is sufficient for coroutine workloads where critical sections
//  are a few pointer ops + value move.  For extreme MPMC contention (hundreds of
//  concurrent producers), a slot-based lock-free design (à la crossbeam) would scale
//  better, but adds significant complexity and per-slot cache-line overhead.
//

export template <typename T>
class channel
{
    static_assert(std::is_move_constructible_v<T>,
        "channel<T> requires T to be move constructible");

    // ---- Aligned slot for ring buffer (no default-construction required) ----

    struct alignas(T) slot_t
    {
        std::byte storage[sizeof(T)];

        auto ptr() noexcept -> T*
        {
            return std::launder(reinterpret_cast<T*>(storage));
        }

        template <typename U>
        void construct(U&& val)
        {
            std::construct_at(reinterpret_cast<T*>(storage), std::forward<U>(val));
        }

        void destroy() noexcept
        {
            std::destroy_at(ptr());
        }
    };

    // ---- Adaptive spinlock (atomic_flag + C++20 wait) ----

    class spinlock
    {
        std::atomic_flag flag_{};
        // Most channel operations are uncontended. Avoid entering libc++'s
        // global atomic-notify table on every unlock; a waiter announces
        // itself before sleeping, and an unlock only notifies when one exists.
        std::atomic<std::uint32_t> waiters_{};

    public:
        void lock() noexcept
        {
            if (!flag_.test_and_set(std::memory_order_acquire))
                return;
            waiters_.fetch_add(1U, std::memory_order_relaxed);
            do
            {
                flag_.wait(true, std::memory_order_relaxed);
            } while (flag_.test_and_set(std::memory_order_acquire));
            waiters_.fetch_sub(1U, std::memory_order_relaxed);
        }

        void unlock() noexcept
        {
            flag_.clear(std::memory_order_release);
            // If an unlock wins the tiny race before a contender increments
            // waiters_, that contender observes the cleared flag and retries
            // instead of sleeping, so no wakeup can be lost.
            if (waiters_.load(std::memory_order_relaxed) != 0U)
                flag_.notify_one();
        }
    };

    struct auto_lock
    {
        spinlock& lk_;

        explicit auto_lock(spinlock& lk) noexcept
            : lk_(lk)
        {
            lk_.lock();
        }

        ~auto_lock()
        {
            lk_.unlock();
        }

        auto_lock(const auto_lock&) = delete;
        auto operator=(const auto_lock&) -> auto_lock& = delete;
    };

    // ---- Intrusive waiter nodes ----

    struct waiter_node
    {
        std::coroutine_handle<> handle{};
        io_context* event_loop = nullptr;
        waiter_node* next = nullptr;
    };

    static void resume_waiter(waiter_node* waiter) noexcept
    {
        if (!waiter || !waiter->handle)
            return;
        if (waiter->event_loop &&
            !waiter->event_loop->running_in_this_thread())
            waiter->event_loop->post(waiter->handle);
        else
            waiter->handle.resume();
    }

    struct send_node : waiter_node
    {
        T value;
        bool succeeded = false;

        template <typename U>
        explicit send_node(U&& v)
            : value(std::forward<U>(v)) {}
    };

    struct recv_node : waiter_node
    {
        std::optional<T>* slot = nullptr;
    };

    // ---- Queue helpers ----

    static void enqueue(waiter_node*& head, waiter_node*& tail, waiter_node* n) noexcept
    {
        n->next = nullptr;
        if (!tail)
        {
            head = tail = n;
        }
        else
        {
            tail->next = n;
            tail = n;
        }
    }

    static auto dequeue(waiter_node*& head, waiter_node*& tail) noexcept -> waiter_node*
    {
        auto* n = head;
        if (n)
        {
            head = n->next;
            if (!head)
                tail = nullptr;
        }
        return n;
    }

    // ---- Ring buffer helpers ----

    void buf_push(T&& v) noexcept
    {
        slots_[write_pos_].construct(std::move(v));
        write_pos_ = next_pos(write_pos_);
        ++count_;
    }

    auto buf_pop() noexcept -> T
    {
        auto& s = slots_[read_pos_];
        T v = std::move(*s.ptr());
        s.destroy();
        read_pos_ = next_pos(read_pos_);
        --count_;
        return v;
    }

    [[nodiscard]] auto buf_empty() const noexcept -> bool
    {
        return count_ == 0;
    }

    [[nodiscard]] auto buf_full() const noexcept -> bool
    {
        return count_ == capacity_;
    }

    [[nodiscard]] auto next_pos(std::size_t pos) const noexcept -> std::size_t
    {
        return pos + 1 == capacity_ ? 0 : pos + 1;
    }

public:
    explicit channel(std::size_t capacity = 1)
        : capacity_(capacity), slots_(capacity > 0 ? std::make_unique<slot_t[]>(capacity) : nullptr) {}

    ~channel()
    {
        close();
        // Destroy remaining items in ring buffer
        while (count_ > 0)
        {
            slots_[read_pos_].destroy();
            read_pos_ = next_pos(read_pos_);
            --count_;
        }
    }

    channel(const channel&) = delete;
    auto operator=(const channel&) -> channel& = delete;

    // =========================================================================
    // send_awaitable
    // =========================================================================

    struct [[nodiscard]] send_awaitable
    {
        channel& ch_;
        send_node node_;

        send_awaitable(channel& ch, T value)
            : ch_(ch), node_(std::move(value)) {}

        auto await_ready() noexcept -> bool
        {
            waiter_node* to_resume = nullptr;
            {
                auto_lock g(ch_.lock_);
                if (ch_.closed_)
                    return true; // node_.succeeded stays false

                // Fast path 1: deliver directly to waiting receiver
                // Peek → move → commit-dequeue (state unchanged if move were to fail)
                if (ch_.recv_head_)
                {
                    auto* w = static_cast<recv_node*>(ch_.recv_head_);
                    *w->slot = std::move(node_.value);
                    dequeue(ch_.recv_head_, ch_.recv_tail_);
                    to_resume = w;
                    node_.succeeded = true;
                }
                // Fast path 2: buffer has space
                else if (ch_.capacity_ > 0 && !ch_.buf_full())
                {
                    ch_.buf_push(std::move(node_.value));
                    node_.succeeded = true;
                }
                // Slow path: must suspend
                else
                {
                    return false;
                }
            }
            resume_waiter(to_resume);
            return true;
        }

        auto await_suspend(std::coroutine_handle<> h) noexcept
            -> std::coroutine_handle<>
        {
            auto_lock g(ch_.lock_);
            // Re-check: channel may have been closed between ready() and suspend()
            if (ch_.closed_)
                return h; // resume self immediately
            node_.handle = h;
            node_.event_loop = io_context::current();
            enqueue(ch_.send_head_, ch_.send_tail_, &node_);
            return std::noop_coroutine(); // stay suspended
        }

        auto await_resume() noexcept -> bool
        {
            return node_.succeeded;
        }
    };

    // =========================================================================
    // recv_awaitable
    // =========================================================================

    struct [[nodiscard]] recv_awaitable
    {
        channel& ch_;
        std::optional<T> result_;
        recv_node node_;

        explicit recv_awaitable(channel& ch)
            : ch_(ch)
        {
            node_.slot = &result_;
        }

        auto await_ready() noexcept -> bool
        {
            waiter_node* to_resume = nullptr;
            {
                auto_lock g(ch_.lock_);

                // Fast path 1: buffer has data
                if (!ch_.buf_empty())
                {
                    result_.emplace(ch_.buf_pop());
                    // Refill buffer from waiting sender (peek → move → commit)
                    if (ch_.send_head_)
                    {
                        auto* w = static_cast<send_node*>(ch_.send_head_);
                        ch_.buf_push(std::move(w->value));
                        dequeue(ch_.send_head_, ch_.send_tail_);
                        w->succeeded = true;
                        to_resume = w;
                    }
                }
                // Fast path 2: buffer empty but sender waiting → direct handoff
                else if (ch_.send_head_)
                {
                    auto* w = static_cast<send_node*>(ch_.send_head_);
                    result_.emplace(std::move(w->value));
                    dequeue(ch_.send_head_, ch_.send_tail_);
                    w->succeeded = true;
                    to_resume = w;
                }
                // Closed and no data → nullopt
                else if (ch_.closed_)
                {
                    return true;
                }
                // Slow path: must suspend
                else
                {
                    return false;
                }
            }
            resume_waiter(to_resume);
            return true;
        }

        auto await_suspend(std::coroutine_handle<> h) noexcept
            -> std::coroutine_handle<>
        {
            auto_lock g(ch_.lock_);
            // Re-check: channel may have been closed between ready() and suspend()
            if (ch_.closed_)
                return h; // resume self, result_ is nullopt
            node_.handle = h;
            node_.event_loop = io_context::current();
            node_.slot = &result_;
            enqueue(ch_.recv_head_, ch_.recv_tail_, &node_);
            return std::noop_coroutine();
        }

        auto await_resume() noexcept -> std::optional<T>
        {
            return std::move(result_);
        }
    };

    // =========================================================================
    // Public API
    // =========================================================================

    /// co_await ch.send(value) — suspends when buffer is full, returns false if channel closed
    auto send(T value) -> send_awaitable
    {
        return send_awaitable{*this, std::move(value)};
    }

    /// Non-blocking send — returns false when the channel is closed or full.
    auto try_send(T value) -> bool
    {
        waiter_node* to_resume = nullptr;
        bool succeeded = false;
        {
            auto_lock g(lock_);
            if (closed_)
                return false;

            if (recv_head_)
            {
                auto* w = static_cast<recv_node*>(recv_head_);
                *w->slot = std::move(value);
                dequeue(recv_head_, recv_tail_);
                to_resume = w;
                succeeded = true;
            }
            else if (capacity_ > 0 && !buf_full())
            {
                buf_push(std::move(value));
                succeeded = true;
            }
        }
        resume_waiter(to_resume);
        return succeeded;
    }

    /// Non-blocking send from an existing object.
    /// The value is moved only when the send succeeds, which lets callers
    /// safely fall back to the suspending send path when the channel is full.
    auto try_send_move(T& value) -> bool
    {
        waiter_node* to_resume = nullptr;
        bool succeeded = false;
        {
            auto_lock g(lock_);
            if (closed_)
                return false;

            if (recv_head_)
            {
                auto* w = static_cast<recv_node*>(recv_head_);
                *w->slot = std::move(value);
                dequeue(recv_head_, recv_tail_);
                to_resume = w;
                succeeded = true;
            }
            else if (capacity_ > 0 && !buf_full())
            {
                buf_push(std::move(value));
                succeeded = true;
            }
        }
        resume_waiter(to_resume);
        return succeeded;
    }

    /// Non-blocking batch send from existing objects.
    /// Values are moved in order only while the send succeeds. Returns the
    /// number of values accepted before the channel became full or closed.
    auto try_send_many_move(std::span<T*> values) -> std::size_t
    {
        waiter_node* first_to_resume = nullptr;
        std::vector<waiter_node*> extra_to_resume;
        std::size_t sent = 0;
        {
            auto_lock g(lock_);
            if (closed_)
                return 0;

            while (sent < values.size() && recv_head_)
            {
                auto* w = static_cast<recv_node*>(recv_head_);
                *w->slot = std::move(*values[sent]);
                dequeue(recv_head_, recv_tail_);
                if (!first_to_resume)
                {
                    first_to_resume = w;
                }
                else
                {
                    extra_to_resume.push_back(w);
                }
                ++sent;
            }

            while (sent < values.size() && capacity_ > 0 && !buf_full())
            {
                buf_push(std::move(*values[sent]));
                ++sent;
            }
        }
        resume_waiter(first_to_resume);
        for (auto* waiter : extra_to_resume)
            resume_waiter(waiter);
        return sent;
    }

    /// co_await ch.receive() — suspends when empty, returns nullopt if closed and drained
    auto receive() -> recv_awaitable
    {
        return recv_awaitable{*this};
    }

    /// Non-blocking try_receive — returns nullopt immediately if no data available
    auto try_receive() noexcept -> std::optional<T>
    {
        waiter_node* to_resume = nullptr;
        std::optional<T> result;
        {
            auto_lock g(lock_);

            // Fast path 1: buffer has data
            if (!buf_empty())
            {
                result.emplace(buf_pop());
                // Refill buffer from waiting sender
                if (send_head_)
                {
                    auto* w = static_cast<send_node*>(send_head_);
                    buf_push(std::move(w->value));
                    dequeue(send_head_, send_tail_);
                    w->succeeded = true;
                    to_resume = w;
                }
            }
            // Fast path 2: buffer empty but sender waiting → direct handoff
            else if (send_head_)
            {
                auto* w = static_cast<send_node*>(send_head_);
                result.emplace(std::move(w->value));
                dequeue(send_head_, send_tail_);
                w->succeeded = true;
                to_resume = w;
            }
            // No data available — return immediately
        }
        resume_waiter(to_resume);
        return result;
    }

    /// Non-blocking receive from buffered items only.
    /// This preserves the usual buffer-refill invariant but does not direct
    /// handoff from a waiting sender when the buffer is empty. It is useful for
    /// opportunistic batching without pulling producers into the consumer's loop.
    auto try_receive_buffered_refill() noexcept -> std::optional<T>
    {
        waiter_node* to_resume = nullptr;
        std::optional<T> result;
        {
            auto_lock g(lock_);

            if (!buf_empty())
            {
                result.emplace(buf_pop());
                if (send_head_)
                {
                    auto* w = static_cast<send_node*>(send_head_);
                    buf_push(std::move(w->value));
                    dequeue(send_head_, send_tail_);
                    w->succeeded = true;
                    to_resume = w;
                }
            }
        }
        resume_waiter(to_resume);
        return result;
    }

    /// Drain up to max_items buffered values into out.
    /// This intentionally does not direct-handoff waiting senders into the
    /// returned batch. Freed slots are refilled after the drain and resumed
    /// producers continue in their own turn, which avoids recursive scheduling
    /// amplification in high-pressure writer actors.
    auto try_receive_many(std::vector<T>& out, std::size_t max_items) -> std::size_t
    {
        std::vector<waiter_node*> to_resume;
        std::size_t received = 0;
        {
            auto_lock g(lock_);

            const auto buffered = std::min(max_items, count_);
            while (received < buffered)
            {
                out.push_back(buf_pop());
                ++received;
            }

            while (send_head_ && capacity_ > 0 && !buf_full())
            {
                auto* w = static_cast<send_node*>(send_head_);
                buf_push(std::move(w->value));
                dequeue(send_head_, send_tail_);
                w->succeeded = true;
                to_resume.push_back(w);
            }
        }
        for (auto* waiter : to_resume)
            resume_waiter(waiter);
        return received;
    }

    /// Close channel — wakes all waiting senders/receivers, zero heap allocation
    void close() noexcept
    {
        waiter_node* sends = nullptr;
        waiter_node* recvs = nullptr;
        {
            auto_lock g(lock_);
            if (closed_)
                return;
            closed_ = true;
            // Steal waiter lists (walk them outside the lock)
            sends = send_head_;
            send_head_ = send_tail_ = nullptr;
            recvs = recv_head_;
            recv_head_ = recv_tail_ = nullptr;
        }
        // Resume all senders (succeeded stays false)
        for (auto* n = sends; n;)
        {
            auto* nx = n->next;
            resume_waiter(n);
            n = nx;
        }
        // Resume all receivers (result_ stays nullopt)
        for (auto* n = recvs; n;)
        {
            auto* nx = n->next;
            resume_waiter(n);
            n = nx;
        }
    }

    [[nodiscard]] auto is_closed() const noexcept -> bool
    {
        auto_lock g(lock_);
        return closed_;
    }

private:
    std::size_t capacity_;
    std::unique_ptr<slot_t[]> slots_;

    mutable spinlock lock_;
    bool closed_ = false;

    // Ring buffer state
    std::size_t read_pos_ = 0;
    std::size_t write_pos_ = 0;
    std::size_t count_ = 0;

    // Intrusive waiter queues
    waiter_node* send_head_ = nullptr;
    waiter_node* send_tail_ = nullptr;
    waiter_node* recv_head_ = nullptr;
    waiter_node* recv_tail_ = nullptr;
};

} // namespace cnetmod
