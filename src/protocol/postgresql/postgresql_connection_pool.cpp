module cnetmod.protocol.postgresql;

import std;
import :connection_pool;
import cnetmod.coro.timer;
import cnetmod.coro.mutex;
import cnetmod.coro.task_group;

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
            slots_.emplace_back();
            slots_.back().connection = std::move(connection);
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

auto connection_pool::warm_up(cancel_token& cancellation)
    -> task<std::expected<void, std::error_code>>
{
    if (cancellation.is_cancelled())
        co_return std::unexpected(make_error_code(std::errc::operation_canceled));
    {
        co_await state_mutex_.lock();
        async_lock_guard guard{state_mutex_, std::adopt_lock};
        if (closing_)
            co_return std::unexpected(make_error_code(std::errc::operation_canceled));
        if (reconnect_tasks_ || reconnect_dispatch_error_)
        {
            const auto completed = reconnect_tasks_ ? reconnect_tasks_->completion_result()
                                                    : std::optional<std::error_code>{std::error_code{}};
            if (completed && (*completed || reconnect_dispatch_error_))
            {
                reconnect_tasks_.reset();
                reconnect_dispatch_error_.clear();
                for (std::size_t index = 0; index < slots_.size(); ++index)
                {
                    auto& entry = slots_[index];
                    if (entry.discard && !entry.in_use)
                    {
                        entry.connecting = false;
                        release_locked(index, true);
                    }
                }
            }
        }
    }
    // Hold leases until the target is reached so acquire cannot repeatedly
    // return the same idle connection. Existing capacity is reused.
    std::vector<pooled_connection> leases;
    std::error_code failure;
    try
    {
        leases.reserve(options_.minimum_connections);
        for (std::size_t index = 0; index < options_.minimum_connections; ++index)
        {
            auto connection = co_await acquire(cancellation);
            if (!connection)
            {
                failure = connection.error();
                break;
            }
            leases.push_back(std::move(*connection));
        }
    }
    catch (const std::bad_alloc&)
    {
        failure = make_error_code(std::errc::not_enough_memory);
    }
    catch (const std::system_error& error)
    {
        failure = error.code();
    }
    catch (...)
    {
        failure = make_error_code(std::errc::io_error);
    }
    if (failure)
    {
        co_await state_mutex_.lock();
        async_lock_guard guard{state_mutex_, std::adopt_lock};
        for (auto& lease : leases)
        {
            const auto index = lease.slot_;
            lease.owner_ = nullptr;
            lease.connection_ = nullptr;
            slots_[index].connection.reset();
            release_locked(index, true);
        }
        co_return std::unexpected(failure);
    }
    co_return {};
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
    if (closing_ || cancellation.is_cancelled())
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
        slots_.emplace_back();
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

            void await_suspend(std::coroutine_handle<> handle)
            {
                pending.handle = handle;
                if (pool.closing_ || token.is_cancelled())
                {
                    guard.release();
                    pool.state_mutex_.unlock();
                    pool.post_waiter(pending);
                    return;
                }
                pool.waiters_.push_back(&pending);
                pending.queued = true;
                pending.registered = true;
                pending.owner = &pool;
                ++pool.active_waiters_;
                pending.cancellation_notification.callback = &connection_pool::dispatch_cancellation;
                pending.cancellation_notification.callback_arg = &pending;
                auto notify = [](void* argument) noexcept
                {
                    auto* item = static_cast<waiter*>(argument);
                    item->owner->context_.post_node_raw(&item->cancellation_notification);
                };
                if (!token.register_callback(&pending, notify))
                    notify(&pending);
                pool.refresh_snapshots_locked();
                guard.release();
                pool.state_mutex_.unlock();
            }

            void await_resume() noexcept
            {
                token.finish_callback(&pending);
            }
        };

        try
        {
            co_await queue_awaitable{*this, pending, guard, cancellation};
        }
        catch (const std::bad_alloc&)
        {
            co_return std::unexpected(make_error_code(std::errc::not_enough_memory));
        }
        co_await state_mutex_.lock();
        async_lock_guard resumed_guard{state_mutex_, std::adopt_lock};
        remove_waiter(&pending);
        if (pending.registered)
            --active_waiters_;
        refresh_snapshots_locked();
        if (assigned.valid())
            co_return std::move(assigned);
        co_return std::unexpected(make_error_code(std::errc::operation_canceled));
    }
    guard.release();
    state_mutex_.unlock();
    std::unique_ptr<client> connection;
    std::error_code connection_error;
    try
    {
        connection = std::make_unique<client>(context_);
        auto result = co_await connection->connect(options_.connection, cancellation);
        if (result.is_err())
        {
            connection_error = connection->last_error();
            if (!connection_error)
                connection_error = make_error_code(std::errc::connection_refused);
        }
    }
    catch (const std::bad_alloc&)
    {
        connection_error = make_error_code(std::errc::not_enough_memory);
    }
    catch (...)
    {
        connection_error = make_error_code(std::errc::io_error);
    }
    if (connection_error)
    {
        co_await state_mutex_.lock();
        async_lock_guard failure_guard{state_mutex_, std::adopt_lock};
        slots_[reserved].connecting = false;
        refresh_snapshots_locked();
        failure_guard.release();
        state_mutex_.unlock();
        release(reserved, true);
        co_return std::unexpected(connection_error);
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
    /**
     * Keep the uncontended lease-return path synchronous. A contended return
     * uses storage owned by its stable deque slot, not a detached coroutine.
     * The slot remains checked out until dispatch, so close cannot erase it.
     */
    if (state_mutex_.try_lock())
    {
        release_locked(slot_index, discard);
        state_mutex_.unlock();
        return;
    }
    auto& entry = slots_[slot_index];
    entry.owner = this;
    entry.index = slot_index;
    entry.return_discard = discard;
    entry.return_notification.callback = &connection_pool::dispatch_return;
    entry.return_notification.callback_arg = &entry;
    context_.post_node_raw(&entry.return_notification);
}

void connection_pool::dispatch_return(void* argument) noexcept
{
    auto& entry = *static_cast<slot*>(argument);
    auto& owner = *entry.owner;
    if (!owner.state_mutex_.try_lock())
    {
        owner.context_.post_node_raw(&entry.return_notification);
        return;
    }
    owner.release_locked(entry.index, entry.return_discard);
    // Unlock may resume shutdown and destroy the slot. Do not access it again.
    owner.state_mutex_.unlock();
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
            try
            {
                if (!reconnect_tasks_ || reconnect_tasks_->completion_result().has_value())
                    reconnect_tasks_ = std::make_unique<task_group>(context_);
                if (!reconnect_tasks_->run([this, slot_index](cancel_token& cancellation)
                        {
                            return reconnect_discarded_slot(slot_index, cancellation);
                        }))
                {
                    slots_[slot_index].connecting = false;
                    reconnect_dispatch_error_ = make_error_code(std::errc::operation_canceled);
                }
            }
            catch (const std::bad_alloc&)
            {
                slots_[slot_index].connecting = false;
                reconnect_dispatch_error_ = make_error_code(std::errc::not_enough_memory);
            }
            catch (...)
            {
                // release is noexcept; leave the discarded slot retryable.
                slots_[slot_index].connecting = false;
                reconnect_dispatch_error_ = make_error_code(std::errc::io_error);
            }
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
            !pending->cancellation->complete_callback(pending))
            continue;
        if (!slots_[slot_index].discard && !closing_)
        {
            *pending->result = pooled_connection(this, slot_index, slots_[slot_index].connection.get());
            refresh_snapshots_locked();
            post_waiter(*pending);
            return;
        }
        post_waiter(*pending);
    }
    slots_[slot_index].in_use = false;
    refresh_snapshots_locked();
}

auto connection_pool::reconnect_discarded_slot(std::size_t slot_index, cancel_token& cancellation)
    -> task<std::expected<void, std::error_code>>
{
    std::unique_ptr<client> replacement;
    bool connected = false;
    std::error_code exception;
    try
    {
        replacement = std::make_unique<client>(context_);
        auto result = co_await replacement->connect(options_.connection, cancellation);
        connected = !result.is_err();
        if (!connected)
            exception = replacement->last_error() ? replacement->last_error()
                                                  : make_error_code(std::errc::io_error);
    }
    catch (const std::bad_alloc&)
    {
        exception = make_error_code(std::errc::not_enough_memory);
    }
    catch (const std::system_error& error)
    {
        exception = error.code() ? error.code() : make_error_code(std::errc::io_error);
    }
    catch (...)
    {
        exception = make_error_code(std::errc::io_error);
    }
    co_await state_mutex_.lock();
    async_lock_guard guard{state_mutex_, std::adopt_lock};
    if (closing_ || slot_index >= slots_.size())
        co_return std::expected<void, std::error_code>{};
    auto& target = slots_[slot_index];
    target.connecting = false;
    if (!connected)
    {
        target.discard = true;
        target.in_use = false;
        refresh_snapshots_locked();
        if (exception)
            co_return std::unexpected(exception);
        co_return std::expected<void, std::error_code>{};
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
            !pending->cancellation->complete_callback(pending))
            continue;
        *pending->result = pooled_connection(this, slot_index, target.connection.get());
        refresh_snapshots_locked();
        post_waiter(*pending);
        co_return std::expected<void, std::error_code>{};
    }
    target.in_use = false;
    refresh_snapshots_locked();
    co_return std::expected<void, std::error_code>{};
}

void connection_pool::post_waiter(waiter& pending) noexcept
{
    /**
     * Completion ownership excludes a queued cancellation notification. Reuse
     * that frame-owned node so handing off a lease cannot allocate or throw.
     */
    pending.cancellation_notification.callback = nullptr;
    pending.cancellation_notification.coroutine = pending.handle;
    context_.post_node_raw(&pending.cancellation_notification);
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

void connection_pool::dispatch_cancellation(void* argument) noexcept
{
    auto* target = static_cast<waiter*>(argument);
    auto& owner = *target->owner;
    if (!owner.state_mutex_.try_lock())
    {
        owner.context_.post_node_raw(&target->cancellation_notification);
        return;
    }
    auto handle = target->handle;
    owner.remove_waiter(target);
    owner.refresh_snapshots_locked();
    owner.state_mutex_.unlock();
    /**
     * The active waiter keeps its pool alive through close's settlement loop.
     * Resuming may destroy the waiter; no notification storage is read again.
     */
    handle.resume();
}

template <bool Cancellable>
auto connection_pool::close_impl(cancel_token* cancellation)
    -> task<std::conditional_t<Cancellable, std::expected<void, std::error_code>, void>>
{
    if constexpr (Cancellable)
    {
        if (cancellation->is_cancelled())
            co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        while (!close_mutex_.try_lock())
        {
            if (cancellation->is_cancelled())
                co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
            co_await async_sleep(context_, std::chrono::milliseconds{1});
        }
    }
    else
        co_await close_mutex_.lock();
    async_lock_guard close_guard{close_mutex_, std::adopt_lock};
    co_await state_mutex_.lock();
    async_lock_guard closing_guard{state_mutex_, std::adopt_lock};
    closing_ = true;
    for (auto* pending : waiters_)
    {
        pending->queued = false;
        if (pending->cancellation)
        {
            /**
             * Cancellation may already own the deferred resume. Shutdown must
             * not publish a second resume or mutate that callback's storage.
             */
            if (!pending->cancellation->complete_callback(pending))
                continue;
        }
        post_waiter(*pending);
    }
    waiters_.clear();
    refresh_snapshots_locked();
    closing_guard.release();
    state_mutex_.unlock();
    if (reconnect_tasks_)
    {
        reconnect_tasks_->cancel();
        if constexpr (Cancellable)
        {
            while (!reconnect_tasks_->completion_result().has_value())
            {
                if (cancellation->is_cancelled())
                    co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
                co_await async_sleep(context_, std::chrono::milliseconds{1});
            }
        }
        else
            co_await reconnect_tasks_->settle();
    }
    for (;;)
    {
        if constexpr (Cancellable)
        {
            if (cancellation->is_cancelled())
                co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        }
        bool borrowed{};
        co_await state_mutex_.lock();
        async_lock_guard state_guard{state_mutex_, std::adopt_lock};
        borrowed = active_waiters_ != 0 || std::ranges::any_of(slots_, [](const slot& entry)
                                               {
                                                   return entry.in_use;
                                               });
        if (!borrowed)
            break;
        state_guard.release();
        state_mutex_.unlock();
        co_await async_sleep(context_, std::chrono::milliseconds(1));
    }
    /**
     * Shutdown owns every remaining slot after borrowers and callbacks settle.
     * Retain storage until transport cleanup succeeds, so a failed close can be
     * retried without allocating an empty staging container or losing owners.
     */
    for (auto& entry : slots_)
        if (entry.connection)
        {
            if constexpr (Cancellable)
            {
                auto result = co_await entry.connection->terminate(*cancellation);
                if (!result)
                    co_return std::unexpected(result.error());
            }
            else
                co_await entry.connection->terminate();
        }
    co_await state_mutex_.lock();
    async_lock_guard final_guard{state_mutex_, std::adopt_lock};
    slots_.clear();
    refresh_snapshots_locked();
    if constexpr (Cancellable)
        co_return std::expected<void, std::error_code>{};
    else
        co_return;
}

auto connection_pool::close() -> task<void>
{
    return close_impl<false>(nullptr);
}

auto connection_pool::close(cancel_token& cancellation) -> task<std::expected<void, std::error_code>>
{
    return close_impl<true>(&cancellation);
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

auto connection_pool::background_error() const noexcept -> std::error_code
{
    if (reconnect_dispatch_error_)
        return reconnect_dispatch_error_;
    if (!reconnect_tasks_)
        return {};
    return reconnect_tasks_->completion_result().value_or(std::error_code{});
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
