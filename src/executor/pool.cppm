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
// pool_post_awaitable — Switch Current Coroutine to stdexec Thread Pool
// =============================================================================
//
// Uses stdexec public API: schedule() + connect() + start()
// Coroutine resumes on thread pool thread after suspension, offloading CPU-intensive work
//
// Usage:
//   co_await pool_post_awaitable{pool};
//   // Now running on pool thread
//   do_heavy_work();
//   co_await post_awaitable{io_ctx};
//   // Now running on io_context thread

namespace detail {

/// Minimal stdexec receiver: resume coroutine
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

    // op_state stored on coroutine frame (awaitable embedded in frame, alive during suspension)
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
// spawn_on — Post Coroutine to Specified io_context (cross-thread safe)
// =============================================================================
//
// Same semantics as spawn(ctx, t), but explicitly for cross-thread scenarios.
// Switches coroutine to target event loop thread via io_context::post().

export inline void spawn_on(io_context& target, task<void> t) {
    spawn(target, std::move(t));
}

// =============================================================================
// server_context — Multi-Core Server Context
// =============================================================================
//
// Manages accept-dedicated io_context + N worker io_contexts + stdexec thread pool
//
// Architecture:
//   Thread 0 (main):  accept_io  — Runs accept loop
//   Thread 1..N:      worker_io  — One io_context per thread, handles connection I/O
//   exec::static_thread_pool:      Optional CPU-intensive work offload
//
// IOCP Feature: New socket after accept is not associated with IOCP, first async_read/write
// on worker io_context automatically associates with worker's IOCP.

export class server_context {
public:
    /// @param workers Number of worker threads (default = CPU cores)
    /// @param pool_threads stdexec thread pool size (default = CPU cores)
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

    // Non-copyable and non-movable
    server_context(const server_context&) = delete;
    server_context(server_context&&) = delete;
    auto operator=(const server_context&) -> server_context& = delete;
    auto operator=(server_context&&) -> server_context& = delete;

    /// Accept-dedicated io_context
    [[nodiscard]] auto accept_io() noexcept -> io_context& {
        return *accept_io_;
    }

    /// Round-robin select next worker io_context (atomic, thread-safe)
    [[nodiscard]] auto next_worker_io() noexcept -> io_context& {
        auto idx = next_.fetch_add(1, std::memory_order_relaxed)
                   % workers_.size();
        return *workers_[idx];
    }

    /// Worker count
    [[nodiscard]] auto worker_count() const noexcept -> unsigned {
        return static_cast<unsigned>(workers_.size());
    }

    /// stdexec thread pool
    [[nodiscard]] auto pool() noexcept -> exec::static_thread_pool& {
        return pool_;
    }

    /// Start worker threads, then run accept_io on current thread
    /// Blocks until stop()
    void run() {
        // Start worker threads
        threads_.reserve(workers_.size());
        for (auto& w : workers_) {
            threads_.emplace_back([&w]() {
                w->run();
            });
        }

        // Run accept_io on current thread
        accept_io_->run();

        // After accept_io stops, wait for all worker threads to finish
        for (auto& t : threads_) {
            if (t.joinable())
                t.join();
        }
        threads_.clear();
    }

    /// Stop all io_context and thread pool
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
