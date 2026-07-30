module cnetmod.protocol.postgresql;

import std;
import :connection_pool;
import cnetmod.coro.timer;
import cnetmod.coro.spawn;

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
        {
            std::scoped_lock lock(mutex_);
            closing = closing_;
            if (!closing)
                slots_.push_back({std::move(connection), false, false});
        }
        if (closing)
        {
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
    {
        std::scoped_lock lock(mutex_);
        if (closing_)
            co_return std::unexpected(make_error_code(std::errc::operation_canceled));
        for (std::size_t i = 0; i < slots_.size(); ++i)
            if (!slots_[i].in_use && !slots_[i].discard)
            {
                slots_[i].in_use = true;
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
                break;
            }
        if (reserved == std::numeric_limits<std::size_t>::max() &&
            slots_.size() >= options_.maximum_connections)
        {
            // Full pool: FIFO suspend below rather than rejecting/busy polling.
        }
        else
        {
            if (reserved == std::numeric_limits<std::size_t>::max())
            {
                slots_.push_back({});
                reserved = slots_.size() - 1;
                slots_[reserved].in_use = true;
            }
        }
    }
    if (reserved == std::numeric_limits<std::size_t>::max())
    {
        pooled_connection assigned;
        waiter pending{.result = &assigned, .cancellation = &cancellation};

        struct queue_awaitable
        {
            connection_pool& pool;
            waiter& pending;
            cancel_token& token;

            auto await_ready() const noexcept -> bool
            {
                return false;
            }

            void await_suspend(std::coroutine_handle<> handle) noexcept
            {
                pending.handle = handle;
                {
                    std::scoped_lock lock(pool.mutex_);
                    if (pool.closing_ || token.is_cancelled())
                    {
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
                        {
                            std::scoped_lock lock(owner->mutex_);
                            owner->remove_waiter(item);
                        }
                        owner->context_.post(cancelled.coroutine_);
                    };
                    token.pending_.store(true, std::memory_order_release);
                }
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

        co_await queue_awaitable{*this, pending, cancellation};
        {
            std::scoped_lock lock(mutex_);
            remove_waiter(&pending);
        }
        if (assigned.valid())
            co_return std::move(assigned);
        co_return std::unexpected(make_error_code(std::errc::operation_canceled));
    }
    auto connection = std::make_unique<client>(context_);
    auto result = co_await connection->connect(options_.connection);
    if (result.is_err())
    {
        {
            std::scoped_lock lock(mutex_);
            slots_[reserved].connecting = false;
        }
        release(reserved, true);
        co_return std::unexpected(make_error_code(std::errc::connection_refused));
    }
    client* raw = connection.get();
    {
        std::scoped_lock lock(mutex_);
        slots_[reserved].connection = std::move(connection);
        slots_[reserved].connecting = false;
    }
    co_return pooled_connection(this, reserved, raw);
}

void connection_pool::release(std::size_t slot_index, bool discard) noexcept
{
    std::scoped_lock lock(mutex_);
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
        return;
    }
    while (!waiters_.empty())
    {
        auto* pending = waiters_.front();
        waiters_.pop_front();
        if (!pending->queued)
            continue;
        pending->queued = false;
        if (pending->cancellation)
        {
            pending->cancellation->pending_.exchange(false, std::memory_order_acq_rel);
            pending->cancellation->cancel_fn_ = nullptr;
        }
        if (!slots_[slot_index].discard && !closing_)
        {
            *pending->result = pooled_connection(this, slot_index, slots_[slot_index].connection.get());
            context_.post(pending->handle);
            return;
        }
        context_.post(pending->handle);
    }
    slots_[slot_index].in_use = false;
}

auto connection_pool::reconnect_discarded_slot(std::size_t slot_index) -> task<void>
{
    auto replacement = std::make_unique<client>(context_);
    auto result = co_await replacement->connect(options_.connection);
    std::scoped_lock lock(mutex_);
    if (closing_ || slot_index >= slots_.size())
        co_return;
    auto& target = slots_[slot_index];
    target.connecting = false;
    if (result.is_err())
    {
        target.discard = true;
        target.in_use = false;
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
        if (pending->cancellation)
        {
            pending->cancellation->pending_.exchange(false);
            pending->cancellation->cancel_fn_ = nullptr;
        }
        *pending->result = pooled_connection(this, slot_index, target.connection.get());
        context_.post(pending->handle);
        co_return;
    }
    target.in_use = false;
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

auto connection_pool::close() -> task<void>
{
    std::deque<slot> slots;
    {
        std::scoped_lock lock(mutex_);
        closing_ = true;
        for (auto* pending : waiters_)
        {
            pending->queued = false;
            if (pending->cancellation)
            {
                pending->cancellation->pending_.exchange(false);
                pending->cancellation->cancel_fn_ = nullptr;
            }
            context_.post(pending->handle);
        }
        waiters_.clear();
    }
    for (;;)
    {
        bool borrowed{};
        {
            std::scoped_lock lock(mutex_);
            borrowed = std::ranges::any_of(slots_, [](const slot& entry)
                {
                    return entry.in_use;
                });
            if (!borrowed)
                slots.swap(slots_);
        }
        if (!borrowed)
            break;
        co_await async_sleep(context_, std::chrono::milliseconds(1));
    }
    for (auto& entry : slots)
        if (entry.connection)
            co_await entry.connection->terminate();
    co_return;
}

auto connection_pool::size() const noexcept -> std::size_t
{
    std::scoped_lock lock(mutex_);
    return slots_.size();
}

auto connection_pool::idle_count() const noexcept -> std::size_t
{
    std::scoped_lock lock(mutex_);
    return std::ranges::count_if(slots_, [](const slot& s)
        {
            return s.connection && !s.in_use && !s.discard;
        });
}

auto connection_pool::checked_out_count() const noexcept -> std::size_t
{
    std::scoped_lock lock(mutex_);
    return std::ranges::count_if(slots_, [](const slot& s)
        {
            return s.connection && s.in_use;
        });
}

auto connection_pool::waiter_count() const noexcept -> std::size_t
{
    std::scoped_lock lock(mutex_);
    return waiters_.size();
}

} // namespace cnetmod::postgresql
