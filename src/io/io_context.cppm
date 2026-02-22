module;

#include <cnetmod/config.hpp>

export module cnetmod.io.io_context;

import std;
import cnetmod.core.error;

namespace cnetmod {

// =============================================================================
// post_node — lock-free MPSC queue node
// =============================================================================

/// Post queue node, supports two modes:
/// 1) Coroutine mode: coroutine.resume()
/// 2) Callback mode: callback(callback_arg)
export struct post_node {
    std::atomic<post_node*> next{nullptr};
    std::coroutine_handle<> coroutine{};       // Mode 1
    void (*callback)(void*) = nullptr;         // Mode 2
    void* callback_arg = nullptr;
    bool heap_owned = false;                   // true = delete after drain

    void dispatch() noexcept {
        if (callback)
            callback(callback_arg);
        else if (coroutine)
            coroutine.resume();
    }
};

// =============================================================================
// I/O Context abstract base class
// =============================================================================

/// I/O execution context interface
/// Encapsulates platform-specific I/O multiplexing mechanisms (IOCP/io_uring/epoll/kqueue)
export class io_context {
public:
    virtual ~io_context() = default;

    // Non-copyable or movable (base class)
    io_context(const io_context&) = delete;
    io_context(io_context&&) = delete;
    auto operator=(const io_context&) -> io_context& = delete;
    auto operator=(io_context&&) -> io_context& = delete;

    /// Run event loop, blocks until stopped
    virtual void run() = 0;

    /// Run event loop once (process ready events, then return)
    /// @return Number of events processed
    virtual auto run_one() -> std::size_t = 0;

    /// Poll ready events (non-blocking)
    /// @return Number of events processed
    virtual auto poll() -> std::size_t = 0;

    /// Stop event loop
    virtual void stop() = 0;

    /// Whether stopped
    [[nodiscard]] virtual auto stopped() const noexcept -> bool = 0;

    /// Reset context (can run again after stop)
    virtual void restart() = 0;

    /// Post a coroutine to event loop for execution (thread-safe, lock-free)
    void post(std::coroutine_handle<> h) {
        auto* node = new post_node{};
        node->coroutine = h;
        node->heap_owned = true;
        push_node(node);
        wake();
    }

    /// Post a callback to event loop for execution (thread-safe, lock-free, zero coroutine overhead)
    void post(void (*fn)(void*), void* arg) {
        auto* node = new post_node{};
        node->callback = fn;
        node->callback_arg = arg;
        node->heap_owned = true;
        push_node(node);
        wake();
    }

    /// Post a pre-constructed post_node (caller owns node lifetime, not deleted after drain)
    void post_node_raw(post_node* node) {
        node->next.store(nullptr, std::memory_order_relaxed);
        push_node_no_delete(node);
        wake();
    }

protected:
    io_context() = default;

    /// Platform-specific: wake blocked event loop
    virtual void wake() = 0;

    /// Retrieve and execute all posted tasks, called by run loop
    auto drain_post_queue() -> std::size_t {
        // Atomically take entire chain (MPSC: multi-producer single-consumer)
        auto* chain = post_head_.exchange(nullptr, std::memory_order_acquire);
        if (!chain)
            return 0;

        // Reverse list (push order is LIFO, need FIFO execution)
        post_node* reversed = nullptr;
        while (chain) {
            auto* next = chain->next.load(std::memory_order_relaxed);
            chain->next.store(reversed, std::memory_order_relaxed);
            reversed = chain;
            chain = next;
        }

        // Dispatch and release one by one
        std::size_t count = 0;
        while (reversed) {
            auto* node = reversed;
            reversed = node->next.load(std::memory_order_relaxed);
            node->dispatch();
            if (node->heap_owned)
                delete node;
            ++count;
        }
        return count;
    }

private:
    /// MPSC push: atomic exchange + store next
    void push_node(post_node* node) {
        node->next.store(nullptr, std::memory_order_relaxed);
        auto* prev = post_head_.exchange(node, std::memory_order_acq_rel);
        if (prev)
            node->next.store(prev, std::memory_order_relaxed);
    }

    /// Same as push_node, but marked as not released by drain
    void push_node_no_delete(post_node* node) {
        auto* prev = post_head_.exchange(node, std::memory_order_acq_rel);
        if (prev)
            node->next.store(prev, std::memory_order_relaxed);
    }

    std::atomic<post_node*> post_head_{nullptr};
};

/// co_await post_awaitable{ctx} — Switch current coroutine to io_context event loop execution
export struct post_awaitable {
    io_context& ctx;
    auto await_ready() const noexcept -> bool { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept { ctx.post(h); }
    void await_resume() noexcept {}
};

/// Create platform default io_context
export [[nodiscard]] auto make_io_context()
    -> std::unique_ptr<io_context>;

} // namespace cnetmod
