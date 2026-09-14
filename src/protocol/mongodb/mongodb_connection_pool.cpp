module cnetmod.protocol.mongodb;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.timer;
import cnetmod.coro.spawn;
import cnetmod.coro.cancel;
import cnetmod.coro.mutex;
import cnetmod.coro.wait_group;
import cnetmod.executor.async_op;
import :error;
import :connection;
import :connection_pool;

namespace cnetmod::mongodb {

class connection_pool_slot
{
public:
    std::unique_ptr<connection> client;
    std::shared_ptr<connection_pool_slot> close_next;
    post_node return_notification;
    std::shared_ptr<connection_pool_state> return_state;
    std::shared_ptr<connection_pool_slot> return_owner;
    bool return_discard = false;
    bool checked_out = false;
    bool stale = false;
    std::chrono::steady_clock::time_point last_used = std::chrono::steady_clock::now();
};

namespace {
    /**
     * An admitted creation owns this intrusive registration in its coroutine
     * frame until settlement. Cancellation only queues connection-owner work.
     */
    struct connection_attempt
    {
        cancel_token cancellation;
        connection_attempt* next = nullptr;
    };

    /**
     * Links one caller to its admitted attempt without allocating a callback.
     * The child token only queues owner-loop work; it never resumes connection
     * code under the parent callback lock. Unregistration joins notification.
     */
    class connection_attempt_link
    {
    public:
        connection_attempt_link(cancel_token& parent, cancel_token& child) noexcept
            : parent_(parent), child_(child)
        {
            if (!parent_.register_callback(this, [](void* value) noexcept
                    {
                        static_cast<connection_attempt_link*>(value)->child_.cancel();
                    }))
                child_.cancel();
        }

        ~connection_attempt_link()
        {
            parent_.finish_callback(this);
        }

        connection_attempt_link(const connection_attempt_link&) = delete;
        auto operator=(const connection_attempt_link&) -> connection_attempt_link& = delete;

    private:
        cancel_token& parent_;
        cancel_token& child_;
    };

    /**
     * Converts callback cancellation into an owner-loop stop request. Joining
     * the notification prevents queued work from outliving the checkout frame.
     */
    class checkout_cancellation
    {
    public:
        checkout_cancellation(io_context& io, cancel_token& token, std::stop_source& stop) noexcept
            : io_(io), token_(token), stop_(stop)
        {
            node_.callback_arg = this;
            node_.callback = [](void* value)
            {
                auto& self = *static_cast<checkout_cancellation*>(value);
                self.stop_.request_stop();
                self.dispatched_ = true;
                const auto waiter = self.waiter_;
                if (waiter)
                    waiter.resume();
            };
            auto notify = [](void* value) noexcept
            {
                auto& self = *static_cast<checkout_cancellation*>(value);
                self.io_.post_node_raw(&self.node_);
            };
            if (!token_.register_callback(this, notify))
                notify(this);
        }

        checkout_cancellation(const checkout_cancellation&) = delete;
        auto operator=(const checkout_cancellation&) -> checkout_cancellation& = delete;

        auto await_ready() noexcept -> bool
        {
            return token_.complete_callback(this) || dispatched_;
        }

        void await_suspend(std::coroutine_handle<> waiter) noexcept
        {
            waiter_ = waiter;
        }

        void await_resume() noexcept
        {
            token_.finish_callback(this);
        }

    private:
        io_context& io_;
        cancel_token& token_;
        std::stop_source& stop_;
        post_node node_;
        std::coroutine_handle<> waiter_;
        bool dispatched_ = false;
    };

    struct pool_waiter
    {
        std::shared_ptr<pool_waiter> close_next;
        bool closed_by_pool = false;
        std::coroutine_handle<> handle{};
        post_node notification;
        std::shared_ptr<connection_pool_slot> assigned;
        std::optional<error> failure;
        std::exception_ptr timeout_failure;
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
    // This protects only pool metadata. It is never held across connection or
    // database I/O, and waiters suspend instead of consuming an OS thread.
    async_mutex state_mutex;
    std::vector<std::shared_ptr<connection_pool_slot>> slots;
    std::deque<std::shared_ptr<pool_waiter>> waiters;
    std::size_t connecting = 0;
    connection_attempt* attempts = nullptr;
    std::atomic<std::size_t> connecting_snapshot{};
    async_wait_group pending_returns;
    post_node close_notification;
    std::shared_ptr<connection_pool_state> close_owner;
    bool closed = false;
    std::atomic_bool close_requested = false;
    std::atomic_bool close_dispatched = false;
    std::atomic<std::size_t> size_snapshot{};
    std::atomic<std::size_t> idle_snapshot{};
    std::atomic<std::size_t> checked_out_snapshot{};
    std::atomic<std::size_t> waiter_snapshot{};
};

namespace {
    void post_resume(const std::shared_ptr<connection_pool_state>& state,
        const std::shared_ptr<pool_waiter>& waiter,
        std::coroutine_handle<> handle)
    {
        if (handle)
        {
            /**
             * The suspended acquire operation retains the waiter until dispatch.
             * Completion is claimed under the state lock, so this node is queued
             * only once and notification requires no allocation after commit.
             */
            waiter->notification.coroutine = handle;
            state->context->post_node_raw(&waiter->notification);
        }
    }

    void remove_waiter_locked(connection_pool_state& state,
        const std::shared_ptr<pool_waiter>& waiter)
    {
        auto found = std::ranges::find(state.waiters, waiter);
        if (found != state.waiters.end())
            state.waiters.erase(found);
    }

    void refresh_snapshots_locked(connection_pool_state& state) noexcept
    {
        state.size_snapshot.store(state.slots.size(), std::memory_order_release);
        state.idle_snapshot.store(std::ranges::count_if(state.slots,
                                      [](const auto& slot)
                                      {
                                          return !slot->checked_out && !slot->stale;
                                      }),
            std::memory_order_release);
        state.checked_out_snapshot.store(std::ranges::count_if(state.slots,
                                             [](const auto& slot)
                                             {
                                                 return slot->checked_out;
                                             }),
            std::memory_order_release);
        state.waiter_snapshot.store(state.waiters.size(), std::memory_order_release);
    }

    auto wake_front_for_retry_locked(connection_pool_state& state) noexcept
        -> std::shared_ptr<pool_waiter>
    {
        while (!state.waiters.empty())
        {
            auto waiter = state.waiters.front();
            state.waiters.pop_front();
            if (waiter->completed)
                continue;
            waiter->retry = true;
            waiter->completed = true;
            return waiter;
        }
        return {};
    }

    auto timeout_waiter(std::shared_ptr<connection_pool_state> state,
        std::shared_ptr<pool_waiter> waiter,
        std::chrono::milliseconds timeout, std::stop_token cancellation) -> task<void>
    {
        std::optional<error> failure;
        std::exception_ptr exception;
        try
        {
            auto elapsed = co_await async_timer_wait(*state->context, timeout,
                waiter->timeout_token);
            if (cancellation.stop_requested())
                failure = make_error(error_code::operation_cancelled,
                    "MongoDB connection pool checkout was cancelled");
            else if (waiter->timeout_token.is_cancelled())
                co_return;
            else if (!elapsed)
                throw std::system_error(elapsed.error());
            else
                failure = make_error(error_code::pool_exhausted,
                    "MongoDB connection pool wait queue timed out");
        }
        catch (...)
        {
            exception = std::current_exception();
        }
        co_await state->state_mutex.lock();
        async_lock_guard lock{state->state_mutex, std::adopt_lock};
        if (waiter->completed)
            co_return;
        try
        {
            waiter->failure = std::move(failure);
        }
        catch (...)
        {
            exception = std::current_exception();
        }
        waiter->timeout_failure = exception;
        remove_waiter_locked(*state, waiter);
        waiter->completed = true;
        refresh_snapshots_locked(*state);
        lock.release();
        state->state_mutex.unlock();
        post_resume(state, waiter, waiter->handle);
    }

    /**
     * Joins an already-dispatched task on its owning I/O thread. Unlike the
     * ordinary task awaiter, this only installs the final-suspend continuation;
     * it never resumes an operation that is still waiting for I/O.
     */
    struct timeout_completion
    {
        task<void>& operation;

        auto await_ready() const noexcept -> bool
        {
            return operation.handle().done();
        }

        void await_suspend(std::coroutine_handle<> caller) noexcept
        {
            operation.handle().promise().set_caller(caller);
        }

        void await_resume()
        {
            operation.handle().promise().result();
        }
    };

    struct waiter_awaitable
    {
        std::shared_ptr<connection_pool_state> state;
        std::shared_ptr<pool_waiter> waiter;
        async_lock_guard& lock;

        auto await_ready() const noexcept -> bool
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept
        {
            waiter->handle = handle;
            lock.release();
            state->state_mutex.unlock();
        }

        void await_resume() const noexcept {}
    };

    struct slot_return_outcome
    {
        /**
         * One returned slot can release at most one queued borrower. Retain that
         * waiter directly so the noexcept return path needs no dynamic storage.
         */
        std::shared_ptr<pool_waiter> waiter;
        bool close_connection = false;
    };

    auto return_slot_locked(connection_pool_state& state,
        const std::shared_ptr<connection_pool_slot>& slot, bool discard) noexcept
        -> slot_return_outcome
    {
        slot_return_outcome outcome;
        slot->checked_out = false;
        slot->last_used = std::chrono::steady_clock::now();
        slot->stale = slot->stale || discard || !slot->client->is_open() || state.closed;
        if (slot->stale)
        {
            outcome.close_connection = true;
            std::erase(state.slots, slot);
            if (!state.closed)
                outcome.waiter = wake_front_for_retry_locked(state);
        }
        else
        {
            while (!state.waiters.empty())
            {
                auto waiter = state.waiters.front();
                state.waiters.pop_front();
                if (waiter->completed)
                    continue;
                slot->checked_out = true;
                waiter->assigned = slot;
                waiter->completed = true;
                outcome.waiter = std::move(waiter);
                break;
            }
        }
        refresh_snapshots_locked(state);
        return outcome;
    }

    void complete_slot_return(const std::shared_ptr<connection_pool_state>& state,
        const std::shared_ptr<connection_pool_slot>& slot, slot_return_outcome outcome)
    {
        if (outcome.close_connection)
            slot->client->close();
        if (outcome.waiter)
        {
            outcome.waiter->timeout_token.cancel();
            post_resume(state, outcome.waiter, outcome.waiter->handle);
        }
    }

    void dispatch_slot_return(void* raw) noexcept
    {
        auto* queued = static_cast<connection_pool_slot*>(raw);
        if (!queued->return_state->state_mutex.try_lock())
        {
            queued->return_state->context->post_node_raw(&queued->return_notification);
            return;
        }
        auto state = std::move(queued->return_state);
        auto slot = std::move(queued->return_owner);
        auto outcome = return_slot_locked(*state, slot, queued->return_discard);
        state->state_mutex.unlock();
        complete_slot_return(state, slot, std::move(outcome));
        state->pending_returns.done();
    }

    void return_slot(const std::shared_ptr<connection_pool_state>& state,
        const std::shared_ptr<connection_pool_slot>& slot, bool discard) noexcept
    {
        if (state->state_mutex.try_lock())
        {
            auto outcome = return_slot_locked(*state, slot, discard);
            state->state_mutex.unlock();
            complete_slot_return(state, slot, std::move(outcome));
            return;
        }
        /**
         * A lease returns its slot only once. Retain ownership until dispatch,
         * using slot-local storage instead of allocating a detached coroutine.
         * Async close joins these registered returns before reporting success.
         */
        slot->return_state = state;
        slot->return_owner = slot;
        slot->return_discard = discard;
        slot->return_notification.callback = &dispatch_slot_return;
        slot->return_notification.callback_arg = slot.get();
        state->pending_returns.add();
        state->context->post_node_raw(&slot->return_notification);
    }

    struct close_outcome
    {
        std::shared_ptr<connection_pool_slot> slots;
        std::shared_ptr<pool_waiter> waiters;
    };

    auto close_locked(connection_pool_state& state) -> close_outcome
    {
        close_outcome outcome;
        if (state.closed)
            return outcome;
        auto* slot_tail = &outcome.slots;
        for (auto& slot : state.slots)
        {
            *slot_tail = slot;
            slot_tail = &slot->close_next;
        }
        auto* waiter_tail = &outcome.waiters;
        for (auto& waiter : state.waiters)
            if (!waiter->completed)
            {
                waiter->closed_by_pool = true;
                waiter->completed = true;
                *waiter_tail = waiter;
                waiter_tail = &waiter->close_next;
            }
        /**
         * Existing nodes retain shutdown ownership without staging allocations.
         * Borrowers construct diagnostics only after leaving the close path.
         */
        state.closed = true;
        for (auto* attempt = state.attempts; attempt; attempt = attempt->next)
            attempt->cancellation.cancel();
        for (auto& slot : state.slots)
            slot->stale = true;
        /**
         * The close outcome owns idle transports until destruction outside the
         * lock. Only borrowed slots remain registered for deferred return.
         */
        std::erase_if(state.slots, [](const auto& slot)
            {
                return !slot->checked_out;
            });
        state.waiters.clear();
        refresh_snapshots_locked(state);
        return outcome;
    }

    void complete_close(const std::shared_ptr<connection_pool_state>& state,
        close_outcome outcome)
    {
        while (outcome.slots)
        {
            auto slot = std::move(outcome.slots);
            outcome.slots = std::move(slot->close_next);
            /**
             * A borrower owns the transport until its operation has unwound.
             * Signal cancellation without destroying pending I/O resources;
             * the stale slot is closed when its lease is returned.
             */
            if (slot->checked_out)
                slot->client->cancel_active_command();
            else
                slot->client->close();
        }
        while (outcome.waiters)
        {
            auto waiter = std::move(outcome.waiters);
            outcome.waiters = std::move(waiter->close_next);
            waiter->timeout_token.cancel();
            post_resume(state, waiter, waiter->handle);
        }
    }

    /**
     * Retries metadata admission using a preallocated node. The state retains
     * itself until dispatch, and async_close joins this notification as well.
     */
    void dispatch_pool_close(void* raw) noexcept
    {
        auto* queued = static_cast<connection_pool_state*>(raw);
        if (!queued->state_mutex.try_lock())
        {
            queued->context->post_node_raw(&queued->close_notification);
            return;
        }
        auto state = std::move(queued->close_owner);
        auto outcome = close_locked(*state);
        state->state_mutex.unlock();
        complete_close(state, std::move(outcome));
        state->pending_returns.done();
    }

    // This cannot be a connection_pool member coroutine. close() is also
    // called from ~connection_pool(), and deferred work must retain the
    // shared pool state rather than a potentially dangling this-pointer.
    auto close_pool_async(std::shared_ptr<connection_pool_state> state) -> task<void>
    {
        co_await state->state_mutex.lock();
        async_lock_guard lock{state->state_mutex, std::adopt_lock};
        auto outcome = close_locked(*state);
        lock.release();
        state->state_mutex.unlock();
        complete_close(state, std::move(outcome));
        co_await state->pending_returns.wait();
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

auto connection_pool::create_connection(cancel_token* cancellation, std::stop_token stop)
    -> task<result<std::shared_ptr<connection_pool_slot>>>
{
    connection_attempt attempt;
    co_await state_->state_mutex.lock();
    async_lock_guard admission_lock{state_->state_mutex, std::adopt_lock};
    if (state_->closed || state_->close_requested.load(std::memory_order_acquire))
        co_return std::unexpected(make_error(error_code::connection_closed,
            "MongoDB connection pool is closed"));
    if (state_->slots.size() + state_->connecting >= state_->options.maximum_size ||
        state_->connecting >= state_->options.maximum_connecting)
        co_return std::unexpected(make_error(error_code::pool_exhausted,
            "MongoDB connection pool creation limit reached"));
    ++state_->connecting;
    attempt.next = state_->attempts;
    state_->attempts = &attempt;
    state_->connecting_snapshot.store(state_->connecting, std::memory_order_release);
    refresh_snapshots_locked(*state_);
    admission_lock.release();
    state_->state_mutex.unlock();
    std::shared_ptr<connection_pool_slot> candidate;
    result<void> connected;
    std::exception_ptr failure;
    try
    {
        std::optional<connection_attempt_link> link;
        if (cancellation)
            link.emplace(*cancellation, attempt.cancellation);
        std::stop_callback stop_attempt{stop, [&]() noexcept
            {
                attempt.cancellation.cancel();
            }};
        candidate = std::make_shared<connection_pool_slot>();
        candidate->client = std::make_unique<connection>(*state_->context);
        connected = co_await candidate->client->connect(state_->options.connection, attempt.cancellation);
    }
    catch (...)
    {
        failure = std::current_exception();
    }
    std::shared_ptr<pool_waiter> resumed;
    bool closed_after_connect = false;
    co_await state_->state_mutex.lock();
    async_lock_guard completion_lock{state_->state_mutex, std::adopt_lock};
    auto** current = &state_->attempts;
    while (*current && *current != &attempt)
        current = &(*current)->next;
    if (*current)
        *current = attempt.next;
    --state_->connecting;
    state_->connecting_snapshot.store(state_->connecting, std::memory_order_release);
    closed_after_connect = state_->closed ||
        state_->close_requested.load(std::memory_order_acquire);
    /**
     * Every admitted attempt releases its creation budget before propagating
     * failure. Publishing a slot must also succeed before claiming checkout.
     */
    if (!failure && connected && !closed_after_connect)
    {
        try
        {
            state_->slots.push_back(candidate);
            candidate->checked_out = true;
        }
        catch (...)
        {
            failure = std::current_exception();
        }
    }
    if (failure || !connected || closed_after_connect)
        resumed = wake_front_for_retry_locked(*state_);
    refresh_snapshots_locked(*state_);
    completion_lock.release();
    state_->state_mutex.unlock();
    if (resumed)
    {
        resumed->timeout_token.cancel();
        post_resume(state_, resumed, resumed->handle);
    }
    if (failure)
        std::rethrow_exception(failure);
    if (closed_after_connect)
    {
        candidate->client->close();
        co_return std::unexpected(make_error(
            error_code::connection_closed, "MongoDB connection pool closed while connecting"));
    }
    if (!connected)
        co_return std::unexpected(connected.error());
    co_return candidate;
}

auto connection_pool::warm_up() -> task<result<void>>
{
    return warm_connections<false>(nullptr);
}

auto connection_pool::warm_up(cancel_token& cancellation) -> task<result<void>>
{
    return warm_connections<true>(&cancellation);
}

template <bool Cancellable>
auto connection_pool::warm_connections(cancel_token* cancellation) -> task<result<void>>
{
    while (true)
    {
        if constexpr (Cancellable)
        {
            if (cancellation->is_cancelled())
                co_return std::unexpected(make_error(error_code::operation_cancelled,
                    "MongoDB pool warmup cancelled"));
        }
        co_await state_->state_mutex.lock();
        async_lock_guard lock{state_->state_mutex, std::adopt_lock};
        if (state_->closed || state_->close_requested.load(std::memory_order_acquire))
            co_return std::unexpected(make_error(error_code::connection_closed,
                "MongoDB connection pool is closed"));
        std::erase_if(state_->slots, [](const auto& value)
            {
                return !value->checked_out && (value->stale || !value->client->is_open());
            });
        const auto usable = std::ranges::count_if(state_->slots, [](const auto& slot)
            {
                return !slot->stale && slot->client->is_open();
            });
        const bool ready = static_cast<std::size_t>(usable) >= state_->options.minimum_size;
        const bool may_create = state_->slots.size() + state_->connecting < state_->options.maximum_size &&
            state_->connecting < state_->options.maximum_connecting;
        const bool creation_pending = state_->connecting != 0;
        refresh_snapshots_locked(*state_);
        lock.release();
        state_->state_mutex.unlock();
        if (ready)
            break;
        if (!may_create)
        {
            /**
             * Reserved creation capacity is not a usable connection. Wait for
             * the owning attempt to settle before re-evaluating readiness.
             */
            if (!creation_pending)
                co_return std::unexpected(make_error(error_code::pool_exhausted,
                    "MongoDB pool has no capacity for warmup"));
            std::expected<void, std::error_code> waited;
            if constexpr (Cancellable)
                waited = co_await async_timer_wait(*state_->context, std::chrono::milliseconds{1}, *cancellation);
            else
                waited = co_await async_timer_wait(*state_->context, std::chrono::milliseconds{1});
            if (!waited)
            {
                if constexpr (Cancellable)
                {
                    if (cancellation->is_cancelled())
                        co_return std::unexpected(make_error(error_code::operation_cancelled,
                            "MongoDB pool warmup cancelled"));
                }
                throw std::system_error(waited.error());
            }
            continue;
        }
        auto created = co_await create_connection(cancellation);
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
        co_await state_->state_mutex.lock();
        async_lock_guard lock{state_->state_mutex, std::adopt_lock};
        if (state_->closed || state_->close_requested.load(std::memory_order_acquire))
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
                    refresh_snapshots_locked(*state_);
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
        refresh_snapshots_locked(*state_);
        if (may_create)
        {
            lock.release();
            state_->state_mutex.unlock();
            auto created = co_await create_connection(nullptr, cancellation);
            if (created)
                co_return pooled_connection(state_, *created);
            if (created.error().code != error_code::pool_exhausted)
                co_return std::unexpected(created.error());
            continue;
        }
        task<void> timeout_operation;
        try
        {
            timeout_operation = timeout_waiter(state_, waiter, state_->options.wait_queue_timeout, cancellation);
        }
        catch (...)
        {
            /**
             * The borrower has not suspended yet. Roll back queue admission if
             * task construction fails, before the metadata guard releases it.
             */
            remove_waiter_locked(*state_, waiter);
            refresh_snapshots_locked(*state_);
            throw;
        }
        /**
         * Foreign threads only signal the timer. The owned timeout operation
         * resolves cancellation and queue metadata on the owning I/O thread.
         */
        std::stop_callback cancel_callback(cancellation, [waiter]() noexcept
            {
                waiter->timeout_token.cancel();
            });
        post_node timeout_start;
        timeout_start.coroutine = timeout_operation.handle();
        state_->context->post_node_raw(&timeout_start);
        co_await waiter_awaitable{state_, waiter, lock};
        waiter->timeout_token.cancel();
        co_await timeout_completion{timeout_operation};
        if (waiter->timeout_failure)
            std::rethrow_exception(waiter->timeout_failure);
        if (waiter->closed_by_pool)
            co_return std::unexpected(make_error(error_code::connection_closed,
                "MongoDB connection pool was closed"));
        if (waiter->failure)
            co_return std::unexpected(*waiter->failure);
        if (waiter->assigned)
            co_return pooled_connection(state_, waiter->assigned);
        if (!waiter->retry)
            co_return std::unexpected(make_error(error_code::protocol_error,
                "MongoDB connection pool waiter resumed without an outcome"));
    }
}

auto connection_pool::checkout_for_health(cancel_token& cancellation) -> task<result<pooled_connection>>
{
    std::stop_source stop;
    checkout_cancellation notification{*state_->context, cancellation, stop};
    result<pooled_connection> outcome;
    std::exception_ptr failure;
    try
    {
        outcome = co_await acquire(stop.get_token());
    }
    catch (...)
    {
        failure = std::current_exception();
    }
    co_await notification;
    if (failure)
        std::rethrow_exception(failure);
    if (cancellation.is_cancelled())
        co_return std::unexpected(make_error(error_code::operation_cancelled,
            "MongoDB health checkout was cancelled"));
    co_return std::move(outcome);
}

auto connection_pool::health_check() -> task<void>
{
    return check_connections<false>(nullptr);
}

auto connection_pool::health_check(cancel_token& cancellation) -> task<result<void>>
{
    return check_connections<true>(&cancellation);
}

template <bool Cancellable>
auto connection_pool::check_connections(cancel_token* cancellation)
    -> task<std::conditional_t<Cancellable, result<void>, void>>
{
    if constexpr (Cancellable)
        if (cancellation->is_cancelled())
            co_return std::unexpected(make_error(error_code::operation_cancelled,
                "MongoDB health check was cancelled"));
    std::vector<pooled_connection> candidates;
    co_await state_->state_mutex.lock();
    async_lock_guard lock{state_->state_mutex, std::adopt_lock};
    /**
     * Reserve storage before claiming slots. Each claimed slot then has a lease
     * owner that returns it even when probe construction or execution throws.
     */
    candidates.reserve(state_->slots.size());
    for (auto& slot : state_->slots)
        if (!slot->checked_out && !slot->stale)
        {
            slot->checked_out = true;
            candidates.push_back(pooled_connection{state_, slot});
        }
    refresh_snapshots_locked(*state_);
    lock.release();
    state_->state_mutex.unlock();
    if constexpr (Cancellable)
        if (candidates.empty())
        {
            auto acquired = co_await checkout_for_health(*cancellation);
            if (!acquired)
                co_return std::unexpected(std::move(acquired.error()));
            candidates.push_back(std::move(*acquired));
        }
    for (auto& lease : candidates)
    {
        if constexpr (Cancellable)
            if (cancellation->is_cancelled())
                co_return std::unexpected(make_error(error_code::operation_cancelled,
                    "MongoDB health check was cancelled"));
        try
        {
            auto healthy = co_await (cancellation ? lease->ping(*cancellation) : lease->ping());
            if (!healthy)
            {
                lease.discard();
                if constexpr (Cancellable)
                    co_return std::unexpected(std::move(healthy.error()));
            }
        }
        catch (...)
        {
            lease.discard();
            throw;
        }
        lease = pooled_connection{};
    }
    if constexpr (Cancellable)
        co_return result<void>{};
    else
        co_return;
}

auto connection_pool::run_maintenance(std::stop_token stop) -> task<void>
{
    cancel_token timer_cancel;
    std::stop_callback wake_on_stop{stop, [&timer_cancel]() noexcept
        {
            timer_cancel.cancel();
        }};
    while (!stop.stop_requested())
    {
        auto waited = co_await async_timer_wait(*state_->context,
            state_->options.health_check_interval, timer_cancel);
        if (stop.stop_requested())
            break;
        if (!waited)
            throw std::system_error(waited.error());
        co_await health_check();
        if (stop.stop_requested())
            break;
        auto ignored = co_await warm_up();
        (void)ignored;
    }
}

void connection_pool::close() noexcept
{
    state_->close_requested.store(true, std::memory_order_release);
    if (state_->close_dispatched.exchange(true, std::memory_order_acq_rel))
        return;
    if (state_->state_mutex.try_lock())
    {
        auto outcome = close_locked(*state_);
        state_->state_mutex.unlock();
        complete_close(state_, std::move(outcome));
        return;
    }
    state_->close_owner = state_;
    state_->close_notification.callback_arg = state_.get();
    state_->close_notification.callback = dispatch_pool_close;
    state_->pending_returns.add();
    state_->context->post_node_raw(&state_->close_notification);
}

auto connection_pool::size() const noexcept -> std::size_t
{
    return state_->size_snapshot.load(std::memory_order_acquire);
}

auto connection_pool::async_close() -> task<void>
{
    auto operation = close_pool_async(state_);
    state_->close_requested.store(true, std::memory_order_release);
    return operation;
}

auto connection_pool::idle_count() const noexcept -> std::size_t
{
    return state_->idle_snapshot.load(std::memory_order_acquire);
}

auto connection_pool::checked_out_count() const noexcept -> std::size_t
{
    return state_->checked_out_snapshot.load(std::memory_order_acquire);
}

auto connection_pool::connecting_count() const noexcept -> std::size_t
{
    return state_->connecting_snapshot.load(std::memory_order_acquire);
}

auto connection_pool::waiter_count() const noexcept -> std::size_t
{
    return state_->waiter_snapshot.load(std::memory_order_acquire);
}

auto connection_pool::context() noexcept -> io_context&
{
    return *state_->context;
}

} // namespace cnetmod::mongodb
