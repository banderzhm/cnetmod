module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.channel;

import std;

namespace cnetmod {

// =============================================================================
// channel<T> — Bounded async MPSC channel
// =============================================================================

export template <typename T>
class channel {
    struct waiter_node {
        std::coroutine_handle<> handle{};
        waiter_node* next = nullptr;
    };

    struct send_node : waiter_node {
        T value;
        template <typename U>
        explicit send_node(U&& v) : value(std::forward<U>(v)) {}
    };

    struct recv_node : waiter_node {
        std::optional<T>* slot = nullptr;  // Write receive result
    };

public:
    explicit channel(std::size_t capacity = 1)
        : capacity_(capacity) {}

    ~channel() { close(); }

    channel(const channel&) = delete;
    auto operator=(const channel&) -> channel& = delete;

    // ---- send awaitable ----

    struct [[nodiscard]] send_awaitable {
        channel& ch_;
        T value_;
        send_node node_;
        bool sent_ = false;

        send_awaitable(channel& ch, T value)
            : ch_(ch), value_(std::move(value)), node_(T{}) {}

        auto await_ready() noexcept -> bool {
            std::coroutine_handle<> to_resume;
            {
                std::lock_guard lock(ch_.mtx_);
                if (ch_.closed_) { sent_ = false; return true; }

                // If there's a waiting receiver, deliver directly
                if (ch_.recv_head_) {
                    auto* w = static_cast<recv_node*>(ch_.recv_head_);
                    ch_.recv_head_ = w->next;
                    if (!ch_.recv_head_) ch_.recv_tail_ = nullptr;
                    *w->slot = std::move(value_);
                    to_resume = w->handle;
                    sent_ = true;
                } else if (ch_.buffer_.size() < ch_.capacity_) {
                    ch_.buffer_.push_back(std::move(value_));
                    sent_ = true;
                } else {
                    return false;  // Need to suspend
                }
            }
            if (to_resume) to_resume.resume();
            return true;
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            std::lock_guard lock(ch_.mtx_);
            node_.value = std::move(value_);
            node_.handle = h;
            node_.next = nullptr;
            if (!ch_.send_tail_) {
                ch_.send_head_ = ch_.send_tail_ = &node_;
            } else {
                ch_.send_tail_->next = &node_;
                ch_.send_tail_ = &node_;
            }
        }

        /// Returns true if send succeeded, false if channel is closed
        auto await_resume() noexcept -> bool { return sent_ || !ch_.closed_; }
    };

    // ---- receive awaitable ----

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
                std::lock_guard lock(ch_.mtx_);
                if (!ch_.buffer_.empty()) {
                    result_.emplace(std::move(ch_.buffer_.front()));
                    ch_.buffer_.pop_front();
                    // Wake up waiting send coroutine, enqueue its value
                    if (ch_.send_head_) {
                        auto* w = static_cast<send_node*>(ch_.send_head_);
                        ch_.send_head_ = w->next;
                        if (!ch_.send_head_) ch_.send_tail_ = nullptr;
                        ch_.buffer_.push_back(std::move(w->value));
                        to_resume = w->handle;
                    }
                    // Unlock before resume
                } else if (ch_.send_head_) {
                    // Buffer is empty but sender is waiting, take value directly
                    auto* w = static_cast<send_node*>(ch_.send_head_);
                    ch_.send_head_ = w->next;
                    if (!ch_.send_head_) ch_.send_tail_ = nullptr;
                    result_.emplace(std::move(w->value));
                    to_resume = w->handle;
                } else if (ch_.closed_) {
                    return true;  // Closed and no data, result_ is nullopt
                } else {
                    return false; // Need to suspend
                }
            }
            if (to_resume) to_resume.resume();
            return true;
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            std::lock_guard lock(ch_.mtx_);
            node_.handle = h;
            node_.slot = &result_;
            node_.next = nullptr;
            if (!ch_.recv_tail_) {
                ch_.recv_head_ = ch_.recv_tail_ = &node_;
            } else {
                ch_.recv_tail_->next = &node_;
                ch_.recv_tail_ = &node_;
            }
        }

        /// Returns value, or nullopt if channel is closed and has no data
        auto await_resume() noexcept -> std::optional<T> {
            return std::move(result_);
        }
    };

    /// co_await ch.send(value) — Suspends when full
    auto send(T value) -> send_awaitable {
        return send_awaitable{*this, std::move(value)};
    }

    /// co_await ch.receive() — Suspends when empty
    auto receive() -> recv_awaitable {
        return recv_awaitable{*this};
    }

    /// Close channel
    void close() noexcept {
        std::vector<std::coroutine_handle<>> to_resume;
        {
            std::lock_guard lock(mtx_);
            if (closed_) return;
            closed_ = true;
            while (send_head_) {
                auto* w = send_head_;
                send_head_ = w->next;
                if (w->handle) to_resume.push_back(w->handle);
            }
            send_tail_ = nullptr;
            while (recv_head_) {
                auto* w = recv_head_;
                recv_head_ = w->next;
                if (w->handle) to_resume.push_back(w->handle);
            }
            recv_tail_ = nullptr;
        }
        for (auto h : to_resume) h.resume();
    }

    [[nodiscard]] auto is_closed() const noexcept -> bool {
        std::lock_guard lock(mtx_);
        return closed_;
    }

private:
    std::size_t capacity_;
    std::deque<T> buffer_;
    mutable std::mutex mtx_;
    bool closed_ = false;

    waiter_node* send_head_ = nullptr;
    waiter_node* send_tail_ = nullptr;
    waiter_node* recv_head_ = nullptr;
    waiter_node* recv_tail_ = nullptr;
};

} // namespace cnetmod
