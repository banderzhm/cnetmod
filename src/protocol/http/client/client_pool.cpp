module cnetmod.protocol.http.client.pool;

import std;
import cnetmod.coro.timer;
import cnetmod.io.io_context;

namespace cnetmod::http {

client_pool::client_pool(cnetmod::io_context& context, client_options options,
    std::size_t max_idle)
    : context_(&context), options_(std::move(options)), max_idle_(max_idle) {}

auto client_pool::acquire() -> std::unique_ptr<client>
{
    if (idle_.empty())
        return std::make_unique<client>(*context_, options_);
    auto value = std::move(idle_.back());
    idle_.pop_back();
    return value;
}

void client_pool::release(std::unique_ptr<client> value)
{
    if (!value)
        return;
    if (idle_.size() >= max_idle_)
    {
        value->close();
        return;
    }
    idle_.push_back(std::move(value));
}

auto client_pool::acquire(client_pool_key key) -> std::unique_ptr<client>
{
    auto found = endpoint_idle_.find(key);
    if (found == endpoint_idle_.end() || found->second.empty())
        return std::make_unique<client>(*context_, options_);
    auto value = std::move(found->second.back());
    found->second.pop_back();
    if (found->second.empty())
        endpoint_idle_.erase(found);
    return value;
}

void client_pool::release(client_pool_key key, std::unique_ptr<client> value)
{
    if (!value)
        return;
    auto& idle = endpoint_idle_[std::move(key)];
    if (idle.size() >= max_idle_)
    {
        value->close();
        return;
    }
    idle.push_back(std::move(value));
}

void client_pool::clear() noexcept
{
    for (auto& value : idle_)
        value->close();
    idle_.clear();
    for (auto& [_, values] : endpoint_idle_)
        for (auto& value : values)
            value->close();
    endpoint_idle_.clear();
}

auto client_pool::idle_count() const noexcept -> std::size_t
{
    auto count = idle_.size();
    for (const auto& [_, values] : endpoint_idle_)
        count += values.size();
    return count;
}

pooled_client::pooled_client(bounded_client_pool* owner, std::size_t slot,
    client* value) noexcept
    : owner_(owner), slot_(slot), value_(value)
{
}

pooled_client::pooled_client(pooled_client&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), slot_(other.slot_), value_(std::exchange(other.value_, nullptr))
{
}

auto pooled_client::operator=(pooled_client&& other) noexcept
    -> pooled_client&
{
    if (this != &other)
    {
        if (owner_)
            owner_->release(slot_, false);
        owner_ = std::exchange(other.owner_, nullptr);
        slot_ = other.slot_;
        value_ = std::exchange(other.value_, nullptr);
    }
    return *this;
}

pooled_client::~pooled_client()
{
    if (owner_)
        owner_->release(slot_, false);
}

auto pooled_client::valid() const noexcept -> bool
{
    return value_ != nullptr;
}

auto pooled_client::operator->() noexcept -> client*
{
    return value_;
}

auto pooled_client::get() noexcept -> client&
{
    return *value_;
}

void pooled_client::discard() noexcept
{
    if (owner_)
        owner_->release(slot_, true);
    owner_ = nullptr;
    value_ = nullptr;
}

bounded_client_pool::bounded_client_pool(io_context& context,
    bounded_client_pool_options options)
    : context_(context), options_(std::move(options))
{
    options_.maximum_clients =
        std::max<std::size_t>(1, options_.maximum_clients);
}

auto bounded_client_pool::acquire()
    -> task<std::expected<pooled_client, std::error_code>>
{
    cancel_token cancellation;
    if (options_.acquire_timeout <= std::chrono::milliseconds::zero())
        co_return co_await acquire(cancellation);
    auto result = co_await with_timeout(context_, options_.acquire_timeout,
        acquire(cancellation), cancellation);
    if (!result &&
        result.error() == make_error_code(std::errc::operation_canceled))
        co_return std::unexpected(make_error_code(std::errc::timed_out));
    co_return result;
}

auto bounded_client_pool::acquire(cancel_token& cancellation)
    -> task<std::expected<pooled_client, std::error_code>>
{
    co_await state_mutex_.lock();
    async_lock_guard guard{state_mutex_, std::adopt_lock};
    if (closing_ || cancellation.is_cancelled())
        co_return std::unexpected(
            make_error_code(std::errc::operation_canceled));

    for (std::size_t index = 0; index < slots_.size(); ++index)
    {
        auto& entry = slots_[index];
        if (entry.checked_out)
            continue;
        entry.checked_out = true;
        --idle_;
        ++checked_out_;
        refresh_snapshots_locked();
        co_return pooled_client{this, index, entry.value.get()};
    }

    if (slots_.size() < options_.maximum_clients)
    {
        try
        {
            slots_.emplace_back();
            auto& entry = slots_.back();
            entry.value = std::make_unique<client>(
                context_, options_.client);
            entry.checked_out = true;
            ++checked_out_;
            refresh_snapshots_locked();
            co_return pooled_client{
                this, slots_.size() - 1, entry.value.get()};
        }
        catch (const std::bad_alloc&)
        {
            if (!slots_.empty() && !slots_.back().value)
                slots_.pop_back();
            refresh_snapshots_locked();
            co_return std::unexpected(
                make_error_code(std::errc::not_enough_memory));
        }
        catch (...)
        {
            if (!slots_.empty() && !slots_.back().value)
                slots_.pop_back();
            refresh_snapshots_locked();
            co_return std::unexpected(make_error_code(std::errc::io_error));
        }
    }

    if (waiters_.size() >= options_.maximum_waiters)
        co_return std::unexpected(
            make_error_code(std::errc::resource_unavailable_try_again));

    pooled_client assigned;
    waiter pending{.result = &assigned, .cancellation = &cancellation};

    struct queue_awaitable
    {
        bounded_client_pool& pool;
        waiter& pending;
        async_lock_guard& guard;
        cancel_token& token;

        [[nodiscard]] auto await_ready() const noexcept -> bool
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
            pending.cancellation_notification.callback =
                &bounded_client_pool::dispatch_cancellation;
            pending.cancellation_notification.callback_arg = &pending;
            auto notify = [](void* argument) noexcept
            {
                auto* item = static_cast<waiter*>(argument);
                item->owner->context_.post_node_raw(
                    &item->cancellation_notification);
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
        co_return std::unexpected(
            make_error_code(std::errc::not_enough_memory));
    }

    co_await state_mutex_.lock();
    async_lock_guard resumed_guard{state_mutex_, std::adopt_lock};
    remove_waiter(&pending);
    if (pending.registered)
        --active_waiters_;
    refresh_snapshots_locked();
    if (assigned.valid())
        co_return std::move(assigned);
    co_return std::unexpected(
        make_error_code(std::errc::operation_canceled));
}

auto bounded_client_pool::close() -> task<void>
{
    co_await close_mutex_.lock();
    async_lock_guard close_guard{close_mutex_, std::adopt_lock};
    co_await state_mutex_.lock();
    async_lock_guard state_guard{state_mutex_, std::adopt_lock};
    closing_ = true;
    for (auto* pending : waiters_)
    {
        pending->queued = false;
        if (pending->cancellation &&
            !pending->cancellation->complete_callback(pending))
            continue;
        post_waiter(*pending);
    }
    waiters_.clear();
    refresh_snapshots_locked();
    state_guard.release();
    state_mutex_.unlock();

    while (true)
    {
        co_await state_mutex_.lock();
        async_lock_guard settlement_guard{state_mutex_, std::adopt_lock};
        if (checked_out_ == 0 && active_waiters_ == 0)
        {
            for (auto& entry : slots_)
                entry.value->close();
            slots_.clear();
            idle_ = 0;
            refresh_snapshots_locked();
            co_return;
        }
        settlement_guard.release();
        state_mutex_.unlock();
        co_await async_sleep(context_, std::chrono::milliseconds{1});
    }
}

auto bounded_client_pool::size() const noexcept -> std::size_t
{
    return size_snapshot_.load(std::memory_order_acquire);
}

auto bounded_client_pool::idle_count() const noexcept -> std::size_t
{
    return idle_snapshot_.load(std::memory_order_acquire);
}

auto bounded_client_pool::checked_out_count() const noexcept -> std::size_t
{
    return checked_out_snapshot_.load(std::memory_order_acquire);
}

auto bounded_client_pool::waiter_count() const noexcept -> std::size_t
{
    return waiter_snapshot_.load(std::memory_order_acquire);
}

void bounded_client_pool::refresh_snapshots_locked() noexcept
{
    size_snapshot_.store(slots_.size(), std::memory_order_release);
    idle_snapshot_.store(idle_, std::memory_order_release);
    checked_out_snapshot_.store(checked_out_, std::memory_order_release);
    waiter_snapshot_.store(waiters_.size(), std::memory_order_release);
}

void bounded_client_pool::release(std::size_t slot_index,
    bool discard) noexcept
{
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
    entry.return_notification.callback =
        &bounded_client_pool::dispatch_return;
    entry.return_notification.callback_arg = &entry;
    context_.post_node_raw(&entry.return_notification);
}

void bounded_client_pool::dispatch_return(void* argument) noexcept
{
    auto& entry = *static_cast<slot*>(argument);
    auto& owner = *entry.owner;
    if (!owner.state_mutex_.try_lock())
    {
        owner.context_.post_node_raw(&entry.return_notification);
        return;
    }
    owner.release_locked(entry.index, entry.return_discard);
    owner.state_mutex_.unlock();
}

void bounded_client_pool::release_locked(std::size_t slot_index,
    bool discard) noexcept
{
    if (slot_index >= slots_.size() ||
        !slots_[slot_index].checked_out)
        return;
    auto& entry = slots_[slot_index];
    if (discard)
        entry.value->close();

    if (!closing_)
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
            *pending->result =
                pooled_client{this, slot_index, entry.value.get()};
            refresh_snapshots_locked();
            post_waiter(*pending);
            return;
        }

    entry.checked_out = false;
    --checked_out_;
    ++idle_;
    refresh_snapshots_locked();
}

void bounded_client_pool::remove_waiter(waiter* target) noexcept
{
    if (!target || !target->queued)
        return;
    auto found = std::ranges::find(waiters_, target);
    if (found != waiters_.end())
        waiters_.erase(found);
    target->queued = false;
}

void bounded_client_pool::post_waiter(waiter& pending) noexcept
{
    pending.cancellation_notification.callback = nullptr;
    pending.cancellation_notification.coroutine = pending.handle;
    context_.post_node_raw(&pending.cancellation_notification);
}

void bounded_client_pool::dispatch_cancellation(void* argument) noexcept
{
    auto* target = static_cast<waiter*>(argument);
    auto& owner = *target->owner;
    if (!owner.state_mutex_.try_lock())
    {
        owner.context_.post_node_raw(
            &target->cancellation_notification);
        return;
    }
    auto handle = target->handle;
    owner.remove_waiter(target);
    owner.refresh_snapshots_locked();
    owner.state_mutex_.unlock();
    handle.resume();
}

} // namespace cnetmod::http
