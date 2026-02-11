module;

#include <cnetmod/config.hpp>

export module cnetmod.io.io_context;

import std;
import cnetmod.core.error;

namespace cnetmod {

// =============================================================================
// post_node — lock-free MPSC 队列节点
// =============================================================================

/// 投递队列节点，支持两种模式：
/// 1) 协程模式：coroutine.resume()
/// 2) 回调模式：callback(callback_arg)
export struct post_node {
    std::atomic<post_node*> next{nullptr};
    std::coroutine_handle<> coroutine{};       // 模式 1
    void (*callback)(void*) = nullptr;         // 模式 2
    void* callback_arg = nullptr;
    bool heap_owned = false;                   // true = drain 后 delete

    void dispatch() noexcept {
        if (callback)
            callback(callback_arg);
        else if (coroutine)
            coroutine.resume();
    }
};

// =============================================================================
// I/O Context 抽象基类
// =============================================================================

/// I/O 执行上下文接口
/// 封装平台特定的 I/O 多路复用机制（IOCP/io_uring/epoll/kqueue）
export class io_context {
public:
    virtual ~io_context() = default;

    // 不可复制或移动（基类）
    io_context(const io_context&) = delete;
    io_context(io_context&&) = delete;
    auto operator=(const io_context&) -> io_context& = delete;
    auto operator=(io_context&&) -> io_context& = delete;

    /// 运行事件循环，阻塞直到停止
    virtual void run() = 0;

    /// 运行事件循环一次（处理就绪事件，然后返回）
    /// @return 处理的事件数量
    virtual auto run_one() -> std::size_t = 0;

    /// 轮询就绪事件（非阻塞）
    /// @return 处理的事件数量
    virtual auto poll() -> std::size_t = 0;

    /// 停止事件循环
    virtual void stop() = 0;

    /// 是否已停止
    [[nodiscard]] virtual auto stopped() const noexcept -> bool = 0;

    /// 重置 context（停止后可重新运行）
    virtual void restart() = 0;

    /// 投递一个协程到事件循环执行（线程安全，lock-free）
    void post(std::coroutine_handle<> h) {
        auto* node = new post_node{};
        node->coroutine = h;
        node->heap_owned = true;
        push_node(node);
        wake();
    }

    /// 投递一个回调到事件循环执行（线程安全，lock-free，零协程开销）
    void post(void (*fn)(void*), void* arg) {
        auto* node = new post_node{};
        node->callback = fn;
        node->callback_arg = arg;
        node->heap_owned = true;
        push_node(node);
        wake();
    }

    /// 投递一个已构造的 post_node（调用者拥有 node 生命周期，drain 后不 delete）
    void post_node_raw(post_node* node) {
        node->next.store(nullptr, std::memory_order_relaxed);
        push_node_no_delete(node);
        wake();
    }

protected:
    io_context() = default;

    /// 平台特定：唤醒阻塞中的事件循环
    virtual void wake() = 0;

    /// 取出并执行所有已投递的任务，供 run loop 调用
    auto drain_post_queue() -> std::size_t {
        // 原子取走整条链（MPSC：多生产者单消费者）
        auto* chain = post_head_.exchange(nullptr, std::memory_order_acquire);
        if (!chain)
            return 0;

        // 反转链表（push 顺序是 LIFO，需要 FIFO 执行）
        post_node* reversed = nullptr;
        while (chain) {
            auto* next = chain->next.load(std::memory_order_relaxed);
            chain->next.store(reversed, std::memory_order_relaxed);
            reversed = chain;
            chain = next;
        }

        // 逐个 dispatch 并释放
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
    /// MPSC push：atomic exchange + store next
    void push_node(post_node* node) {
        node->next.store(nullptr, std::memory_order_relaxed);
        auto* prev = post_head_.exchange(node, std::memory_order_acq_rel);
        if (prev)
            node->next.store(prev, std::memory_order_relaxed);
    }

    /// 同 push_node，但标记为不由 drain 释放
    void push_node_no_delete(post_node* node) {
        auto* prev = post_head_.exchange(node, std::memory_order_acq_rel);
        if (prev)
            node->next.store(prev, std::memory_order_relaxed);
    }

    std::atomic<post_node*> post_head_{nullptr};
};

/// co_await post_awaitable{ctx} — 将当前协程切换到 io_context 事件循环执行
export struct post_awaitable {
    io_context& ctx;
    auto await_ready() const noexcept -> bool { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept { ctx.post(h); }
    void await_resume() noexcept {}
};

/// 创建平台默认的 io_context
export [[nodiscard]] auto make_io_context()
    -> std::unique_ptr<io_context>;

} // namespace cnetmod
