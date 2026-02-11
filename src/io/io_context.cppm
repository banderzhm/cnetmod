module;

#include <cnetmod/config.hpp>

export module cnetmod.io.io_context;

import std;
import cnetmod.core.error;

namespace cnetmod {

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

    /// 投递一个协程到事件循环执行（线程安全）
    void post(std::coroutine_handle<> h) {
        {
            std::lock_guard lock(post_mtx_);
            post_queue_.push_back(h);
        }
        wake();
    }

protected:
    io_context() = default;

    /// 平台特定：唤醒阻塞中的事件循环
    virtual void wake() = 0;

    /// 取出并恢复所有已投递的协程，供 run loop 调用
    auto drain_post_queue() -> std::size_t {
        std::vector<std::coroutine_handle<>> work;
        {
            std::lock_guard lock(post_mtx_);
            work.swap(post_queue_);
        }
        for (auto h : work)
            h.resume();
        return work.size();
    }

private:
    std::mutex post_mtx_;
    std::vector<std::coroutine_handle<>> post_queue_;
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
