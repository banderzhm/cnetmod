module cnetmod.protocol.postgresql;

import std;
import :connection_pool;
import cnetmod.coro.timer;
import cnetmod.coro.spawn;
import cnetmod.coro.mutex;

namespace cnetmod::postgresql {

pooled_connection::pooled_connection(connection_pool* owner, std::size_t slot,
    client* connection) noexcept : owner_(owner), slot_(slot), connection_(connection) {}

pooled_connection::pooled_connection(pooled_connection&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), slot_(other.slot_), connection_(std::exchange(other.connection_, nullptr)) {}

auto pooled_connection::operator=(pooled_connection&& other) noexcept -> pooled_connection&
{
    if (this != &other)
    {
        if (owner_)
            owner_->release(slot_, false);
        owner_ = std::exchange(other.owner_, nullptr);
        slot_ = other.slot_;
        connection_ = std::exchange(other.connection_, nullptr);
    }
    return *this;
}

pooled_connection::~pooled_connection()
{
    if (owner_)
        owner_->release(slot_, false);
}

auto pooled_connection::valid() const noexcept -> bool
{
    return connection_ != nullptr;
}

auto pooled_connection::operator->() noexcept -> client*
{
    return connection_;
}

auto pooled_connection::get() noexcept -> client&
{
    return *connection_;
}

void pooled_connection::discard() noexcept
{
    if (owner_)
        owner_->release(slot_, true);
    owner_ = nullptr;
    connection_ = nullptr;
}

connection_pool::connection_pool(io_context& context, connection_pool_options options)
    : context_(context), options_(std::move(options))
{
    options_.maximum_connections = std::max<std::size_t>(1, options_.maximum_connections);
    options_.minimum_connections = std::min(options_.minimum_connections, options_.maximum_connections);
}

auto connection_pool::warm_up() -> task<result_set>
{
    while (size() < options_.minimum_connections)
    {
        auto connection = std::make_unique<client>(context_);
        auto result = co_await connection->connect(options_.connection);
        if (result.is_err())
            co_return result;
        bool closing{};
        co_await state_mutex_.lock();
        async_lock_guard guard{state_mutex_, std::adopt_lock};
        closing = closing_;
        if (!closing)
        {
            slots_.push_back({std::move(connection), false, false});
            refresh_snapshots_locked();
        }
        if (closing)
        {
            guard.release();
            state_mutex_.unlock();
            co_await connection->terminate();
            co_return result_set{};
        }
    }
    co_return result_set{};
}

auto connection_pool::acquire() -> task<std::expected<pooled_connection, std::error_code>>
{
    cancel_token cancellation;
    auto result = co_await with_timeout(context_, options_.acquire_timeout,
        acquire(cancellation), cancellation);
    if (!result && result.error() == make_error_code(std::errc::operation_canceled))
        co_return std::unexpected(make_error_code(std::errc::timed_out));
    co_return result;
}

auto connection_pool::acquire(cancel_token& cancellation)
    -> task<std::expected<pooled_connection, std::error_code>>
{
    std::size_t reserved = std::numeric_limits<std::size_t>::max();
    co_await state_mutex_.lock();
    async_lock_guard guard{state_mutex_, std::adopt_lock};
    if (closing_)
        co_return std::unexpected(make_error_code(std::errc::operation_canceled));
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (!slots_[i].in_use && !slots_[i].discard)
        {
            slots_[i].in_use = true;
            refresh_snapshots_locked();
            co_return pooled_connection(this, i, slots_[i].connection.get());
        }
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (!slots_[i].in_use && slots_[i].discard && !slots_[i].connecting)
        {
            slots_[i].connection.reset();
            slots_[i].discard = false;
            slots_[i].in_use = true;
            slots_[i].connecting = true;
            reserved = i;
            refresh_snapshots_locked();
            break;
        }
    if (reserved == std::numeric_limits<std::size_t>::max() &&
        slots_.size() < options_.maximum_connections)
    {
        slots_.push_back({});
        reserved = slots_.size() - 1;
        slots_[reserved].in_use = true;
        refresh_snapshots_locked();
    }
    if (reserved == std::numeric_limits<std::size_t>::max())
    {
        pooled_connection assigned;
        waiter pending{.result = &assigned, .cancellation = &cancellation};

        struct queue_awaitable
        {
            connection_pool& pool;
            waiter& pending;
            async_lock_guard& guard;
            cancel_token& token;

            auto await_ready() const noexcept -> bool
            {
                return false;
            }

            void await_suspend(std::coroutine_handle<> handle) noexcept
            {
                pending.handle = handle;
                if (pool.closing_ || token.is_cancelled())
                {
                    guard.release();
                    pool.state_mutex_.unlock();
                    pool.context_.post(handle);
                    return;
                }
                pending.queued = true;
                pool.waiters_.push_back(&pending);
                token.ctx_ = &pool;
                token.io_handle_ = &pending;
                token.coroutine_ = handle;
                token.cancel_fn_ = [](cancel_token& cancelled) noexcept
                {
                    if (!cancelled.pending_.exchange(false, std::memory_order_acq_rel))
                        return;
                    auto* owner = static_cast<connection_pool*>(cancelled.ctx_);
                    auto* item = static_cast<waiter*>(cancelled.io_handle_);
                    auto handle = cancelled.coroutine_;
                    if (owner->state_mutex_.try_lock())
                    {
                        owner->remove_waiter(item);
                        owner->refresh_snapshots_locked();
                        owner->state_mutex_.unlock();
                        owner->context_.post(handle);
                        return;
                    }
                    spawn(owner->context_, owner->cancel_waiter_async(item, handle));
                };
                token.pending_.store(true, std::memory_order_release);
                pool.refresh_snapshots_locked();
                guard.release();
                pool.state_mutex_.unlock();
                if (token.is_cancelled())
                {
                    token.cancel_fn_(token);
                }
            }

            void await_resume() noexcept
            {
                token.pending_.store(false, std::memory_order_release);
                token.cancel_fn_ = nullptr;
            }
        };

        co_await queue_awaitable{*this, pending, guard, cancellation};
        co_await state_mutex_.lock();
        async_lock_guard resumed_guard{state_mutex_, std::adopt_lock};
        remove_waiter(&pending);
        refresh_snapshots_locked();
        if (assigned.valid())
            co_return std::move(assigned);
        co_return std::unexpected(make_error_code(std::errc::operation_canceled));
    }
    guard.release();
    state_mutex_.unlock();
    auto connection = std::make_unique<client>(context_);
    auto result = co_await connection->connect(options_.connection);
    if (result.is_err())
    {
        co_await state_mutex_.lock();
        async_lock_guard failure_guard{state_mutex_, std::adopt_lock};
        slots_[reserved].connecting = false;
        refresh_snapshots_locked();
        failure_guard.release();
        state_mutex_.unlock();
        release(reserved, true);
        co_return std::unexpected(make_error_code(std::errc::connection_refused));
    }
    client* raw = connection.get();
    co_await state_mutex_.lock();
    async_lock_guard connected_guard{state_mutex_, std::adopt_lock};
    if (closing_)
    {
        slots_[reserved].connecting = false;
        slots_[reserved].in_use = false;
        slots_[reserved].discard = true;
        refresh_snapshots_locked();
        connected_guard.release();
        state_mutex_.unlock();
        co_await connection->terminate();
        co_return std::unexpected(make_error_code(std::errc::operation_canceled));
    }
    slots_[reserved].connection = std::move(connection);
    slots_[reserved].connecting = false;
    refresh_snapshots_locked();
    co_return pooled_connection(this, reserved, raw);
}

void connection_pool::release(std::size_t slot_index, bool discard) noexcept
{
    // Lease destruction is synchronous, so it cannot co_await.  Prefer the
    // uncontended fast path; under contention, transfer the release to the
    // pool executor where it acquires the same coroutine mutex.  The lease is
    // never dropped and the mutex is never held during client I/O.
    if (state_mutex_.try_lock())
    {
        release_locked(slot_index, discard);
        state_mutex_.unlock();
        return;
    }
    spawn(context_, release_async(slot_index, discard));
}

auto connection_pool::release_async(std::size_t slot_index, bool discard) -> task<void>
{
    co_await state_mutex_.lock();
    async_lock_guard guard{state_mutex_, std::adopt_lock};
    release_locked(slot_index, discard);
}

void connection_pool::release_locked(std::size_t slot_index, bool discard) noexcept
{
    if (slot_index >= slots_.size())
        return;
    slots_[slot_index].discard |= discard;
    if (slots_[slot_index].discard)
    {
        slots_[slot_index].in_use = false;
        if (!closing_ && !waiters_.empty() && !slots_[slot_index].connecting)
        {
            slots_[slot_index].connecting = true;
            spawn(context_, reconnect_discarded_slot(slot_index));
        }
        refresh_snapshots_locked();
        return;
    }
    while (!waiters_.empty())
    {
        auto* pending = waiters_.front();
        waiters_.pop_front();
        if (!pending->queued)
            continue;
        pending->queued = false;
        // The cancellation callback and a synchronous pooled_connection
        // destructor may run on different threads. Claim this waiter before
        // publishing the slot: cancellation winning here owns the only resume
        // and this lease remains available for the next FIFO waiter.
        if (pending->cancellation &&
            !pending->cancellation->pending_.exchange(false,
                std::memory_order_acq_rel))
            continue;
        if (!slots_[slot_index].discard && !closing_)
        {
            *pending->result = pooled_connection(this, slot_index, slots_[slot_index].connection.get());
            refresh_snapshots_locked();
            context_.post(pending->handle);
            return;
        }
        context_.post(pending->handle);
    }
    slots_[slot_index].in_use = false;
    refresh_snapshots_locked();
}

auto connection_pool::reconnect_discarded_slot(std::size_t slot_index) -> task<void>
{
    auto replacement = std::make_unique<client>(context_);
    auto result = co_await replacement->connect(options_.connection);
    co_await state_mutex_.lock();
    async_lock_guard guard{state_mutex_, std::adopt_lock};
    if (closing_ || slot_index >= slots_.size())
        co_return;
    auto& target = slots_[slot_index];
    target.connecting = false;
    if (result.is_err())
    {
        target.discard = true;
        target.in_use = false;
        refresh_snapshots_locked();
        co_return;
    }
    target.connection = std::move(replacement);
    target.discard = false;
    target.in_use = true;
    while (!waiters_.empty())
    {
        auto* pending = waiters_.front();
        waiters_.pop_front();
        if (!pending->queued)
            continue;
        pending->queued = false;
        if (pending->cancellation &&
            !pending->cancellation->pending_.exchange(false,
                std::memory_order_acq_rel))
            continue;
        *pending->result = pooled_connection(this, slot_index, target.connection.get());
        refresh_snapshots_locked();
        context_.post(pending->handle);
        co_return;
    }
    target.in_use = false;
    refresh_snapshots_locked();
}

void connection_pool::remove_waiter(waiter* target) noexcept
{
    if (!target || !target->queued)
        return;
    auto found = std::ranges::find(waiters_, target);
    if (found != waiters_.end())
        waiters_.erase(found);
    target->queued = false;
}

auto connection_pool::cancel_waiter_async(waiter* target,
    std::coroutine_handle<> handle) -> task<void>
{
    co_await state_mutex_.lock();
    async_lock_guard guard{state_mutex_, std::adopt_lock};
    remove_waiter(target);
    refresh_snapshots_locked();
    guard.release();
    state_mutex_.unlock();
    context_.post(handle);
}

auto connection_pool::close() -> task<void>
{
    std::deque<slot> slots;
    co_await state_mutex_.lock();
    async_lock_guard closing_guard{state_mutex_, std::adopt_lock};
    closing_ = true;
    for (auto* pending : waiters_)
    {
        pending->queued = false;
        if (pending->cancellation)
        {
            pending->cancellation->pending_.exchange(false,
                std::memory_order_acq_rel);
            pending->cancellation->cancel_fn_ = nullptr;
        }
        context_.post(pending->handle);
    }
    waiters_.clear();
    refresh_snapshots_locked();
    closing_guard.release();
    state_mutex_.unlock();
    for (;;)
    {
        bool borrowed{};
        co_await state_mutex_.lock();
        async_lock_guard state_guard{state_mutex_, std::adopt_lock};
        borrowed = std::ranges::any_of(slots_, [](const slot& entry)
            {
                return entry.in_use;
            });
        if (!borrowed)
        {
            slots.swap(slots_);
            refresh_snapshots_locked();
        }
        if (!borrowed)
            break;
        state_guard.release();
        state_mutex_.unlock();
        co_await async_sleep(context_, std::chrono::milliseconds(1));
    }
    for (auto& entry : slots)
        if (entry.connection)
            co_await entry.connection->terminate();
    co_return;
}

auto connection_pool::size() const noexcept -> std::size_t
{
    return size_snapshot_.load(std::memory_order_acquire);
}

auto connection_pool::idle_count() const noexcept -> std::size_t
{
    return idle_snapshot_.load(std::memory_order_acquire);
}

auto connection_pool::checked_out_count() const noexcept -> std::size_t
{
    return checked_out_snapshot_.load(std::memory_order_acquire);
}

auto connection_pool::waiter_count() const noexcept -> std::size_t
{
    return waiter_snapshot_.load(std::memory_order_acquire);
}

void connection_pool::refresh_snapshots_locked() noexcept
{
    size_snapshot_.store(slots_.size(), std::memory_order_release);
    idle_snapshot_.store(std::ranges::count_if(slots_, [](const slot& entry)
                             {
                                 return entry.connection && !entry.in_use && !entry.discard;
                             }),
        std::memory_order_release);
    checked_out_snapshot_.store(std::ranges::count_if(slots_, [](const slot& entry)
                                    {
                                        return entry.connection && entry.in_use;
                                    }),
        std::memory_order_release);
    waiter_snapshot_.store(waiters_.size(), std::memory_order_release);
}

} // namespace cnetmod::postgresql
