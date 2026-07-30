module cnetmod.protocol.mongodb;

import std;
import cnetmod.coro.timer;
import cnetmod.coro.spawn;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;
import :error;
import :connection;
import :connection_pool;

namespace cnetmod::mongodb {

class connection_pool_slot
{
public:
    std::unique_ptr<connection> client;
    bool checked_out = false;
    bool stale = false;
    std::chrono::steady_clock::time_point last_used = std::chrono::steady_clock::now();
};

namespace {
    struct pool_waiter
    {
        std::coroutine_handle<> handle{};
        std::shared_ptr<connection_pool_slot> assigned;
        std::optional<error> failure;
        cancel_token timeout_token;
        bool completed = false;
        bool retry = false;
    };
} // namespace

class connection_pool_state
{
public:
    io_context* context = nullptr;
    connection_pool_options options;
    mutable std::mutex mutex;
    std::vector<std::shared_ptr<connection_pool_slot>> slots;
    std::deque<std::shared_ptr<pool_waiter>> waiters;
    std::size_t connecting = 0;
    bool closed = false;
};

namespace {
    void post_resume(const std::shared_ptr<connection_pool_state>& state,
        std::coroutine_handle<> handle)
    {
        if (handle)
            state->context->post(handle);
    }

    void remove_waiter_locked(connection_pool_state& state,
        const std::shared_ptr<pool_waiter>& waiter)
    {
        auto found = std::ranges::find(state.waiters, waiter);
        if (found != state.waiters.end())
            state.waiters.erase(found);
    }

    void finish_waiter(const std::shared_ptr<connection_pool_state>& state,
        const std::shared_ptr<pool_waiter>& waiter, std::optional<error> failure,
        bool retry = false,
        std::shared_ptr<connection_pool_slot> assigned = {})
    {
        std::coroutine_handle<> handle;
        {
            std::scoped_lock lock(state->mutex);
            if (waiter->completed)
                return;
            remove_waiter_locked(*state, waiter);
            waiter->failure = std::move(failure);
            waiter->retry = retry;
            waiter->assigned = std::move(assigned);
            waiter->completed = true;
            handle = waiter->handle;
        }
        waiter->timeout_token.cancel();
        post_resume(state, handle);
    }

    void wake_front_for_retry_locked(connection_pool_state& state,
        std::vector<std::pair<std::shared_ptr<pool_waiter>, std::coroutine_handle<>>>& resumed)
    {
        while (!state.waiters.empty())
        {
            auto waiter = state.waiters.front();
            state.waiters.pop_front();
            if (waiter->completed)
                continue;
            waiter->retry = true;
            waiter->completed = true;
            resumed.emplace_back(waiter, waiter->handle);
            break;
        }
    }

    auto timeout_waiter(std::weak_ptr<connection_pool_state> weak_state,
        std::shared_ptr<pool_waiter> waiter,
        std::chrono::milliseconds timeout) -> task<void>
    {
        auto state = weak_state.lock();
        if (!state)
            co_return;
        auto elapsed = co_await async_timer_wait(*state->context, timeout,
            waiter->timeout_token);
        if (elapsed)
            finish_waiter(state, waiter, make_error(error_code::pool_exhausted, "MongoDB connection pool wait queue timed out"));
    }

    struct waiter_awaitable
    {
        std::shared_ptr<connection_pool_state> state;
        std::shared_ptr<pool_waiter> waiter;

        auto await_ready() const noexcept -> bool
        {
            std::scoped_lock lock(state->mutex);
            return waiter->completed;
        }

        auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool
        {
            std::scoped_lock lock(state->mutex);
            if (waiter->completed)
                return false;
            waiter->handle = handle;
            return true;
        }

        void await_resume() const noexcept {}
    };

    void return_slot(const std::shared_ptr<connection_pool_state>& state,
        const std::shared_ptr<connection_pool_slot>& slot, bool discard) noexcept
    {
        std::shared_ptr<pool_waiter> assigned_waiter;
        std::coroutine_handle<> assigned_handle;
        std::vector<std::pair<std::shared_ptr<pool_waiter>, std::coroutine_handle<>>> retry_waiters;
        {
            std::scoped_lock lock(state->mutex);
            slot->checked_out = false;
            slot->last_used = std::chrono::steady_clock::now();
            slot->stale = slot->stale || discard || !slot->client->is_open() || state->closed;
            if (slot->stale)
            {
                slot->client->close();
                std::erase(state->slots, slot);
                if (!state->closed)
                    wake_front_for_retry_locked(*state, retry_waiters);
            }
            else
            {
                while (!state->waiters.empty())
                {
                    auto waiter = state->waiters.front();
                    state->waiters.pop_front();
                    if (waiter->completed)
                        continue;
                    slot->checked_out = true;
                    waiter->assigned = slot;
                    waiter->completed = true;
                    assigned_waiter = waiter;
                    assigned_handle = waiter->handle;
                    break;
                }
            }
        }
        if (assigned_waiter)
            assigned_waiter->timeout_token.cancel();
        post_resume(state, assigned_handle);
        for (auto& [waiter, handle] : retry_waiters)
        {
            waiter->timeout_token.cancel();
            post_resume(state, handle);
        }
    }
} // namespace

pooled_connection::pooled_connection(std::shared_ptr<connection_pool_state> state,
    std::shared_ptr<connection_pool_slot> slot) noexcept
    : state_(std::move(state)), slot_(std::move(slot)) {}

pooled_connection::pooled_connection(pooled_connection&& other) noexcept = default;

auto pooled_connection::operator=(pooled_connection&& other) noexcept
    -> pooled_connection&
{
    if (this != &other)
    {
        release();
        state_ = std::move(other.state_);
        slot_ = std::move(other.slot_);
    }
    return *this;
}

pooled_connection::~pooled_connection()
{
    release();
}

auto pooled_connection::valid() const noexcept -> bool
{
    return slot_ && slot_->client && slot_->client->is_open() && !slot_->stale;
}

auto pooled_connection::get() noexcept -> connection&
{
    return *slot_->client;
}

auto pooled_connection::operator->() noexcept -> connection*
{
    return slot_->client.get();
}

void pooled_connection::discard() noexcept
{
    if (state_ && slot_)
        return_slot(state_, slot_, true);
    slot_.reset();
    state_.reset();
}

void pooled_connection::release() noexcept
{
    if (state_ && slot_)
        return_slot(state_, slot_, false);
    slot_.reset();
    state_.reset();
}

connection_pool::connection_pool(io_context& context, connection_pool_options options)
    : state_(std::make_shared<connection_pool_state>())
{
    options.maximum_size = std::max<std::size_t>(1, options.maximum_size);
    options.minimum_size = std::min(options.minimum_size, options.maximum_size);
    options.maximum_connecting = std::max<std::size_t>(1,
        std::min(options.maximum_connecting, options.maximum_size));
    state_->context = &context;
    state_->options = std::move(options);
}

connection_pool::~connection_pool()
{
    close();
}

auto connection_pool::create_connection()
    -> task<result<std::shared_ptr<connection_pool_slot>>>
{
    {
        std::scoped_lock lock(state_->mutex);
        if (state_->closed)
            co_return std::unexpected(make_error(error_code::connection_closed,
                "MongoDB connection pool is closed"));
        if (state_->slots.size() + state_->connecting >= state_->options.maximum_size ||
            state_->connecting >= state_->options.maximum_connecting)
            co_return std::unexpected(make_error(error_code::pool_exhausted,
                "MongoDB connection pool creation limit reached"));
        ++state_->connecting;
    }
    auto candidate = std::make_shared<connection_pool_slot>();
    candidate->client = std::make_unique<connection>(*state_->context);
    auto connected = co_await candidate->client->connect(state_->options.connection);
    std::vector<std::pair<std::shared_ptr<pool_waiter>, std::coroutine_handle<>>> resumed;
    bool closed_after_connect = false;
    {
        std::scoped_lock lock(state_->mutex);
        --state_->connecting;
        closed_after_connect = state_->closed;
        if (!connected || closed_after_connect)
            wake_front_for_retry_locked(*state_, resumed);
        else
        {
            candidate->checked_out = true;
            state_->slots.push_back(candidate);
        }
    }
    for (auto& [waiter, handle] : resumed)
    {
        waiter->timeout_token.cancel();
        post_resume(state_, handle);
    }
    if (!connected)
        co_return std::unexpected(connected.error());
    if (closed_after_connect)
    {
        candidate->client->close();
        co_return std::unexpected(make_error(
            error_code::connection_closed, "MongoDB connection pool closed while connecting"));
    }
    co_return candidate;
}

auto connection_pool::warm_up() -> task<result<void>>
{
    while (true)
    {
        {
            std::scoped_lock lock(state_->mutex);
            std::erase_if(state_->slots, [](const auto& value)
                {
                    return !value->checked_out && (value->stale || !value->client->is_open());
                });
            if (state_->slots.size() + state_->connecting >= state_->options.minimum_size)
                break;
        }
        auto created = co_await create_connection();
        if (!created)
            co_return std::unexpected(created.error());
        return_slot(state_, *created, false);
    }
    co_return result<void>{};
}

auto connection_pool::acquire() -> task<result<pooled_connection>>
{
    co_return co_await acquire(std::stop_token{});
}

auto connection_pool::acquire(std::stop_token cancellation)
    -> task<result<pooled_connection>>
{
    while (true)
    {
        if (cancellation.stop_requested())
            co_return std::unexpected(make_error(
                error_code::operation_cancelled, "MongoDB connection pool checkout was cancelled"));
        bool may_create = false;
        std::shared_ptr<pool_waiter> waiter;
        {
            std::scoped_lock lock(state_->mutex);
            if (state_->closed)
                co_return std::unexpected(make_error(error_code::connection_closed,
                    "MongoDB connection pool is closed"));
            const auto now = std::chrono::steady_clock::now();
            std::erase_if(state_->slots, [&](const auto& value)
                {
                    const bool expired = state_->options.maximum_idle_time.count() > 0 &&
                        now - value->last_used > state_->options.maximum_idle_time;
                    if (!value->checked_out && (value->stale || expired || !value->client->is_open()))
                    {
                        value->client->close();
                        return true;
                    }
                    return false;
                });
            if (!state_->waiters.empty())
            {
                waiter = std::make_shared<pool_waiter>();
                state_->waiters.push_back(waiter);
            }
            else
            {
                for (auto& slot : state_->slots)
                    if (!slot->checked_out && !slot->stale)
                    {
                        slot->checked_out = true;
                        co_return pooled_connection(state_, slot);
                    }
                may_create = state_->slots.size() + state_->connecting < state_->options.maximum_size &&
                    state_->connecting < state_->options.maximum_connecting;
                if (!may_create)
                {
                    if (state_->options.wait_queue_timeout <= std::chrono::milliseconds::zero())
                        co_return std::unexpected(make_error(error_code::pool_exhausted,
                            "MongoDB connection pool wait queue timed out"));
                    waiter = std::make_shared<pool_waiter>();
                    state_->waiters.push_back(waiter);
                }
            }
        }
        if (may_create)
        {
            auto created = co_await create_connection();
            if (created)
                co_return pooled_connection(state_, *created);
            if (created.error().code != error_code::pool_exhausted)
                co_return std::unexpected(created.error());
            continue;
        }
        spawn(*state_->context, timeout_waiter(state_, waiter, state_->options.wait_queue_timeout));
        std::stop_callback cancel_callback(cancellation, [state = state_, waiter]
            {
                finish_waiter(state, waiter, make_error(error_code::operation_cancelled, "MongoDB connection pool checkout was cancelled"));
            });
        co_await waiter_awaitable{state_, waiter};
        waiter->timeout_token.cancel();
        if (waiter->failure)
            co_return std::unexpected(*waiter->failure);
        if (waiter->assigned)
            co_return pooled_connection(state_, waiter->assigned);
        if (!waiter->retry)
            co_return std::unexpected(make_error(error_code::protocol_error,
                "MongoDB connection pool waiter resumed without an outcome"));
    }
}

auto connection_pool::health_check() -> task<void>
{
    std::vector<std::shared_ptr<connection_pool_slot>> candidates;
    {
        std::scoped_lock lock(state_->mutex);
        for (auto& slot : state_->slots)
            if (!slot->checked_out && !slot->stale)
            {
                slot->checked_out = true;
                candidates.push_back(slot);
            }
    }
    for (auto& slot : candidates)
    {
        auto healthy = co_await slot->client->ping();
        return_slot(state_, slot, !healthy);
    }
}

auto connection_pool::run_maintenance(std::stop_token stop) -> task<void>
{
    while (!stop.stop_requested())
    {
        co_await async_sleep(*state_->context, state_->options.health_check_interval);
        if (stop.stop_requested())
            break;
        co_await health_check();
        auto ignored = co_await warm_up();
        (void)ignored;
    }
}

void connection_pool::close() noexcept
{
    std::vector<std::pair<std::shared_ptr<pool_waiter>, std::coroutine_handle<>>> waiters;
    {
        std::scoped_lock lock(state_->mutex);
        if (state_->closed)
            return;
        state_->closed = true;
        for (auto& slot : state_->slots)
        {
            slot->stale = true;
            slot->client->close();
        }
        for (auto& waiter : state_->waiters)
            if (!waiter->completed)
            {
                waiter->failure = make_error(error_code::connection_closed,
                    "MongoDB connection pool was closed");
                waiter->completed = true;
                waiters.emplace_back(waiter, waiter->handle);
            }
        state_->waiters.clear();
    }
    for (auto& [waiter, handle] : waiters)
    {
        waiter->timeout_token.cancel();
        post_resume(state_, handle);
    }
}

auto connection_pool::size() const noexcept -> std::size_t
{
    std::scoped_lock lock(state_->mutex);
    return state_->slots.size();
}

auto connection_pool::idle_count() const noexcept -> std::size_t
{
    std::scoped_lock lock(state_->mutex);
    return std::ranges::count_if(state_->slots,
        [](const auto& slot)
        {
            return !slot->checked_out && !slot->stale;
        });
}

auto connection_pool::checked_out_count() const noexcept -> std::size_t
{
    std::scoped_lock lock(state_->mutex);
    return std::ranges::count_if(state_->slots,
        [](const auto& slot)
        {
            return slot->checked_out;
        });
}

auto connection_pool::waiter_count() const noexcept -> std::size_t
{
    std::scoped_lock lock(state_->mutex);
    return state_->waiters.size();
}

auto connection_pool::context() noexcept -> io_context&
{
    return *state_->context;
}

} // namespace cnetmod::mongodb
