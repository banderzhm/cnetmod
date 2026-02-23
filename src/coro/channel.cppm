module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.channel;

import std;

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
class channel {
    static_assert(std::is_move_constructible_v<T>,
        "channel<T> requires T to be move constructible");

    // ---- Aligned slot for ring buffer (no default-construction required) ----

    struct alignas(T) slot_t {
        std::byte storage[sizeof(T)];

        auto ptr() noexcept -> T* {
            return std::launder(reinterpret_cast<T*>(storage));
        }

        template <typename U>
        void construct(U&& val) {
            std::construct_at(reinterpret_cast<T*>(storage), std::forward<U>(val));
        }

        void destroy() noexcept { std::destroy_at(ptr()); }
    };

    // ---- Adaptive spinlock (atomic_flag + C++20 wait) ----

    class spinlock {
        std::atomic_flag flag_{};
    public:
        void lock() noexcept {
            if (!flag_.test_and_set(std::memory_order_acquire)) return;
            do {
                flag_.wait(true, std::memory_order_relaxed);
            } while (flag_.test_and_set(std::memory_order_acquire));
        }
        void unlock() noexcept {
            flag_.clear(std::memory_order_release);
            flag_.notify_one();
        }
    };

    struct auto_lock {
        spinlock& lk_;
        explicit auto_lock(spinlock& lk) noexcept : lk_(lk) { lk_.lock(); }
        ~auto_lock() { lk_.unlock(); }
        auto_lock(const auto_lock&) = delete;
        auto operator=(const auto_lock&) -> auto_lock& = delete;
    };

    // ---- Intrusive waiter nodes ----

    struct waiter_node {
        std::coroutine_handle<> handle{};
        waiter_node* next = nullptr;
    };

    struct send_node : waiter_node {
        T value;
        bool succeeded = false;

        template <typename U>
        explicit send_node(U&& v) : value(std::forward<U>(v)) {}
    };

    struct recv_node : waiter_node {
        std::optional<T>* slot = nullptr;
    };

    // ---- Queue helpers ----

    static void enqueue(waiter_node*& head, waiter_node*& tail, waiter_node* n) noexcept {
        n->next = nullptr;
        if (!tail) { head = tail = n; }
        else       { tail->next = n; tail = n; }
    }

    static auto dequeue(waiter_node*& head, waiter_node*& tail) noexcept -> waiter_node* {
        auto* n = head;
        if (n) { head = n->next; if (!head) tail = nullptr; }
        return n;
    }

    // ---- Ring buffer helpers ----

    void buf_push(T&& v) noexcept {
        slots_[write_pos_].construct(std::move(v));
        write_pos_ = next_pos(write_pos_);
        ++count_;
    }

    auto buf_pop() noexcept -> T {
        auto& s = slots_[read_pos_];
        T v = std::move(*s.ptr());
        s.destroy();
        read_pos_ = next_pos(read_pos_);
        --count_;
        return v;
    }

    [[nodiscard]] auto buf_empty() const noexcept -> bool { return count_ == 0; }
    [[nodiscard]] auto buf_full()  const noexcept -> bool { return count_ == capacity_; }

    [[nodiscard]] auto next_pos(std::size_t pos) const noexcept -> std::size_t {
        return pos + 1 == capacity_ ? 0 : pos + 1;
    }

public:
    explicit channel(std::size_t capacity = 1)
        : capacity_(capacity)
        , slots_(capacity > 0 ? std::make_unique<slot_t[]>(capacity) : nullptr) {}

    ~channel() {
        close();
        // Destroy remaining items in ring buffer
        while (count_ > 0) {
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

    struct [[nodiscard]] send_awaitable {
        channel& ch_;
        send_node node_;

        send_awaitable(channel& ch, T value)
            : ch_(ch), node_(std::move(value)) {}

        auto await_ready() noexcept -> bool {
            std::coroutine_handle<> to_resume;
            {
                auto_lock g(ch_.lock_);
                if (ch_.closed_) return true;  // node_.succeeded stays false

                // Fast path 1: deliver directly to waiting receiver
                // Peek → move → commit-dequeue (state unchanged if move were to fail)
                if (ch_.recv_head_) {
                    auto* w = static_cast<recv_node*>(ch_.recv_head_);
                    *w->slot = std::move(node_.value);
                    dequeue(ch_.recv_head_, ch_.recv_tail_);
                    to_resume = w->handle;
                    node_.succeeded = true;
                }
                // Fast path 2: buffer has space
                else if (ch_.capacity_ > 0 && !ch_.buf_full()) {
                    ch_.buf_push(std::move(node_.value));
                    node_.succeeded = true;
                }
                // Slow path: must suspend
                else {
                    return false;
                }
            }
            if (to_resume) to_resume.resume();
            return true;
        }

        auto await_suspend(std::coroutine_handle<> h) noexcept
            -> std::coroutine_handle<>
        {
            auto_lock g(ch_.lock_);
            // Re-check: channel may have been closed between ready() and suspend()
            if (ch_.closed_) return h;  // resume self immediately
            node_.handle = h;
            enqueue(ch_.send_head_, ch_.send_tail_, &node_);
            return std::noop_coroutine();  // stay suspended
        }

        auto await_resume() noexcept -> bool { return node_.succeeded; }
    };

    // =========================================================================
    // recv_awaitable
    // =========================================================================

    struct [[nodiscard]] recv_awaitable {
        channel& ch_;
        std::optional<T> result_;
        recv_node node_;

        explicit recv_awaitable(channel& ch) : ch_(ch) {
            node_.slot = &result_;
        }

        auto await_ready() noexcept -> bool {
            std::coroutine_handle<> to_resume;
            {
                auto_lock g(ch_.lock_);

                // Fast path 1: buffer has data
                if (!ch_.buf_empty()) {
                    result_.emplace(ch_.buf_pop());
                    // Refill buffer from waiting sender (peek → move → commit)
                    if (ch_.send_head_) {
                        auto* w = static_cast<send_node*>(ch_.send_head_);
                        ch_.buf_push(std::move(w->value));
                        dequeue(ch_.send_head_, ch_.send_tail_);
                        w->succeeded = true;
                        to_resume = w->handle;
                    }
                }
                // Fast path 2: buffer empty but sender waiting → direct handoff
                else if (ch_.send_head_) {
                    auto* w = static_cast<send_node*>(ch_.send_head_);
                    result_.emplace(std::move(w->value));
                    dequeue(ch_.send_head_, ch_.send_tail_);
                    w->succeeded = true;
                    to_resume = w->handle;
                }
                // Closed and no data → nullopt
                else if (ch_.closed_) {
                    return true;
                }
                // Slow path: must suspend
                else {
                    return false;
                }
            }
            if (to_resume) to_resume.resume();
            return true;
        }

        auto await_suspend(std::coroutine_handle<> h) noexcept
            -> std::coroutine_handle<>
        {
            auto_lock g(ch_.lock_);
            // Re-check: channel may have been closed between ready() and suspend()
            if (ch_.closed_) return h;  // resume self, result_ is nullopt
            node_.handle = h;
            node_.slot = &result_;
            enqueue(ch_.recv_head_, ch_.recv_tail_, &node_);
            return std::noop_coroutine();
        }

        auto await_resume() noexcept -> std::optional<T> {
            return std::move(result_);
        }
    };

    // =========================================================================
    // Public API
    // =========================================================================

    /// co_await ch.send(value) — suspends when buffer is full, returns false if channel closed
    auto send(T value) -> send_awaitable {
        return send_awaitable{*this, std::move(value)};
    }

    /// co_await ch.receive() — suspends when empty, returns nullopt if closed and drained
    auto receive() -> recv_awaitable {
        return recv_awaitable{*this};
    }

    /// Non-blocking try_receive — returns nullopt immediately if no data available
    auto try_receive() noexcept -> std::optional<T> {
        std::coroutine_handle<> to_resume;
        std::optional<T> result;
        {
            auto_lock g(lock_);

            // Fast path 1: buffer has data
            if (!buf_empty()) {
                result.emplace(buf_pop());
                // Refill buffer from waiting sender
                if (send_head_) {
                    auto* w = static_cast<send_node*>(send_head_);
                    buf_push(std::move(w->value));
                    dequeue(send_head_, send_tail_);
                    w->succeeded = true;
                    to_resume = w->handle;
                }
            }
            // Fast path 2: buffer empty but sender waiting → direct handoff
            else if (send_head_) {
                auto* w = static_cast<send_node*>(send_head_);
                result.emplace(std::move(w->value));
                dequeue(send_head_, send_tail_);
                w->succeeded = true;
                to_resume = w->handle;
            }
            // No data available — return immediately
        }
        if (to_resume) to_resume.resume();
        return result;
    }

    /// Close channel — wakes all waiting senders/receivers, zero heap allocation
    void close() noexcept {
        waiter_node* sends = nullptr;
        waiter_node* recvs = nullptr;
        {
            auto_lock g(lock_);
            if (closed_) return;
            closed_ = true;
            // Steal waiter lists (walk them outside the lock)
            sends = send_head_; send_head_ = send_tail_ = nullptr;
            recvs = recv_head_; recv_head_ = recv_tail_ = nullptr;
        }
        // Resume all senders (succeeded stays false)
        for (auto* n = sends; n;) {
            auto* nx = n->next;
            if (n->handle) n->handle.resume();
            n = nx;
        }
        // Resume all receivers (result_ stays nullopt)
        for (auto* n = recvs; n;) {
            auto* nx = n->next;
            if (n->handle) n->handle.resume();
            n = nx;
        }
    }

    [[nodiscard]] auto is_closed() const noexcept -> bool {
        auto_lock g(lock_);
        return closed_;
    }

private:
    std::size_t capacity_;
    std::unique_ptr<slot_t[]> slots_;

    mutable spinlock lock_;
    bool closed_ = false;

    // Ring buffer state
    std::size_t read_pos_  = 0;
    std::size_t write_pos_ = 0;
    std::size_t count_     = 0;

    // Intrusive waiter queues
    waiter_node* send_head_ = nullptr;
    waiter_node* send_tail_ = nullptr;
    waiter_node* recv_head_ = nullptr;
    waiter_node* recv_tail_ = nullptr;
};

} // namespace cnetmod
