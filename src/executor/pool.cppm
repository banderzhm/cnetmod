export module cnetmod.executor.pool;

import std;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;

namespace cnetmod {

// =============================================================================
// thread_pool — cnetmod-owned thread pool facade
// =============================================================================

/// The implementation is intentionally hidden in a .cpp file. This keeps
/// third-party executor implementation types out of the exported module BMI.
export class thread_pool
{
public:
    explicit thread_pool(unsigned thread_count = std::thread::hardware_concurrency());
    ~thread_pool();

    thread_pool(const thread_pool&) = delete;
    auto operator=(const thread_pool&) -> thread_pool& = delete;
    thread_pool(thread_pool&&) = delete;
    auto operator=(thread_pool&&) -> thread_pool& = delete;

    void request_stop() noexcept;

private:
    struct impl;
    std::unique_ptr<impl> impl_;

    auto prepare_resume(std::coroutine_handle<> coroutine) -> void*;
    void start_resume(void* operation) noexcept;
    void finish_resume(void* operation);

    friend struct pool_post_awaitable;
};

// =============================================================================
// pool_post_awaitable — Switch Current Coroutine to cnetmod Thread Pool
// =============================================================================
//
// Coroutine resumes on a pool thread after suspension, offloading CPU-heavy
// work while keeping implementation details out of the module interface.
//
// Usage:
//   co_await pool_post_awaitable{pool};
//   // Now running on pool thread
//   do_heavy_work();
//   co_await post_awaitable{io_ctx};
//   // Now running on io_context thread

export struct pool_post_awaitable
{
    thread_pool& pool;

    explicit pool_post_awaitable(thread_pool& value) noexcept;
    auto await_ready() const noexcept -> bool;
    void await_suspend(std::coroutine_handle<> coroutine);
    void await_resume();

private:
    void* operation_ = nullptr;
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
        std::exception_ptr scheduling_error;
        try
        {
            co_await pool_post_awaitable{pool};
        }
        catch (...)
        {
            scheduling_error = std::current_exception();
        }
        if (scheduling_error)
        {
            co_await post_awaitable{io};
            std::rethrow_exception(scheduling_error);
        }
        std::optional<R> result;
        std::exception_ptr error;
        try
        {
            result.emplace(fn());
        }
        catch (...)
        {
            error = std::current_exception();
        }
        co_await post_awaitable{io};
        if (error)
            std::rethrow_exception(error);
        co_return std::move(*result);
    }

    template <typename F>
    requires std::invocable<F> && std::is_void_v<std::invoke_result_t<F>>
    auto offload_impl(thread_pool& pool, io_context& io, F fn)
        -> task<void>
    {
        std::exception_ptr scheduling_error;
        try
        {
            co_await pool_post_awaitable{pool};
        }
        catch (...)
        {
            scheduling_error = std::current_exception();
        }
        if (scheduling_error)
        {
            co_await post_awaitable{io};
            std::rethrow_exception(scheduling_error);
        }
        std::exception_ptr error;
        try
        {
            fn();
        }
        catch (...)
        {
            error = std::current_exception();
        }
        co_await post_awaitable{io};
        if (error)
            std::rethrow_exception(error);
    }

} // namespace detail

// =============================================================================
// server_context — Multi-Core Server Context
// =============================================================================
//
// Manages accept-dedicated io_context + N worker io_contexts + CPU thread pool.
//
// Architecture:
//   Thread 0 (main):  accept_io  — Runs accept loop
//   Thread 1..N:      worker_io  — One io_context per thread, handles
//   connection I/O CPU pool:                      Optional CPU-intensive work
//   offload
//
// IOCP Feature: New socket after accept is not associated with IOCP, first
// async_read/write on worker io_context automatically associates with worker's
// IOCP.

export struct thread_affinity_options
{
    bool enabled{false};
    std::vector<unsigned> worker_processors;
    std::optional<unsigned> accept_processor;
};

export auto set_current_thread_affinity(unsigned processor) noexcept
    -> std::expected<void, std::error_code>;

export class server_context
{
public:
    /// @param workers Number of worker threads (default = CPU cores)
    /// @param pool_threads CPU thread pool size (default = CPU cores)
    explicit server_context(
        unsigned workers = std::thread::hardware_concurrency(),
        unsigned pool_threads = std::thread::hardware_concurrency(),
        thread_affinity_options affinity = {});

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

    /// cnetmod-owned thread pool
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
    thread_affinity_options affinity_;
    std::atomic<std::size_t> next_{0};
};

} // namespace cnetmod
