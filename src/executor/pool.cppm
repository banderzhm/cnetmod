module;

#include <cnetmod/config.hpp>
#include <exec/static_thread_pool.hpp>

export module cnetmod.executor.pool;

import std;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;

namespace cnetmod {

// =============================================================================
// pool_post_awaitable — 将当前协程切换到 stdexec 线程池执行
// =============================================================================
//
// 使用 stdexec 公共 API：schedule() + connect() + start()
// 协程挂起后由线程池线程恢复，实现 CPU 密集型工作的卸载
//
// 用法:
//   co_await pool_post_awaitable{pool};
//   // 此后运行在 pool 线程上
//   do_heavy_work();
//   co_await post_awaitable{io_ctx};
//   // 此后运行在 io_context 线程上

namespace detail {

/// 最小化的 stdexec receiver：恢复协程
struct coro_resume_receiver {
    using receiver_concept = stdexec::receiver_t;
    std::coroutine_handle<> coro;

    void set_value() noexcept { coro.resume(); }
    void set_stopped() noexcept { coro.resume(); }

    struct env {};
    auto get_env() const noexcept -> env { return {}; }
};

} // namespace detail

export struct pool_post_awaitable {
    exec::static_thread_pool& pool;

    using scheduler_t = exec::static_thread_pool::scheduler;
    using sender_t = decltype(std::declval<scheduler_t>().schedule());
    using op_t = decltype(stdexec::connect(
        std::declval<sender_t>(), std::declval<detail::coro_resume_receiver>()));

    // op_state 存储在协程帧上（awaitable 嵌入协程帧，挂起期间存活）
    alignas(op_t) std::byte storage_[sizeof(op_t)];

    explicit pool_post_awaitable(exec::static_thread_pool& p) noexcept : pool(p), storage_{} {}

    auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        auto sched = pool.get_scheduler();
        auto* op = new (storage_) op_t(
            stdexec::connect(sched.schedule(),
                             detail::coro_resume_receiver{h}));
        op->start();
    }

    void await_resume() noexcept {
        std::launder(reinterpret_cast<op_t*>(storage_))->~op_t();
    }
};

// =============================================================================
// spawn_on — 将协程投递到指定 io_context 执行（跨线程安全）
// =============================================================================
//
// 与 spawn(ctx, t) 语义相同，但明确用于跨线程场景。
// 通过 io_context::post() 将协程切换到目标事件循环线程。

export inline void spawn_on(io_context& target, task<void> t) {
    spawn(target, std::move(t));
}

// =============================================================================
// server_context — 多核服务器上下文
// =============================================================================
//
// 管理 accept 专用 io_context + N 个 worker io_context + stdexec 线程池
//
// 架构:
//   Thread 0 (main):  accept_io  — 运行 accept 循环
//   Thread 1..N:      worker_io  — 每线程一个 io_context，处理连接 I/O
//   exec::static_thread_pool:      可选的 CPU 密集型工作卸载
//
// IOCP 特性: accept 后的新 socket 未关联 IOCP，首次 async_read/write
// 在 worker io_context 上调用时自动关联到 worker 的 IOCP。

export class server_context {
public:
    /// @param workers worker 线程数（默认 = CPU 核心数）
    /// @param pool_threads stdexec 线程池大小（默认 = CPU 核心数）
    explicit server_context(
        unsigned workers = std::thread::hardware_concurrency(),
        unsigned pool_threads = std::thread::hardware_concurrency())
        : pool_(pool_threads)
    {
        if (workers == 0) workers = 1;
        if (pool_threads == 0) pool_threads = 1;

        accept_io_ = make_io_context();
        workers_.reserve(workers);
        for (unsigned i = 0; i < workers; ++i) {
            workers_.push_back(make_io_context());
        }
    }

    ~server_context() {
        stop();
    }

    // 不可复制或移动
    server_context(const server_context&) = delete;
    server_context(server_context&&) = delete;
    auto operator=(const server_context&) -> server_context& = delete;
    auto operator=(server_context&&) -> server_context& = delete;

    /// accept 专用 io_context
    [[nodiscard]] auto accept_io() noexcept -> io_context& {
        return *accept_io_;
    }

    /// 轮询选择下一个 worker io_context（原子 round-robin，线程安全）
    [[nodiscard]] auto next_worker_io() noexcept -> io_context& {
        auto idx = next_.fetch_add(1, std::memory_order_relaxed)
                   % workers_.size();
        return *workers_[idx];
    }

    /// worker 数量
    [[nodiscard]] auto worker_count() const noexcept -> unsigned {
        return static_cast<unsigned>(workers_.size());
    }

    /// stdexec 线程池
    [[nodiscard]] auto pool() noexcept -> exec::static_thread_pool& {
        return pool_;
    }

    /// 启动 worker 线程，然后在当前线程运行 accept_io
    /// 阻塞直到 stop()
    void run() {
        // 启动 worker 线程
        threads_.reserve(workers_.size());
        for (auto& w : workers_) {
            threads_.emplace_back([&w]() {
                w->run();
            });
        }

        // 当前线程运行 accept_io
        accept_io_->run();

        // accept_io 停止后，等待所有 worker 线程结束
        for (auto& t : threads_) {
            if (t.joinable())
                t.join();
        }
        threads_.clear();
    }

    /// 停止所有 io_context 和线程池
    void stop() {
        accept_io_->stop();
        for (auto& w : workers_) {
            w->stop();
        }
        pool_.request_stop();
    }

private:
    std::unique_ptr<io_context> accept_io_;
    std::vector<std::unique_ptr<io_context>> workers_;
    std::vector<std::jthread> threads_;
    exec::static_thread_pool pool_;
    std::atomic<std::size_t> next_{0};
};

} // namespace cnetmod
