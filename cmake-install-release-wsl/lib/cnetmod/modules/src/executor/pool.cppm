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
// thread_pool — cnetmod 统一线程池类型
// =============================================================================
//
// 对外导出唯一线程池类型别名。下游代码只依赖 cnetmod::thread_pool，
// 不直接引用 exec::static_thread_pool。后续 stdexec 升级只需改此处。

export using thread_pool = exec::static_thread_pool;

// =============================================================================
// pool_post_awaitable — Switch Current Coroutine to stdexec Thread Pool
// =============================================================================
//
// Uses stdexec public API: schedule() + connect() + start()
// Coroutine resumes on thread pool thread after suspension, offloading
// CPU-intensive work
//
// Usage:
//   co_await pool_post_awaitable{pool};
//   // Now running on pool thread
//   do_heavy_work();
//   co_await post_awaitable{io_ctx};
//   // Now running on io_context thread

namespace detail {

    /// Minimal stdexec receiver: resume coroutine
    struct coro_resume_receiver
    {
        using receiver_concept = stdexec::receiver_t;
        std::coroutine_handle<> coro;

        void set_value() noexcept
        {
            coro.resume();
        }

        void set_stopped() noexcept
        {
            coro.resume();
        }

        struct env
        {
        };

        auto get_env() const noexcept -> env
        {
            return {};
        }
    };

} // namespace detail

export struct pool_post_awaitable
{
    thread_pool& pool;

    using scheduler_t = thread_pool::scheduler;
    using sender_t = decltype(std::declval<scheduler_t>().schedule());
    using op_t = decltype(stdexec::connect(
        std::declval<sender_t>(), std::declval<detail::coro_resume_receiver>()));

    // op_state stored on coroutine frame (awaitable embedded in frame, alive
    // during suspension)
    alignas(op_t) std::byte storage_[sizeof(op_t)];

    explicit pool_post_awaitable(thread_pool& p) noexcept
        : pool(p), storage_{} {}

    auto await_ready() const noexcept -> bool
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept
    {
        auto sched = pool.get_scheduler();
        auto* op = new (storage_) op_t(
            stdexec::connect(sched.schedule(), detail::coro_resume_receiver{h}));
        op->start();
    }

    void await_resume() noexcept
    {
        std::launder(reinterpret_cast<op_t*>(storage_))->~op_t();
    }
};

// =============================================================================
// spawn_on — Post Coroutine to Specified io_context (cross-thread safe)
// =============================================================================
//
// Same semantics as spawn(ctx, t), but explicitly for cross-thread scenarios.
// Switches coroutine to target event loop thread via io_context::post().

export void spawn_on(io_context& target, task<void> t);

// =============================================================================
// detail::offload_impl — Coroutine implementation for server_context::offload
// =============================================================================

namespace detail {

    template <typename F>
    requires std::invocable<F> && (!std::is_void_v<std::invoke_result_t<F>>)
    auto offload_impl(thread_pool& pool, io_context& io, F fn)
        -> task<std::invoke_result_t<F>>
    {
        using R = std::invoke_result_t<F>;
        co_await pool_post_awaitable{pool};
        R result = fn();
        co_await post_awaitable{io};
        co_return std::move(result);
    }

    template <typename F>
    requires std::invocable<F> && std::is_void_v<std::invoke_result_t<F>>
    auto offload_impl(thread_pool& pool, io_context& io, F fn)
        -> task<void>
    {
        co_await pool_post_awaitable{pool};
        fn();
        co_await post_awaitable{io};
    }

} // namespace detail

// =============================================================================
// server_context — Multi-Core Server Context
// =============================================================================
//
// Manages accept-dedicated io_context + N worker io_contexts + stdexec thread
// pool
//
// Architecture:
//   Thread 0 (main):  accept_io  — Runs accept loop
//   Thread 1..N:      worker_io  — One io_context per thread, handles
//   connection I/O exec::static_thread_pool:      Optional CPU-intensive work
//   offload
//
// IOCP Feature: New socket after accept is not associated with IOCP, first
// async_read/write on worker io_context automatically associates with worker's
// IOCP.

export class server_context
{
public:
    /// @param workers Number of worker threads (default = CPU cores)
    /// @param pool_threads stdexec thread pool size (default = CPU cores)
    explicit server_context(
        unsigned workers = std::thread::hardware_concurrency(),
        unsigned pool_threads = std::thread::hardware_concurrency());

    ~server_context();

    // Non-copyable and non-movable
    server_context(const server_context&) = delete;
    server_context(server_context&&) = delete;
    auto operator=(const server_context&) -> server_context& = delete;
    auto operator=(server_context&&) -> server_context& = delete;

    /// Accept-dedicated io_context
    [[nodiscard]] auto accept_io() noexcept -> io_context&;

    /// Round-robin select next worker io_context (atomic, thread-safe)
    [[nodiscard]] auto next_worker_io() noexcept -> io_context&;

    /// Worker count
    [[nodiscard]] auto worker_count() const noexcept -> unsigned;

    /// Return all worker io_context pointers
    [[nodiscard]] auto worker_ios() -> std::vector<io_context*>;

    /// cnetmod thread pool (type alias shields downstream from stdexec)
    [[nodiscard]] auto pool() noexcept -> thread_pool&;

    /// Offload a blocking callable to the thread pool, then switch back to
    /// the given io_context thread. Usage:
    ///   auto r = co_await ctx.offload(io, [&] { return blocking_call(); });
    template <typename F>
    requires std::invocable<std::decay_t<F>>
    auto offload(io_context& return_to, F&& fn)
    {
        return detail::offload_impl(
            pool_, return_to, std::decay_t<F>(std::forward<F>(fn)));
    }

    /// Spawn a coroutine on the next worker io_context (round-robin,
    /// thread-safe).
    void spawn_next(task<void> t);

    /// Start worker threads, then run accept_io on current thread
    /// Blocks until stop()
    void run();

    /// Stop all io_context and thread pool
    void stop();

private:
    std::unique_ptr<io_context> accept_io_;
    std::vector<std::unique_ptr<io_context>> workers_;
    std::vector<std::jthread> threads_;
    thread_pool pool_;
    std::atomic<std::size_t> next_{0};
};

} // namespace cnetmod
