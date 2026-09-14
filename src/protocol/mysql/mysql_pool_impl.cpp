module;

#include <cnetmod/config.hpp>
#include <cstdint>
#if defined(_MSC_VER)
    #include <intrin.h>
#endif

module cnetmod.protocol.mysql;

import :pool;
import :types;
import :diagnostics;
import :connection_client;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.coro.mutex;
import cnetmod.coro.cancel;
import cnetmod.coro.wait_group;
import cnetmod.executor.async_op;

namespace cnetmod::mysql {

namespace {
    /**
     * @brief Holds exclusive maintenance ownership until all workers have joined.
     */
    class maintenance_run_guard
    {
    public:
        explicit maintenance_run_guard(std::atomic<bool>& active) : active_(active)
        {
            if (active_.exchange(true, std::memory_order_acq_rel))
                throw std::system_error(std::make_error_code(std::errc::operation_in_progress));
        }

        ~maintenance_run_guard()
        {
            active_.store(false, std::memory_order_release);
        }

        maintenance_run_guard(const maintenance_run_guard&) = delete;
        auto operator=(const maintenance_run_guard&) -> maintenance_run_guard& = delete;

    private:
        std::atomic<bool>& active_;
    };
} // namespace

pooled_connection::pooled_connection(pooled_connection&& o) noexcept
    : pool_(std::exchange(o.pool_, nullptr)),
      node_(std::exchange(o.node_, nullptr)) {}

auto pooled_connection::operator=(pooled_connection&& o) noexcept
    -> pooled_connection&
{
    if (this != &o)
    {
        return_to_pool(true);
        pool_ = std::exchange(o.pool_, nullptr);
        node_ = std::exchange(o.node_, nullptr);
    }
    return *this;
}

pooled_connection::~pooled_connection()
{
    return_to_pool(true);
}

auto pooled_connection::valid() const noexcept -> bool
{
    return node_ != nullptr;
}

auto pooled_connection::get() noexcept -> client&
{
    return *node_->conn;
}

auto pooled_connection::get() const noexcept -> const client&
{
    return *node_->conn;
}

auto pooled_connection::operator->() noexcept -> client*
{
    return node_->conn.get();
}

auto pooled_connection::operator->() const noexcept -> const client*
{
    return node_->conn.get();
}

pooled_connection::pooled_connection(connection_pool* pool,
    conn_node* node) noexcept
    : pool_(pool), node_(node) {}

connection_pool::connection_pool(io_context& ctx, pool_params params)
    : ctx_(ctx), params_(std::move(params))
{
    // P6: Initialize bitmap to zero
    for (auto& bm : idle_bitmap_)
    {
        bm.store(0, std::memory_order_relaxed);
    }
}

auto connection_pool::async_run() -> task<void>
{
    maintenance_run_guard active_run{run_active_};

    try
    {
        co_await run_maintenance();
    }
    catch (...)
    {
        if (!maintenance_failure_)
            maintenance_failure_ = std::current_exception();
    }
    co_await cancel();
    co_await connection_workers_.wait();
    for (auto& node : conns_)
        if (node.conn && node.state.load(std::memory_order_acquire) != conn_state::in_use)
            node.conn->close();
    if (maintenance_failure_)
        std::rethrow_exception(maintenance_failure_);
}

auto connection_pool::run_maintenance() -> task<void>
{
    if (stop_requested_.load(std::memory_order_acquire))
        co_return;
    running_ = true;

    struct run_state_guard
    {
        bool& running;

        ~run_state_guard()
        {
            running = false;
        }
    } reset_running{running_};

    // Spawn initial connection tasks
    for (std::size_t i = 0; i < params_.initial_size && i < params_.max_size;
        ++i)
    {
        if (stop_requested_.load(std::memory_order_acquire))
            break;
        spawn_connection();
    }
    // Warm-up barrier: wait until initial connections are ready (or timeout).
    auto target = std::min(params_.initial_size, params_.max_size);
    if (target > 0)
    {
        auto deadline = std::chrono::steady_clock::now() + params_.connect_timeout;
        while (running_ && !stop_requested_.load(std::memory_order_acquire) &&
            std::chrono::steady_clock::now() < deadline)
        {
            if (count_ready_connections() >= target)
                break;
            auto waited = co_await async_timer_wait(ctx_, std::chrono::milliseconds(10), run_cancel_);
            if (!waited)
            {
                if (!stop_requested_.load(std::memory_order_acquire))
                {
                    running_ = false;
                    throw std::system_error(waited.error());
                }
                break;
            }
        }
    }
    // Background: periodic scan for dead connections that have no task
    while (running_ && !stop_requested_.load(std::memory_order_acquire))
    {
        auto waited = co_await async_timer_wait(ctx_, params_.ping_interval, run_cancel_);
        if (!waited)
        {
            running_ = false;
            if (!stop_requested_.load(std::memory_order_acquire))
                throw std::system_error(waited.error());
            break;
        }
    }
    running_ = false;
}

void connection_pool::request_stop() noexcept
{
    stop_requested_.store(true, std::memory_order_seq_cst);
    run_cancel_.cancel();
}

auto connection_pool::async_get_connection()
    -> task<std::expected<pooled_connection, std::error_code>>
{
    cancel_token token;
    co_return co_await with_timeout(ctx_, params_.pool_timeout,
        async_get_connection(token), token);
}

auto connection_pool::try_get_connection()
    -> std::expected<pooled_connection, std::error_code>
{
    if (!mtx_.try_lock())
    {
        return std::unexpected(
            make_error_code(std::errc::resource_unavailable_try_again));
    }
    async_lock_guard guard(mtx_, std::adopt_lock);

    if (stop_requested_.load(std::memory_order_acquire))
        return std::unexpected(make_error_code(std::errc::operation_canceled));

    if (waiters_head_)
    {
        notify_waiters_with_idle_locked();
        return std::unexpected(
            make_error_code(std::errc::resource_unavailable_try_again));
    }

    if (auto* node = try_get_idle_locked())
    {
        return pooled_connection(this, node);
    }

    // No idle connection now: proactively grow one if possible.
    if (running_ && conns_.size() < params_.max_size)
    {
        spawn_connection();
    }

    return std::unexpected(
        make_error_code(std::errc::resource_unavailable_try_again));
}

auto connection_pool::cancel() -> task<void>
{
    request_stop();
    running_ = false;
    co_await mtx_.lock();
    async_lock_guard guard(mtx_, std::adopt_lock);
    while (waiters_head_)
    {
        auto* waiter = waiters_head_;
        remove_waiter(waiter);
        if (waiter->token &&
            !waiter->token->pending_.exchange(false, std::memory_order_acq_rel))
            continue;
        ctx_.post_node_raw(&waiter->completion);
    }
    for (auto& node : conns_)
    {
        node.ping_sleep_token.cancel();
        node.network_token.cancel();
        if (auto handle = node.task_waiting.exchange({}, std::memory_order_seq_cst))
        {
            node.task_completion.coroutine = handle;
            ctx_.post_node_raw(&node.task_completion);
        }
        if (node.conn && node.conn->is_open() &&
            node.state.load(std::memory_order_acquire) != conn_state::in_use)
        {
            node.state.store(conn_state::dead, std::memory_order_release);
            if (connection_workers_.count() == 0)
                node.conn->close();
        }
    }
}

auto connection_pool::size() const noexcept -> std::size_t
{
    return conns_.size();
}

auto connection_pool::idle_count() const noexcept -> std::size_t
{
    std::size_t n = 0;
    for (auto& nd : conns_)
    {
        if (nd.state.load(std::memory_order_acquire) == conn_state::idle)
            ++n;
    }
    return n;
}

auto connection_pool::waiter_count() const noexcept -> std::size_t
{
    return waiters_count_.load(std::memory_order_acquire);
}

auto connection_pool::checked_out_count() const noexcept -> std::size_t
{
    std::size_t count = 0;
    for (const auto& node : conns_)
        count += node.state.load(std::memory_order_acquire) == conn_state::in_use;
    return count;
}

auto connection_pool::make_connect_options() const -> connect_options
{
    connect_options opts;
    opts.host = params_.host;
    opts.port = params_.port;
    opts.username = params_.username;
    opts.password = params_.password;
    opts.database = params_.database;
    opts.ssl = params_.ssl;
    opts.tls_verify = params_.tls_verify;
    opts.tls_ca_file = params_.tls_ca_file;
    return opts;
}

auto connection_pool::count_ready_connections() const noexcept -> std::size_t
{
    std::size_t ready = 0;
    for (auto& node : conns_)
    {
        auto st = node.state.load(std::memory_order_acquire);
        if (st == conn_state::idle || st == conn_state::in_use ||
            st == conn_state::pinging || st == conn_state::resetting ||
            st == conn_state::returning)
        {
            ++ready;
        }
    }
    return ready;
}

auto connection_pool::connection_task(conn_node& node) -> task<void>
{
    auto max_retry = std::chrono::duration_cast<std::chrono::milliseconds>(
        params_.retry_interval);
    if (max_retry <= std::chrono::milliseconds::zero())
    {
        max_retry = std::chrono::milliseconds(1);
    }
    auto retry_backoff = std::min(max_retry, std::chrono::milliseconds(100));

    while (running_ && !stop_requested_.load(std::memory_order_acquire))
    {
        // Phase 1: Ensure connected
        auto state = node.state.load(std::memory_order_acquire);
        if (state == conn_state::initial || state == conn_state::dead)
        {
            node.state.store(conn_state::connecting, std::memory_order_release);
            auto opts = make_connect_options();
            auto rs = co_await node.conn->connect(opts, node.network_token);

            co_await mtx_.lock();
            if (!running_ || stop_requested_.load(std::memory_order_acquire))
            {
                mtx_.unlock();
                break;
            }
            if (rs.is_err())
            {
                node.state.store(conn_state::dead, std::memory_order_release);
                mtx_.unlock();
                node.ping_sleep_token.reset();
                if (!running_ || stop_requested_.load(std::memory_order_acquire))
                    break;
                (void)co_await async_timer_wait(ctx_, retry_backoff, node.ping_sleep_token);
                retry_backoff = std::min(max_retry, retry_backoff * 2);
                continue;
            }
            retry_backoff = std::min(max_retry, std::chrono::milliseconds(100));
            node.state.store(conn_state::idle, std::memory_order_release);
            set_idle_bit(node.index); // P6: Mark as idle in bitmap
            // Hand idle connections to queued waiters first.
            notify_waiters_with_idle_locked();
            mtx_.unlock();
        }

        /**
         * A returned session remains unavailable until its reset succeeds.
         * Reuse the owned connection worker so reset is joined during shutdown.
         */
        const auto returned_state = node.state.load(std::memory_order_acquire);
        if (returned_state == conn_state::resetting || returned_state == conn_state::returning)
        {
            auto reset = [&]() -> task<std::expected<void, std::error_code>>
            {
                auto result = co_await node.conn->reset_connection(node.network_token);
                if (result.is_err())
                    co_return std::unexpected(node.conn->last_error()
                            ? node.conn->last_error()
                            : std::make_error_code(std::errc::io_error));
                co_return std::expected<void, std::error_code>{};
            };
            std::expected<void, std::error_code> result;
            if (returned_state == conn_state::resetting)
                result = co_await with_timeout(ctx_, params_.ping_timeout,
                    reset(), node.network_token);
            if (!running_ || stop_requested_.load(std::memory_order_acquire))
                break;
            node.network_token.reset();
            co_await mtx_.lock();
            async_lock_guard guard(mtx_, std::adopt_lock);
            node.state.store(result ? conn_state::idle : conn_state::dead,
                std::memory_order_release);
            if (result)
            {
                set_idle_bit(node.index);
                notify_waiters_with_idle_locked();
            }
            continue;
        }

        // Phase 2: Idle — wait ping_interval then ping
        state = node.state.load(std::memory_order_acquire);
        if (state == conn_state::idle)
        {
            // Make the idle wait cancellable so we can reconnect promptly
            // when a borrowed connection returns broken.
            node.ping_sleep_token.reset();
            if (!running_ || stop_requested_.load(std::memory_order_acquire))
                break;
            /**
             * A return may cancel the previous token just before reset clears it.
             * Recheck the published state before arming a fresh idle timer so
             * pending reset, reconnect, or waiter notification is not delayed.
             */
            if (node.state.load(std::memory_order_seq_cst) != conn_state::idle)
                continue;
            auto wait_r = co_await cnetmod::async_timer_wait(
                ctx_, params_.ping_interval, node.ping_sleep_token);
            node.ping_sleep_token.reset();
            if (!wait_r)
                continue; // cancelled or timer error
            if (!running_)
                break;

            co_await mtx_.lock();
            conn_state expected = conn_state::idle;
            if (!node.state.compare_exchange_strong(expected, conn_state::pinging,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                mtx_.unlock();
                continue; // Was borrowed during sleep
            }
            clear_idle_bit(node.index); // P6: Clear idle bit during ping
            mtx_.unlock();

            auto rs = co_await node.conn->ping(node.network_token);

            co_await mtx_.lock();
            if (rs.is_err())
            {
                node.state.store(conn_state::dead, std::memory_order_release);
                mtx_.unlock();
                continue; // Will reconnect next iteration
            }
            node.state.store(conn_state::idle, std::memory_order_release);
            set_idle_bit(node.index); // P6: Mark as idle again
            mtx_.unlock();
        }

        // If in_use, suspend until returned (zero-cost wait)
        state = node.state.load(std::memory_order_acquire);
        if (state == conn_state::in_use)
        {
            struct return_awaitable
            {
                conn_node& n;
                const std::atomic<bool>& stopping;

                auto await_ready() const noexcept -> bool
                {
                    return n.state.load(std::memory_order_acquire) != conn_state::in_use ||
                        stopping.load(std::memory_order_acquire);
                }

                auto await_suspend(std::coroutine_handle<> h) noexcept -> bool
                {
                    /**
                     * Publish before rechecking the condition. Either this awaiter
                     * withdraws its handle and continues, or a producer owns the
                     * single queued wakeup. Sequential ordering closes the gap
                     * between observing in_use and registering the waiter.
                     */
                    n.task_waiting.store(h, std::memory_order_seq_cst);
                    if (n.state.load(std::memory_order_seq_cst) != conn_state::in_use ||
                        stopping.load(std::memory_order_seq_cst))
                        return !n.task_waiting.exchange({}, std::memory_order_seq_cst);
                    return true;
                }

                void await_resume() noexcept {}
            };

            co_await return_awaitable{node, stop_requested_};
        }
    }
}

void connection_pool::spawn_connection()
{
    std::size_t idx = conns_.size();
    if (idx >= MAX_BITMAPS * BITMAP_BITS)
        return; // Max capacity reached

    // Construct the client before publishing a slot. Allocation failure must
    // not leave a node whose connection pointer cannot be dereferenced.
    auto connection = std::make_unique<client>(ctx_);
    conns_.emplace_back();
    auto& node = conns_.back();
    node.conn = std::move(connection);
    node.state.store(conn_state::initial, std::memory_order_release);
    node.index = idx;
    task<void> work;
    try
    {
        work = connection_task(node);
    }
    catch (...)
    {
        conns_.pop_back();
        throw;
    }
    try
    {
        start_worker(std::move(work));
    }
    catch (...)
    {
        conns_.pop_back();
        throw;
    }
}

void connection_pool::start_worker(task<void> work)
{
    struct completion_ticket
    {
        async_wait_group& workers;
        bool registered = false;

        ~completion_ticket()
        {
            if (registered)
                workers.done();
        }
    };

    auto ticket = std::make_shared<completion_ticket>(connection_workers_);
    connection_workers_.add();
    ticket->registered = true;
    spawn_guarded(ctx_, std::move(work), [this, ticket](std::exception_ptr failure) noexcept
        {
            if (!maintenance_failure_)
                maintenance_failure_ = std::move(failure);
            request_stop();
        });
}

void connection_pool::set_idle_bit(std::size_t idx)
{
    std::size_t bitmap_idx = idx / BITMAP_BITS;
    std::size_t bit_pos = idx % BITMAP_BITS;
    if (bitmap_idx < MAX_BITMAPS)
    {
        idle_bitmap_[bitmap_idx].fetch_or(1ULL << bit_pos,
            std::memory_order_release);
    }
}

void connection_pool::clear_idle_bit(std::size_t idx)
{
    std::size_t bitmap_idx = idx / BITMAP_BITS;
    std::size_t bit_pos = idx % BITMAP_BITS;
    if (bitmap_idx < MAX_BITMAPS)
    {
        idle_bitmap_[bitmap_idx].fetch_and(~(1ULL << bit_pos),
            std::memory_order_release);
    }
}

std::size_t connection_pool::count_pending_conns() const
{
    std::size_t n = 0;
    for (auto& nd : conns_)
    {
        auto state = nd.state.load(std::memory_order_acquire);
        if (state == conn_state::connecting || state == conn_state::initial)
            ++n;
    }
    return n;
}

void connection_pool::create_connections_if_needed()
{
    // Must hold lock
    std::size_t pending = count_pending_conns();
    std::size_t room = params_.max_size - conns_.size();
    std::size_t needed =
        (num_pending_requests_ > pending) ? (num_pending_requests_ - pending) : 0;
    std::size_t to_create = std::min(needed, room);
    for (std::size_t i = 0; i < to_create; ++i)
        spawn_connection();
}

void connection_pool::dec_if_positive(std::atomic<std::size_t>& counter)
{
    auto v = counter.load(std::memory_order_acquire);
    while (v > 0 &&
        !counter.compare_exchange_weak(v, v - 1, std::memory_order_release,
            std::memory_order_relaxed))
    {
    }
}

auto connection_pool::try_get_idle_locked() -> conn_node*
{
    // Must hold lock.
    for (std::size_t i = 0; i < MAX_BITMAPS; ++i)
    {
        uint64_t bits = idle_bitmap_[i].load(std::memory_order_acquire);
        while (bits != 0)
        {
            int bit_pos = -1;
#if defined(_MSC_VER)
            unsigned long pos;
            if (_BitScanForward64(&pos, bits))
            {
                bit_pos = static_cast<int>(pos);
            }
#elif defined(__GNUC__) || defined(__clang__)
            bit_pos = __builtin_ctzll(bits);
#else
            for (int j = 0; j < 64; ++j)
            {
                if (bits & (1ULL << j))
                {
                    bit_pos = j;
                    break;
                }
            }
#endif
            if (bit_pos < 0)
                break;
            bits &= (bits - 1);

            std::size_t idx = i * BITMAP_BITS + static_cast<std::size_t>(bit_pos);
            if (idx >= conns_.size())
                continue;

            auto& node = conns_[idx];
            conn_state expected = conn_state::idle;
            if (node.state.compare_exchange_strong(expected, conn_state::in_use,
                    std::memory_order_acquire,
                    std::memory_order_relaxed))
            {
                clear_idle_bit(idx);
                return &node;
            }
        }
    }
    return nullptr;
}

void connection_pool::notify_waiters_with_idle_locked()
{
    // Must hold lock.
    while (waiters_head_)
    {
        auto* node = try_get_idle_locked();
        if (!node)
            break;

        auto* w = waiters_head_;
        waiters_head_ = w->next;
        if (!waiters_head_)
            waiters_tail_ = nullptr;
        dec_if_positive(waiters_count_);
        if (num_pending_requests_ > 0)
            --num_pending_requests_;

        // A cancellation callback may race a connection return from another
        // executor.  Claim the waiter before publishing the connection.  If
        // cancellation already owns it, put the slot back and let its queued
        // cleanup resume the waiter.  In particular, never leave an in-use
        // slot assigned to a coroutine that will report a timeout.
        if (w->token &&
            !w->token->pending_.exchange(false, std::memory_order_acq_rel))
        {
            node->state.store(conn_state::idle, std::memory_order_release);
            set_idle_bit(node->index);
            continue;
        }
        *w->result_node = node;
        if (w->handle)
            ctx_.post_node_raw(&w->completion);
    }
}

auto connection_pool::remove_waiter(pool_waiter* target) -> bool
{
    pool_waiter* prev = nullptr;
    for (auto* w = waiters_head_; w; prev = w, w = w->next)
    {
        if (w == target)
        {
            if (prev)
                prev->next = w->next;
            else
                waiters_head_ = w->next;
            if (waiters_tail_ == w)
                waiters_tail_ = prev;
            w->next = nullptr;
            dec_if_positive(waiters_count_);
            if (num_pending_requests_ > 0)
                --num_pending_requests_;
            return true;
        }
    }
    return false;
}

auto connection_pool::publish_returned_connection(conn_node& node, conn_state reusable_state) -> bool
{
    const bool open = node.conn && node.conn->is_open();
    const conn_state target = open ? reusable_state : conn_state::dead;

    conn_state expected = conn_state::in_use;
    if (!node.state.compare_exchange_strong(expected, target,
            std::memory_order_seq_cst,
            std::memory_order_relaxed))
    {
        return false;
    }

    if (target == conn_state::idle)
    {
        set_idle_bit(node.index);
    }
    else
    {
        clear_idle_bit(node.index);
        // Wake the per-connection task if it's waiting in idle sleep,
        // so it can reconnect immediately.
        node.ping_sleep_token.cancel();
    }

    if (auto h = node.task_waiting.exchange({}, std::memory_order_seq_cst))
    {
        node.task_completion.coroutine = h;
        ctx_.post_node_raw(&node.task_completion);
    }

    return true;
}

void connection_pool::return_connection(conn_node& node, bool needs_reset)
{
    const auto target = needs_reset ? conn_state::resetting : conn_state::idle;
    // No queued waiters: avoid lock and return immediately.
    if (waiters_count_.load(std::memory_order_acquire) == 0)
    {
        if (publish_returned_connection(node, target))
            return;
    }

    // Waiters exist: notify under lock.
    if (mtx_.try_lock())
    {
        (void)publish_returned_connection(node, target);
        if (waiters_head_)
        {
            notify_waiters_with_idle_locked();
        }
        mtx_.unlock();
        return;
    }

    /**
     * Delegate contended publication to the existing joined connection worker.
     * Returning a lease must not allocate a coroutine from its destructor.
     * A no-reset lease remains unavailable until FIFO waiters are notified.
     */
    (void)publish_returned_connection(node,
        needs_reset ? conn_state::resetting : conn_state::returning);
}

auto connection_pool::async_get_connection(cnetmod::deadline value)
    -> task<std::expected<pooled_connection, std::error_code>>
{
    co_return co_await with_deadline(ctx_, value,
        [this](cancel_token& token)
        {
            return async_get_connection(token);
        });
}

auto connection_pool::async_get_connection(cancel_token& token)
    -> task<std::expected<pooled_connection, std::error_code>>
{
    // All pool metadata, including deque growth and FIFO ordering, is
    // serialized by this coroutine mutex. It is never held while a caller
    // owns a connection or while any I/O is awaited.
    co_await mtx_.lock();
    async_lock_guard guard(mtx_, std::adopt_lock);

    // Prioritize old waiters first if any idle connections exist.
    if (stop_requested_.load(std::memory_order_acquire))
        co_return std::unexpected(make_error_code(std::errc::operation_canceled));
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(
            token.reason() == cancellation_reason::deadline_exceeded
                ? std::errc::timed_out
                : std::errc::operation_canceled));

    if (waiters_head_)
    {
        notify_waiters_with_idle_locked();
    }

    // 1) Try to find an idle connection again (may have become available)
    if (!waiters_head_)
    {
        if (auto* node = try_get_idle_locked())
        {
            co_return pooled_connection(this, node);
        }
    }

    // 2) P1: Record pending request + demand-driven scaling
    ++num_pending_requests_;
    try
    {
        if (running_)
            create_connections_if_needed();
    }
    catch (...)
    {
        --num_pending_requests_;
        throw;
    }

    // 3) Wait in queue (cancellable via cancel_token)
    conn_node* assigned = nullptr;
    pool_waiter waiter;
    waiter.result_node = &assigned;
    waiter.token = &token;

    struct waiter_awaitable
    {
        connection_pool& pool;
        pool_waiter& w;
        async_lock_guard& guard;
        cancel_token& token;

        auto await_ready() const noexcept -> bool
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> h) noexcept
        {
            w.handle = h;
            w.completion.coroutine = h;
            if (!pool.waiters_head_)
            {
                pool.waiters_head_ = pool.waiters_tail_ = &w;
            }
            else
            {
                pool.waiters_tail_->next = &w;
                pool.waiters_tail_ = &w;
            }
            pool.waiters_count_.fetch_add(1, std::memory_order_release);
            token.ctx_ = &pool;
            token.io_handle_ = &w;
            token.coroutine_ = h;
            token.cancel_fn_ = [](cancel_token& tok) noexcept
            {
                if (!tok.pending_.exchange(false, std::memory_order_acq_rel))
                    return;
                auto* p = static_cast<connection_pool*>(tok.ctx_);
                // The owning coroutine unlinks its waiter under the mutex
                // before returning. Keep that cleanup in its existing frame:
                // cancellation needs neither a pool lock nor a helper task.
                auto* waiter = static_cast<pool_waiter*>(tok.io_handle_);
                p->ctx_.post_node_raw(&waiter->completion);
            };
            token.pending_.store(true, std::memory_order_release);
            guard.release();
            pool.mtx_.unlock();

            if (token.is_cancelled())
            {
                // Cancellation may have happened immediately before the
                // waiter was armed. The resumed coroutine must unlink its
                // stack waiter before its frame can finish.
                token.cancel_fn_(token);
            }
        }

        void await_resume() noexcept
        {
            token.pending_.store(false, std::memory_order_relaxed);
        }
    };

    co_await waiter_awaitable{*this, waiter, guard, token};

    // Assignment wins when it raced cancellation: the connection has already
    // been transferred atomically under the pool mutex and must be returned by
    // the pooled_connection RAII handle, not silently leaked on a timeout.
    if (assigned)
    {
        co_return pooled_connection(this, assigned);
    }

    if (token.is_cancelled())
    {
        {
            co_await mtx_.lock();
            async_lock_guard cleanup(mtx_, std::adopt_lock);
            remove_waiter(&waiter);
        }
        co_return std::unexpected(make_error_code(
            token.reason() == cancellation_reason::deadline_exceeded
                ? std::errc::timed_out
                : std::errc::operation_canceled));
    }
    co_return std::unexpected(make_error_code(
        stop_requested_.load(std::memory_order_acquire)
            ? std::errc::operation_canceled
            : std::errc::timed_out));
}

void pooled_connection::return_to_pool(bool needs_reset)
{
    if (pool_ && node_)
    {
        pool_->return_connection(*node_, needs_reset);
        pool_ = nullptr;
        node_ = nullptr;
    }
}

void pooled_connection::return_without_reset()
{
    return_to_pool(false);
}

sharded_connection_pool::sharded_connection_pool(io_context& ctx,
    pool_params params,
    std::size_t num_shards)
    : base_params_(std::move(params)), fallback_ctx_(&ctx)
{
    if (num_shards == 0)
        num_shards = 1;
    init_shards(std::vector<io_context*>{&ctx}, num_shards);
}

sharded_connection_pool::sharded_connection_pool(
    std::vector<io_context*> worker_contexts, pool_params params)
    : sharded_connection_pool(
          worker_contexts, std::move(params),
          worker_contexts.empty() ? 1 : worker_contexts.size()) {}

sharded_connection_pool::sharded_connection_pool(
    std::vector<io_context*> worker_contexts, pool_params params,
    std::size_t num_shards)
    : base_params_(std::move(params))
{
    if (worker_contexts.empty())
    {
        throw std::invalid_argument(
            "sharded_connection_pool requires at least one io_context");
    }
    if (num_shards == 0)
        num_shards = 1;
    fallback_ctx_ = worker_contexts.front();
    init_shards(worker_contexts, num_shards);
}

auto sharded_connection_pool::async_run() -> task<void>
{
    maintenance_run_guard active_run{run_active_};
    async_wait_group workers;
    std::vector<std::exception_ptr> failures(shards_.size());
    std::exception_ptr startup_failure;

    struct completion_ticket
    {
        async_wait_group& workers;
        bool registered = false;

        ~completion_ticket()
        {
            if (registered)
                workers.done();
        }
    };

    try
    {
        for (std::size_t index = 0; index < shards_.size(); ++index)
        {
            auto ticket = std::make_shared<completion_ticket>(workers);
            workers.add();
            ticket->registered = true;
            spawn_guarded(*shard_ctxs_[index], shards_[index]->async_run(),
                [this, index, &failures, ticket](std::exception_ptr failure) noexcept
                {
                    failures[index] = std::move(failure);
                    request_stop();
                });
        }
    }
    catch (...)
    {
        startup_failure = std::current_exception();
        request_stop();
    }
    co_await workers.wait();
    if (startup_failure)
        std::rethrow_exception(startup_failure);
    for (const auto& failure : failures)
        if (failure)
            std::rethrow_exception(failure);
}

void sharded_connection_pool::request_stop() noexcept
{
    for (auto& shard : shards_)
        shard->request_stop();
}

auto sharded_connection_pool::async_get_connection()
    -> task<std::expected<pooled_connection, std::error_code>>
{
    auto primary =
        next_shard_.fetch_add(1, std::memory_order_relaxed) % shards_.size();
    if (auto fast = try_borrow_immediate(primary))
    {
        co_return std::move(*fast);
    }

    auto wait_idx = select_wait_shard(primary);
    co_return co_await shards_[wait_idx]->async_get_connection();
}

auto sharded_connection_pool::async_get_connection(cancel_token& token)
    -> task<std::expected<pooled_connection, std::error_code>>
{
    auto primary =
        next_shard_.fetch_add(1, std::memory_order_relaxed) % shards_.size();
    if (auto fast = try_borrow_immediate(primary))
    {
        co_return std::move(*fast);
    }

    auto wait_idx = select_wait_shard(primary);
    co_return co_await shards_[wait_idx]->async_get_connection(token);
}

auto sharded_connection_pool::async_get_connection(cnetmod::deadline value)
    -> task<std::expected<pooled_connection, std::error_code>>
{
    auto primary =
        next_shard_.fetch_add(1, std::memory_order_relaxed) % shards_.size();
    if (auto fast = try_borrow_immediate(primary))
        co_return std::move(*fast);
    auto wait_index = select_wait_shard(primary);
    co_return co_await shards_[wait_index]->async_get_connection(value);
}

auto sharded_connection_pool::async_get_connection(io_context& io)
    -> task<std::expected<pooled_connection, std::error_code>>
{
    auto primary = get_shard_index(io);
    if (auto fast = try_borrow_immediate(primary))
    {
        co_return std::move(*fast);
    }

    auto wait_idx = select_wait_shard(primary);
    co_return co_await shards_[wait_idx]->async_get_connection();
}

auto sharded_connection_pool::async_get_connection(io_context& io,
    cancel_token& token)
    -> task<std::expected<pooled_connection, std::error_code>>
{
    auto primary = get_shard_index(io);
    if (auto fast = try_borrow_immediate(primary))
    {
        co_return std::move(*fast);
    }

    auto wait_idx = select_wait_shard(primary);
    co_return co_await shards_[wait_idx]->async_get_connection(token);
}

auto sharded_connection_pool::cancel() -> task<void>
{
    request_stop();
    co_return;
}

auto sharded_connection_pool::size() const noexcept -> std::size_t
{
    std::size_t total = 0;
    for (auto& shard : shards_)
    {
        total += shard->size();
    }
    return total;
}

auto sharded_connection_pool::idle_count() const noexcept -> std::size_t
{
    std::size_t total = 0;
    for (auto& shard : shards_)
    {
        total += shard->idle_count();
    }
    return total;
}

auto sharded_connection_pool::shard_count() const noexcept -> std::size_t
{
    return shards_.size();
}

auto sharded_connection_pool::get_shard_index(io_context& io) -> std::size_t
{
    auto it = shard_by_ctx_.find(&io);
    if (it != shard_by_ctx_.end())
    {
        return it->second;
    }
    return next_shard_.fetch_add(1, std::memory_order_relaxed) % shards_.size();
}

auto sharded_connection_pool::try_borrow_immediate(std::size_t primary_idx)
    -> std::expected<pooled_connection, std::error_code>
{
    // 1) Try preferred shard first.
    if (primary_idx < shards_.size())
    {
        if (auto conn = shards_[primary_idx]->try_get_connection())
        {
            return std::move(*conn);
        }
    }

    // 2) Steal from other shards (best effort, no waiting).
    if (shards_.size() <= 1)
    {
        return std::unexpected(
            make_error_code(std::errc::resource_unavailable_try_again));
    }

    auto start =
        next_shard_.fetch_add(1, std::memory_order_relaxed) % shards_.size();
    for (std::size_t i = 0; i < shards_.size(); ++i)
    {
        auto idx = (start + i) % shards_.size();
        if (idx == primary_idx)
            continue;
        if (auto conn = shards_[idx]->try_get_connection())
        {
            return std::move(*conn);
        }
    }
    return std::unexpected(
        make_error_code(std::errc::resource_unavailable_try_again));
}

auto sharded_connection_pool::select_wait_shard(std::size_t preferred_idx)
    -> std::size_t
{
    if (preferred_idx >= shards_.size())
    {
        preferred_idx = 0;
    }
    auto best_idx = preferred_idx;
    auto best_waiters = shards_[best_idx]->waiter_count();

    for (std::size_t i = 0; i < shards_.size(); ++i)
    {
        auto w = shards_[i]->waiter_count();
        if (w < best_waiters)
        {
            best_waiters = w;
            best_idx = i;
        }
    }
    return best_idx;
}

void sharded_connection_pool::init_shards(
    const std::vector<io_context*>& worker_contexts, std::size_t num_shards)
{
    if (num_shards == 0)
        num_shards = 1;

    std::vector<io_context*> contexts;
    contexts.reserve(worker_contexts.size());
    for (auto* ctx : worker_contexts)
    {
        if (ctx)
            contexts.push_back(ctx);
    }
    if (contexts.empty())
    {
        throw std::invalid_argument(
            "sharded_connection_pool has no valid io_context");
    }

    auto shard_initial =
        (base_params_.initial_size + num_shards - 1) / num_shards;
    auto shard_max = (base_params_.max_size + num_shards - 1) / num_shards;

    shards_.reserve(num_shards);
    shard_ctxs_.reserve(num_shards);
    for (std::size_t i = 0; i < num_shards; ++i)
    {
        auto* shard_ctx = contexts[i % contexts.size()];
        if (!shard_ctx)
            shard_ctx = fallback_ctx_;
        if (!shard_ctx)
            continue;
        auto shard_params = base_params_;
        shard_params.initial_size = shard_initial;
        shard_params.max_size = shard_max;
        shard_ctxs_.push_back(shard_ctx);
        shards_.push_back(
            std::make_unique<connection_pool>(*shard_ctx, std::move(shard_params)));
    }

    if (shards_.empty())
    {
        throw std::invalid_argument(
            "sharded_connection_pool has no valid io_context");
    }

    // Build context -> primary shard mapping (workers and shards are decoupled).
    for (std::size_t i = 0; i < contexts.size(); ++i)
    {
        auto* ctx = contexts[i];
        auto primary = i % shards_.size();
        if (!shard_by_ctx_.contains(ctx))
        {
            shard_by_ctx_.emplace(ctx, primary);
        }
    }
}

} // namespace cnetmod::mysql
