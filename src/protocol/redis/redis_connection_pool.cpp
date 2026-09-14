module;

#if defined(_MSC_VER)
    #include <intrin.h>
#endif

module cnetmod.protocol.redis;

import std;
import :pool;
import cnetmod.coro.cancel;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.coro.mutex;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;
import cnetmod.coro.wait_group;

namespace cnetmod::redis {

namespace {
    [[nodiscard]] auto first_set_bit(std::uint64_t bits) noexcept -> int
    {
#if defined(_MSC_VER)
        unsigned long position;
        return _BitScanForward64(&position, bits) ? static_cast<int>(position) : -1;
#elif defined(__GNUC__) || defined(__clang__)
        return bits == 0 ? -1 : __builtin_ctzll(bits);
#else
        for (int i = 0; i < 64; ++i)
            if (bits & (std::uint64_t{1} << i))
                return i;
        return -1;
#endif
    }
} // namespace

pooled_connection::pooled_connection() noexcept = default;

pooled_connection::pooled_connection(connection_pool* pool,
    conn_node* node) noexcept
    : pool_(pool), node_(node) {}

pooled_connection::pooled_connection(pooled_connection&& other) noexcept
    : pool_(std::exchange(other.pool_, nullptr)),
      node_(std::exchange(other.node_, nullptr)) {}

auto pooled_connection::operator=(pooled_connection&& other) noexcept
    -> pooled_connection&
{
    if (this != &other)
    {
        return_to_pool();
        pool_ = std::exchange(other.pool_, nullptr);
        node_ = std::exchange(other.node_, nullptr);
    }
    return *this;
}

pooled_connection::~pooled_connection()
{
    return_to_pool();
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

void pooled_connection::return_to_pool()
{
    if (pool_ && node_)
    {
        pool_->return_connection(*node_);
        pool_ = nullptr;
        node_ = nullptr;
    }
}

connection_pool::connection_pool(io_context& ctx, pool_params params)
    : ctx_(ctx), params_(std::move(params))
{
    for (auto& bitmap : idle_bitmap_)
        bitmap.store(0, std::memory_order_relaxed);
}

auto connection_pool::async_run() -> task<void>
{
    if (stop_requested_.load())
    {
        co_await cancel();
        co_return;
    }
    run_cancellation_.reset();
    run_active_.store(true);

    struct run_registration
    {
        std::atomic<bool>& active;

        ~run_registration()
        {
            active.store(false);
        }
    } registration{run_active_};

    if (stop_requested_.load())
        run_cancellation_.cancel();
    stopped_ = false;
    running_ = true;
    maintenance_failure_ = {};
    try
    {
        for (std::size_t i = 0; i < params_.initial_size && i < params_.max_size &&
            !stop_requested_.load(std::memory_order_acquire);
            ++i)
            spawn_connection();
        const auto target = std::min(params_.initial_size, params_.max_size);
        if (target > 0)
        {
            const auto deadline =
                std::chrono::steady_clock::now() + params_.connect_timeout;
            while (running_ && !stop_requested_.load(std::memory_order_acquire) &&
                std::chrono::steady_clock::now() < deadline)
            {
                if (count_ready_connections() >= target)
                    break;
                (void)co_await async_timer_wait(ctx_, std::chrono::milliseconds(10), run_cancellation_);
            }
        }
        while (running_ && !stop_requested_.load(std::memory_order_acquire))
            (void)co_await async_timer_wait(ctx_, params_.ping_interval, run_cancellation_);
    }
    catch (...)
    {
        if (!maintenance_failure_)
            maintenance_failure_ = std::current_exception();
    }
    co_await cancel();
    if (maintenance_failure_)
        std::rethrow_exception(maintenance_failure_);
}

void connection_pool::request_stop() noexcept
{
    stop_requested_.store(true);
    if (run_active_.load())
        run_cancellation_.cancel();
}

auto connection_pool::async_get_connection()
    -> task<std::expected<pooled_connection, std::error_code>>
{
    cancel_token token;
    co_return co_await with_timeout(ctx_, params_.pool_timeout,
        async_get_connection(token), token);
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
    co_await mtx_.lock();
    async_lock_guard guard(mtx_, std::adopt_lock);
    if (token.is_cancelled())
        co_return std::unexpected(std::make_error_code(
            token.reason() == cancellation_reason::deadline_exceeded
                ? std::errc::timed_out
                : std::errc::operation_canceled));
    if (stopped_)
        co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
    if (waiters_head_)
        notify_waiters_with_idle_locked();
    if (!waiters_head_)
        if (auto* node = try_get_idle_locked())
            co_return pooled_connection(this, node);

    ++num_pending_requests_;
    if (running_)
        create_connections_if_needed();
    conn_node* assigned = nullptr;
    pool_waiter waiter;
    waiter.result_node = &assigned;
    waiter.token = &token;

    struct waiter_awaitable
    {
        connection_pool& pool;
        pool_waiter& waiter;
        async_lock_guard& guard;
        cancel_token& token;

        auto await_ready() const noexcept -> bool
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept
        {
            waiter.handle = handle;
            waiter.completion.coroutine = handle;
            if (!pool.waiters_head_)
                pool.waiters_head_ = pool.waiters_tail_ = &waiter;
            else
            {
                pool.waiters_tail_->next = &waiter;
                pool.waiters_tail_ = &waiter;
            }
            pool.waiters_count_.fetch_add(1, std::memory_order_release);
            token.ctx_ = &pool;
            token.io_handle_ = &waiter;
            token.coroutine_ = handle;
            token.cancel_fn_ = [](cancel_token& item) noexcept
            {
                if (!item.pending_.exchange(false, std::memory_order_acq_rel))
                    return;
                auto* owner = static_cast<connection_pool*>(item.ctx_);
                auto* queued = static_cast<pool_waiter*>(item.io_handle_);
                // The original waiter removes its registration after resuming.
                // Cancellation neither allocates nor starts an unowned task.
                owner->ctx_.post_node_raw(&queued->completion);
            };
            token.pending_.store(true, std::memory_order_release);
            guard.release();
            pool.mtx_.unlock();
            if (token.is_cancelled())
            {
                token.cancel_fn_(token);
            }
        }

        void await_resume() noexcept
        {
            token.pending_.store(false, std::memory_order_relaxed);
        }
    };

    co_await waiter_awaitable{*this, waiter, guard, token};
    // A successfully assigned connection owns the completion race with a
    // concurrent cancellation.  Returning it through RAII preserves the
    // lease instead of losing an in-use slot on the timeout path.
    if (assigned)
        co_return pooled_connection(this, assigned);
    if (waiter.pool_stopped)
        co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));

    if (token.is_cancelled())
    {
        co_await mtx_.lock();
        remove_waiter(&waiter);
        mtx_.unlock();
        co_return std::unexpected(make_error_code(
            token.reason() == cancellation_reason::deadline_exceeded
                ? std::errc::timed_out
                : std::errc::operation_canceled));
    }
    co_return std::unexpected(make_error_code(std::errc::timed_out));
}

auto connection_pool::try_get_connection()
    -> std::expected<pooled_connection, std::error_code>
{
    if (!mtx_.try_lock())
        return std::unexpected(
            make_error_code(std::errc::resource_unavailable_try_again));
    async_lock_guard guard(mtx_, std::adopt_lock);
    if (stopped_)
        return std::unexpected(std::make_error_code(std::errc::operation_canceled));
    if (waiters_head_)
    {
        notify_waiters_with_idle_locked();
        return std::unexpected(
            make_error_code(std::errc::resource_unavailable_try_again));
    }
    if (auto* node = try_get_idle_locked())
        return pooled_connection(this, node);
    if (running_ && conns_.size() < params_.max_size)
        spawn_connection();
    return std::unexpected(
        make_error_code(std::errc::resource_unavailable_try_again));
}

auto connection_pool::cancel() -> task<void>
{
    co_await mtx_.lock();
    running_ = false;
    stopped_ = true;
    run_cancellation_.cancel();
    /**
     * Claim each pending completion under the queue lock. A caller cancellation
     * that already owns completion retains responsibility for its queued resume.
     */
    while (waiters_head_)
    {
        auto* waiter = waiters_head_;
        waiters_head_ = waiter->next;
        dec_if_positive(waiters_count_);
        if (num_pending_requests_ > 0U)
            --num_pending_requests_;
        if (waiter->token->pending_.exchange(false, std::memory_order_acq_rel))
        {
            waiter->pool_stopped = true;
            ctx_.post_node_raw(&waiter->completion);
        }
    }
    waiters_tail_ = nullptr;
    for (auto& node : conns_)
    {
        auto state = node.state.load(std::memory_order_acquire);
        while (!node.state.compare_exchange_weak(state,
            state == conn_state::in_use || state == conn_state::retired_in_use
                ? conn_state::retired_in_use
                : conn_state::dead,
            std::memory_order_acq_rel, std::memory_order_acquire))
        {
        }
        clear_idle_bit(node.index);
        node.maintenance_cancellation.cancel();
        if (auto handle = std::exchange(node.task_waiting, {}))
            ctx_.post(handle);
    }
    mtx_.unlock();
    co_await maintenance_.wait();
    /**
     * Close idle clients only after maintenance I/O has settled. Outstanding
     * leases retain their transport until their owners return them.
     */
    for (auto& node : conns_)
        if (node.conn && node.state.load(std::memory_order_acquire) == conn_state::dead)
            node.conn->close();
}

auto connection_pool::size() const noexcept -> std::size_t
{
    return conns_.size();
}

auto connection_pool::idle_count() const noexcept -> std::size_t
{
    std::size_t count = 0;
    for (const auto& node : conns_)
        if (node.state.load(std::memory_order_acquire) == conn_state::idle)
            ++count;
    return count;
}

auto connection_pool::waiter_count() const noexcept -> std::size_t
{
    return waiters_count_.load(std::memory_order_acquire);
}

auto connection_pool::checked_out_count() const noexcept -> std::size_t
{
    std::size_t count = 0;
    for (const auto& node : conns_)
    {
        const auto state = node.state.load(std::memory_order_acquire);
        if (state == conn_state::in_use || state == conn_state::retired_in_use)
            ++count;
    }
    return count;
}

auto connection_pool::pending_maintenance() const noexcept -> int
{
    return maintenance_.count();
}

auto connection_pool::make_connect_options() const -> connect_options
{
    return {.host = params_.host,
        .port = params_.port,
        .password = params_.password,
        .username = params_.username,
        .db = params_.db,
        .resp3 = params_.resp3,
        .tls = params_.tls,
        .tls_verify = params_.tls_verify,
        .tls_ca_file = params_.tls_ca_file,
        .tls_cert_file = params_.tls_cert_file,
        .tls_key_file = params_.tls_key_file,
        .tls_sni = params_.tls_sni};
}

auto connection_pool::count_ready_connections() const noexcept -> std::size_t
{
    std::size_t ready = 0;
    for (const auto& node : conns_)
    {
        auto state = node.state.load(std::memory_order_acquire);
        if (state == conn_state::idle || state == conn_state::in_use ||
            state == conn_state::pinging)
            ++ready;
    }
    return ready;
}

auto connection_pool::connection_task(conn_node& node) -> task<void>
{
    auto max_retry = std::chrono::duration_cast<std::chrono::milliseconds>(
        params_.retry_interval);
    if (max_retry <= std::chrono::milliseconds::zero())
        max_retry = std::chrono::milliseconds(1);
    auto retry = std::min(max_retry, std::chrono::milliseconds(100));
    while (running_)
    {
        node.maintenance_cancellation.reset();
        auto state = node.state.load(std::memory_order_acquire);
        if (state == conn_state::initial || state == conn_state::dead)
        {
            node.state.store(conn_state::connecting, std::memory_order_release);
            auto result = co_await with_timeout(ctx_, params_.connect_timeout,
                node.conn->connect(make_connect_options(), node.maintenance_cancellation),
                node.maintenance_cancellation);
            co_await mtx_.lock();
            if (!running_)
            {
                node.conn->close();
                mtx_.unlock();
                break;
            }
            if (!result)
            {
                node.state.store(conn_state::dead, std::memory_order_release);
                mtx_.unlock();
                (void)co_await async_timer_wait(ctx_, retry, node.maintenance_cancellation);
                retry = std::min(max_retry, retry * 2);
                continue;
            }
            retry = std::min(max_retry, std::chrono::milliseconds(100));
            node.state.store(conn_state::idle, std::memory_order_release);
            node.last_used = std::chrono::steady_clock::now();
            set_idle_bit(node.index);
            notify_waiters_with_idle_locked();
            mtx_.unlock();
        }
        state = node.state.load(std::memory_order_acquire);
        if (state == conn_state::idle)
        {
            (void)co_await async_timer_wait(ctx_, params_.ping_interval, node.maintenance_cancellation);
            if (!running_)
                break;
            co_await mtx_.lock();
            conn_state expected = conn_state::idle;
            if (!node.state.compare_exchange_strong(expected, conn_state::pinging,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                mtx_.unlock();
                continue;
            }
            clear_idle_bit(node.index);
            mtx_.unlock();
            auto result = co_await with_timeout(ctx_, params_.ping_timeout,
                node.conn->ping(node.maintenance_cancellation), node.maintenance_cancellation);
            co_await mtx_.lock();
            if (!result || !running_)
            {
                node.state.store(conn_state::dead, std::memory_order_release);
                mtx_.unlock();
                continue;
            }
            node.state.store(conn_state::idle, std::memory_order_release);
            node.last_used = std::chrono::steady_clock::now();
            set_idle_bit(node.index);
            mtx_.unlock();
        }
        if (node.state.load(std::memory_order_acquire) == conn_state::in_use)
        {
            struct return_awaitable
            {
                conn_node& node;

                auto await_ready() const noexcept -> bool
                {
                    return node.state.load(std::memory_order_acquire) !=
                        conn_state::in_use;
                }

                void await_suspend(std::coroutine_handle<> handle) noexcept
                {
                    node.task_waiting = handle;
                }

                void await_resume() noexcept {}
            };

            co_await return_awaitable{node};
        }
    }
}

void connection_pool::spawn_connection()
{
    const auto index = conns_.size();
    if (index >= max_bitmaps * bitmap_bits)
        return;
    conns_.emplace_back();
    auto& node = conns_.back();
    node.conn = std::make_unique<client>(ctx_);
    node.state.store(conn_state::initial, std::memory_order_release);
    node.last_used = std::chrono::steady_clock::now();
    node.index = index;

    /**
     * @brief Keeps completion registered even if dispatch fails before first resume.
     */
    struct maintenance_ticket
    {
        async_wait_group& group;

        explicit maintenance_ticket(async_wait_group& value) noexcept : group(value)
        {
            group.add();
        }

        ~maintenance_ticket()
        {
            group.done();
        }
    };

    auto ticket = std::make_shared<maintenance_ticket>(maintenance_);
    spawn_guarded(ctx_, connection_task(node), [this, ticket = std::move(ticket)](std::exception_ptr failure)
        {
            if (!maintenance_failure_)
                maintenance_failure_ = failure;
            running_ = false;
            run_cancellation_.cancel();
        });
}

void connection_pool::set_idle_bit(std::size_t index)
{
    const auto map = index / bitmap_bits;
    if (map < max_bitmaps)
        idle_bitmap_[map].fetch_or(std::uint64_t{1} << (index % bitmap_bits),
            std::memory_order_release);
}

void connection_pool::clear_idle_bit(std::size_t index)
{
    const auto map = index / bitmap_bits;
    if (map < max_bitmaps)
        idle_bitmap_[map].fetch_and(~(std::uint64_t{1} << (index % bitmap_bits)),
            std::memory_order_release);
}

auto connection_pool::count_pending_conns() const -> std::size_t
{
    std::size_t count = 0;
    for (const auto& node : conns_)
    {
        auto state = node.state.load(std::memory_order_acquire);
        if (state == conn_state::connecting || state == conn_state::initial)
            ++count;
    }
    return count;
}

void connection_pool::create_connections_if_needed()
{
    const auto pending = count_pending_conns();
    const auto room = params_.max_size - conns_.size();
    const auto needed =
        num_pending_requests_ > pending ? num_pending_requests_ - pending : 0;
    for (std::size_t i = 0; i < std::min(needed, room); ++i)
        spawn_connection();
}

void connection_pool::dec_if_positive(std::atomic<std::size_t>& counter)
{
    auto value = counter.load(std::memory_order_acquire);
    while (value > 0 && !counter.compare_exchange_weak(value, value - 1, std::memory_order_release, std::memory_order_relaxed))
    {
    }
}

auto connection_pool::try_get_idle_locked() -> conn_node*
{
    for (std::size_t map = 0; map < max_bitmaps; ++map)
    {
        auto bits = idle_bitmap_[map].load(std::memory_order_acquire);
        while (bits)
        {
            const auto bit = first_set_bit(bits);
            if (bit < 0)
                break;
            bits &= bits - 1;
            const auto index = map * bitmap_bits + static_cast<std::size_t>(bit);
            if (index >= conns_.size())
                continue;
            auto& node = conns_[index];
            auto expected = conn_state::idle;
            if (node.state.compare_exchange_strong(expected, conn_state::in_use,
                    std::memory_order_acquire,
                    std::memory_order_relaxed))
            {
                clear_idle_bit(index);
                node.last_used = std::chrono::steady_clock::now();
                return &node;
            }
        }
    }
    return nullptr;
}

void connection_pool::notify_waiters_with_idle_locked()
{
    while (waiters_head_)
    {
        auto* node = try_get_idle_locked();
        if (!node)
            break;
        auto* waiter = waiters_head_;
        waiters_head_ = waiter->next;
        if (!waiters_head_)
            waiters_tail_ = nullptr;
        dec_if_positive(waiters_count_);
        if (num_pending_requests_ > 0)
            --num_pending_requests_;
        // Cancellation and return may arrive concurrently.  The pending flag
        // is the completion claim: when cancellation got there first, restore
        // the lease and let its cleanup coroutine resume the waiter.
        if (waiter->token && !waiter->token->pending_.exchange(false, std::memory_order_acq_rel))
        {
            node->state.store(conn_state::idle, std::memory_order_release);
            set_idle_bit(node->index);
            continue;
        }
        *waiter->result_node = node;
        if (waiter->handle)
            ctx_.post_node_raw(&waiter->completion);
    }
}

auto connection_pool::remove_waiter(pool_waiter* target) -> bool
{
    pool_waiter* previous = nullptr;
    for (auto* waiter = waiters_head_; waiter;
        previous = waiter, waiter = waiter->next)
        if (waiter == target)
        {
            if (previous)
                previous->next = waiter->next;
            else
                waiters_head_ = waiter->next;
            if (waiters_tail_ == waiter)
                waiters_tail_ = previous;
            waiter->next = nullptr;
            dec_if_positive(waiters_count_);
            if (num_pending_requests_ > 0)
                --num_pending_requests_;
            return true;
        }
    return false;
}

auto connection_pool::release_connection(conn_node& node) -> bool
{
    const auto next = node.conn->is_open() ? conn_state::idle : conn_state::dead;
    auto expected = conn_state::in_use;
    if (!node.state.compare_exchange_strong(expected, next,
            std::memory_order_release, std::memory_order_relaxed))
    {
        if (expected == conn_state::retired_in_use &&
            node.state.compare_exchange_strong(expected, conn_state::dead,
                std::memory_order_release, std::memory_order_relaxed))
        {
            node.conn->close();
            return true;
        }
        return false;
    }
    if (next == conn_state::idle)
    {
        node.last_used = std::chrono::steady_clock::now();
        set_idle_bit(node.index);
    }
    else
        node.maintenance_cancellation.cancel();
    if (auto handle = std::exchange(node.task_waiting, {}))
        ctx_.post(handle);
    return true;
}

void connection_pool::return_connection(conn_node& node)
{
    if (waiters_count_.load(std::memory_order_acquire) == 0 && release_connection(node))
        return;
    if (mtx_.try_lock())
    {
        if (release_connection(node) || waiters_head_)
            notify_waiters_with_idle_locked();
        mtx_.unlock();
        return;
    }
    node.return_owner = this;
    node.return_notification.callback = &connection_pool::dispatch_return;
    node.return_notification.callback_arg = &node;
    maintenance_.add();
    ctx_.post_node_raw(&node.return_notification);
}

void connection_pool::dispatch_return(void* argument) noexcept
{
    auto& node = *static_cast<conn_node*>(argument);
    auto& owner = *node.return_owner;
    if (!owner.mtx_.try_lock())
    {
        owner.ctx_.post_node_raw(&node.return_notification);
        return;
    }
    (void)owner.release_connection(node);
    if (owner.waiters_head_)
        owner.notify_waiters_with_idle_locked();
    owner.mtx_.unlock();
    // Completion may resume shutdown; do not access the pool or node afterward.
    owner.maintenance_.done();
}

auto sharded_connection_pool::async_get_connection(cancel_token& token)
    -> task<std::expected<pooled_connection, std::error_code>>
{
    const auto primary =
        next_shard_.fetch_add(1, std::memory_order_relaxed) % shards_.size();
    if (auto conn = try_borrow_immediate(primary))
        co_return std::move(*conn);
    co_return co_await shards_[select_wait_shard(primary)]->async_get_connection(
        token);
}

auto sharded_connection_pool::async_get_connection(cnetmod::deadline value)
    -> task<std::expected<pooled_connection, std::error_code>>
{
    const auto primary =
        next_shard_.fetch_add(1, std::memory_order_relaxed) % shards_.size();
    if (auto conn = try_borrow_immediate(primary))
        co_return std::move(*conn);
    co_return co_await shards_[select_wait_shard(primary)]
        ->async_get_connection(value);
}

auto sharded_connection_pool::async_get_connection()
    -> task<std::expected<pooled_connection, std::error_code>>
{
    const auto primary =
        next_shard_.fetch_add(1, std::memory_order_relaxed) % shards_.size();
    if (auto conn = try_borrow_immediate(primary))
        co_return std::move(*conn);
    co_return co_await shards_[select_wait_shard(primary)]
        ->async_get_connection();
}

auto sharded_connection_pool::async_get_connection(io_context& io)
    -> task<std::expected<pooled_connection, std::error_code>>
{
    const auto primary = get_shard_index(io);
    if (auto conn = try_borrow_immediate(primary))
        co_return std::move(*conn);
    co_return co_await shards_[select_wait_shard(primary)]
        ->async_get_connection();
}

auto sharded_connection_pool::async_get_connection(io_context& io,
    cancel_token& token)
    -> task<std::expected<pooled_connection, std::error_code>>
{
    const auto primary = get_shard_index(io);
    if (auto conn = try_borrow_immediate(primary))
        co_return std::move(*conn);
    co_return co_await shards_[select_wait_shard(primary)]->async_get_connection(
        token);
}

sharded_connection_pool::sharded_connection_pool(io_context& ctx,
    pool_params params,
    std::size_t count)
    : base_params_(std::move(params)), fallback_ctx_(&ctx)
{
    init_shards({&ctx}, count == 0 ? 1 : count);
}

sharded_connection_pool::sharded_connection_pool(
    std::vector<io_context*> contexts, pool_params params)
    : sharded_connection_pool(std::move(contexts), std::move(params),
          contexts.empty() ? 1 : contexts.size()) {}

sharded_connection_pool::sharded_connection_pool(
    std::vector<io_context*> contexts, pool_params params, std::size_t count)
    : base_params_(std::move(params))
{
    if (contexts.empty())
        throw std::invalid_argument(
            "sharded_connection_pool requires at least one io_context");
    fallback_ctx_ = contexts.front();
    init_shards(contexts, count == 0 ? 1 : count);
}

auto sharded_connection_pool::async_run() -> task<void>
{
    for (std::size_t i = 0; i < shards_.size(); ++i)
        spawn(*shard_ctxs_[i], shards_[i]->async_run());
    co_return;
}

auto sharded_connection_pool::cancel() -> task<void>
{
    for (auto& shard : shards_)
        co_await shard->cancel();
}

auto sharded_connection_pool::size() const noexcept -> std::size_t
{
    std::size_t total = 0;
    for (const auto& shard : shards_)
        total += shard->size();
    return total;
}

auto sharded_connection_pool::idle_count() const noexcept -> std::size_t
{
    std::size_t total = 0;
    for (const auto& shard : shards_)
        total += shard->idle_count();
    return total;
}

auto sharded_connection_pool::shard_count() const noexcept -> std::size_t
{
    return shards_.size();
}

auto sharded_connection_pool::get_shard_index(io_context& io) -> std::size_t
{
    if (auto it = shard_by_ctx_.find(&io); it != shard_by_ctx_.end())
        return it->second;
    return next_shard_.fetch_add(1, std::memory_order_relaxed) % shards_.size();
}

auto sharded_connection_pool::try_borrow_immediate(std::size_t primary)
    -> std::expected<pooled_connection, std::error_code>
{
    if (primary < shards_.size())
        if (auto conn = shards_[primary]->try_get_connection())
            return std::move(*conn);
    if (shards_.size() <= 1)
        return std::unexpected(
            make_error_code(std::errc::resource_unavailable_try_again));
    const auto start =
        next_shard_.fetch_add(1, std::memory_order_relaxed) % shards_.size();
    for (std::size_t i = 0; i < shards_.size(); ++i)
    {
        const auto index = (start + i) % shards_.size();
        if (index != primary)
            if (auto conn = shards_[index]->try_get_connection())
                return std::move(*conn);
    }
    return std::unexpected(
        make_error_code(std::errc::resource_unavailable_try_again));
}

auto sharded_connection_pool::select_wait_shard(std::size_t preferred)
    -> std::size_t
{
    if (preferred >= shards_.size())
        preferred = 0;
    auto best = preferred;
    auto waiting = shards_[best]->waiter_count();
    for (std::size_t i = 0; i < shards_.size(); ++i)
        if (auto current = shards_[i]->waiter_count(); current < waiting)
        {
            waiting = current;
            best = i;
        }
    return best;
}

void sharded_connection_pool::init_shards(
    const std::vector<io_context*>& workers, std::size_t count)
{
    std::vector<io_context*> contexts;
    for (auto* ctx : workers)
        if (ctx)
            contexts.push_back(ctx);
    if (contexts.empty())
        throw std::invalid_argument(
            "sharded_connection_pool has no valid io_context");
    const auto initial = (base_params_.initial_size + count - 1) / count;
    const auto maximum = (base_params_.max_size + count - 1) / count;
    shards_.reserve(count);
    shard_ctxs_.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        auto* ctx = contexts[i % contexts.size()];
        auto params = base_params_;
        params.initial_size = initial;
        params.max_size = maximum;
        shard_ctxs_.push_back(ctx);
        shards_.push_back(
            std::make_unique<connection_pool>(*ctx, std::move(params)));
    }
    for (std::size_t i = 0; i < contexts.size(); ++i)
        shard_by_ctx_.try_emplace(contexts[i], i % shards_.size());
}

} // namespace cnetmod::redis
