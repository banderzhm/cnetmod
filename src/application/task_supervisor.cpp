module cnetmod.application.task_supervisor;

import std;
import cnetmod.application.recovery_policy;
import cnetmod.core.log;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.coro.wait_group;
import cnetmod.executor.async_op;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod::application {

namespace {

    /**
     * @brief Hashes owned names and borrowed queries identically without allocation.
     */
    struct task_name_hash
    {
        using is_transparent = void;

        auto operator()(std::string_view name) const noexcept -> std::size_t
        {
            return std::hash<std::string_view>{}(name);
        }
    };

    auto retry_delay(recovery_policy policy, std::size_t attempt)
        -> std::chrono::milliseconds
    {
        const auto exponent = std::pow(policy.multiplier,
            static_cast<double>(attempt));
        const auto raw = static_cast<double>(policy.initial_delay.count()) *
            exponent;
        const auto bounded = std::min(raw,
            static_cast<double>(policy.maximum_delay.count()));
        const auto phase = static_cast<double>((attempt * 1103515245U +
                                                   12345U) %
                               2001U) /
                1000.0 -
            1.0;
        const auto adjusted = std::max(0.0, bounded * (1.0 + phase * policy.jitter));
        if (adjusted >= static_cast<double>(policy.maximum_delay.count()))
            return policy.maximum_delay;
        return std::chrono::milliseconds{static_cast<std::int64_t>(adjusted)};
    }

} // namespace

class task_supervisor::implementation
    : public std::enable_shared_from_this<implementation>
{
public:
    struct completion_ticket
    {
        std::shared_ptr<implementation> owner;
        std::atomic<bool> registered{false};
        std::atomic<bool> settled{false};

        /**
         * @brief Releases the registered completion count at most once.
         */
        void settle() noexcept
        {
            if (registered.load(std::memory_order_acquire) &&
                !settled.exchange(true, std::memory_order_acq_rel))
                owner->completed.done();
        }

        ~completion_ticket()
        {
            settle();
        }
    };

    struct completion_guard
    {
        std::shared_ptr<completion_ticket> ticket;

        ~completion_guard()
        {
            ticket->settle();
        }
    };

    struct entry
    {
        std::string name;
        supervised_task operation;
        std::function<void()> stop_request;
        recovery_policy recovery;
        deadline recovery_deadline;
        std::shared_ptr<cancel_token> cancellation =
            std::make_shared<cancel_token>();
        supervised_task_state state = supervised_task_state::starting;
        std::error_code error;
        std::error_code stop_error;
        bool required = true;
    };

    explicit implementation(io_context& context)
        : context(context)
    {
        cancellation_post.callback = [](void* state) noexcept
        {
            auto* owner = static_cast<implementation*>(state);
            owner->cancel_on_context();
            owner->cancellation_lifetime.reset();
        };
        cancellation_post.callback_arg = this;
        cancellation_post.callback_cleanup = [](void* state) noexcept
        {
            static_cast<implementation*>(state)->cancellation_lifetime.reset();
        };
    }

    /**
     * @brief Publishes terminal recovery failure without holding a latch across callbacks.
     */
    void report_exhausted(const std::shared_ptr<entry>& item) noexcept
    {
        std::shared_ptr<const recovery_exhausted_handler> callback;
        {
            concurrent_containers::exclusive_latch_guard lock{latch};
            item->state = supervised_task_state::failed;
            callback = exhausted;
        }
        if (item->required && callback)
        {
            try
            {
                (*callback)(item->name, item->error);
            }
            catch (...)
            {
                // Observer failures must not escape the supervised task.
            }
        }
    }

    auto run(std::string name, std::shared_ptr<entry> item,
        std::shared_ptr<completion_ticket> ticket) -> task<void>
    {
        completion_guard completion{std::move(ticket)};
        auto recovery_limit = item->recovery_deadline;
        std::size_t attempt = 0;
        for (;;)
        {
            if (attempt != 0)
            {
                if (stopping.load(std::memory_order_acquire) || item->cancellation->is_cancelled())
                    break;
                if (recovery_limit.expired())
                {
                    report_exhausted(item);
                    co_return;
                }
            }
            {
                concurrent_containers::exclusive_latch_guard lock{latch};
                item->state = attempt == 0
                    ? supervised_task_state::running
                    : supervised_task_state::recovering;
            }

            std::expected<void, std::error_code> result;
            try
            {
                result = co_await item->operation(*item->cancellation);
            }
            catch (const std::system_error& error)
            {
                result = std::unexpected(error.code());
            }
            catch (const std::bad_alloc&)
            {
                result = std::unexpected(std::make_error_code(std::errc::not_enough_memory));
            }
            catch (...)
            {
                result = std::unexpected(
                    std::make_error_code(std::errc::io_error));
            }

            if (stopping.load(std::memory_order_acquire) ||
                item->cancellation->is_cancelled())
                break;
            if (result)
                break;

            {
                concurrent_containers::exclusive_latch_guard lock{latch};
                item->error = result.error();
            }
            if (recovery_limit.is_unlimited())
                recovery_limit = deadline::after(item->recovery.budget);
            const auto remaining = recovery_limit.remaining();
            if (remaining <= std::chrono::steady_clock::duration::zero())
            {
                report_exhausted(item);
                co_return;
            }

            const auto requested_delay = retry_delay(item->recovery, attempt++);
            if (requested_delay >= remaining)
            {
                report_exhausted(item);
                co_return;
            }
            const auto delay = requested_delay;
            try
            {
                logger::warn("supervised task {} failed; retrying in {} ms",
                    name, std::chrono::duration_cast<std::chrono::milliseconds>(delay).count());
            }
            catch (...)
            {
                /**
                 * Diagnostic failure must not terminate service recovery.
                 */
            }
            auto waited = co_await async_timer_wait(context, delay,
                *item->cancellation);
            if (!waited)
                break;
        }

        {
            concurrent_containers::exclusive_latch_guard lock{latch};
            if (item->stop_error)
            {
                if (item->state != supervised_task_state::failed)
                    item->error = item->stop_error;
                item->state = supervised_task_state::failed;
            }
            else
                item->state = supervised_task_state::stopped;
        }
    }

    /**
     * @brief Cancels every supervised task on the owning I/O thread.
     *
     * Platform cancellation adapters may submit kernel operations and are not
     * generally safe to invoke concurrently with the event loop.
     */
    void cancel_on_context() noexcept
    {
        for (const auto& [name, item] : entries)
        {
            (void)name;
            item->cancellation->dispatch_cancel();
        }
        completed.done();
    }

    io_context& context;
    mutable concurrent_containers::atomic_rw_latch latch;
    std::unordered_map<std::string, std::shared_ptr<entry>, task_name_hash, std::equal_to<>> entries;
    async_wait_group completed;
    std::shared_ptr<const recovery_exhausted_handler> exhausted;
    post_node cancellation_post{};
    std::shared_ptr<implementation> cancellation_lifetime;
    std::atomic<bool> stopping{false};
};

task_supervisor::task_supervisor(io_context& context)
    : implementation_(std::make_shared<implementation>(context))
{
}

task_supervisor::~task_supervisor()
{
    request_stop();
}

auto task_supervisor::supervise(std::string name,
    supervised_task operation, recovery_policy recovery, bool required,
    std::function<void()> stop_request, deadline recovery_deadline)
    -> std::expected<void, std::error_code>
{
    if (name.empty() || !operation || !valid_recovery_policy(recovery))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    if (implementation_->stopping.load(std::memory_order_acquire))
        return std::unexpected(
            std::make_error_code(std::errc::operation_canceled));

    auto item = std::make_shared<implementation::entry>();
    item->name = name;
    item->operation = std::move(operation);
    item->stop_request = std::move(stop_request);
    item->recovery = recovery;
    item->recovery_deadline = recovery_deadline;
    item->required = required;

    auto ticket = std::make_shared<implementation::completion_ticket>();
    ticket->owner = implementation_;
    {
        concurrent_containers::exclusive_latch_guard lock{
            implementation_->latch};
        if (implementation_->stopping.load(std::memory_order_acquire))
            return std::unexpected(
                std::make_error_code(std::errc::operation_canceled));
        if (const auto found = implementation_->entries.find(name);
            found != implementation_->entries.end())
        {
            if (found->second->state != supervised_task_state::failed &&
                found->second->state != supervised_task_state::stopped)
                return std::unexpected(
                    std::make_error_code(std::errc::file_exists));
            implementation_->entries.erase(found);
        }
        implementation_->entries.emplace(name, item);
        implementation_->completed.add();
        ticket->registered.store(true, std::memory_order_release);
    }
    auto failed = [ticket, item](std::exception_ptr failure)
    {
        auto error = std::make_error_code(std::errc::io_error);
        try
        {
            std::rethrow_exception(failure);
        }
        catch (const std::bad_alloc&)
        {
            error = std::make_error_code(std::errc::not_enough_memory);
        }
        catch (const std::system_error& exception)
        {
            error = exception.code();
        }
        catch (...)
        {
        }
        std::shared_ptr<const recovery_exhausted_handler> callback;
        bool already_failed = false;
        {
            concurrent_containers::exclusive_latch_guard lock{ticket->owner->latch};
            if (item->state == supervised_task_state::failed)
                already_failed = true;
            else
            {
                item->state = supervised_task_state::failed;
                item->error = error;
                if (item->required)
                    callback = ticket->owner->exhausted;
            }
        }
        if (already_failed)
        {
            ticket->settle();
            return;
        }
        if (callback)
        {
            try
            {
                (*callback)(item->name, error);
            }
            catch (...)
            {
                // Observer failures must not prevent completion settlement.
            }
        }
        ticket->settle();
    };
    try
    {
        spawn_guarded(implementation_->context,
            implementation_->run(std::move(name), item, ticket), failed);
    }
    catch (...)
    {
        const auto failure = std::current_exception();
        try
        {
            failed(failure);
        }
        catch (...)
        {
        }
        std::rethrow_exception(failure);
    }
    return {};
}

auto task_supervisor::state(std::string_view name) const noexcept
    -> std::optional<supervised_task_state>
{
    concurrent_containers::shared_latch_guard lock{implementation_->latch};
    const auto found = implementation_->entries.find(name);
    if (found == implementation_->entries.end())
        return std::nullopt;
    return found->second->state;
}

auto task_supervisor::last_error(std::string_view name) const noexcept
    -> std::error_code
{
    concurrent_containers::shared_latch_guard lock{implementation_->latch};
    const auto found = implementation_->entries.find(name);
    return found == implementation_->entries.end()
        ? std::error_code{}
        : found->second->error;
}

void task_supervisor::on_recovery_exhausted(
    recovery_exhausted_handler handler)
{
    auto callback = handler ? std::make_shared<const recovery_exhausted_handler>(std::move(handler))
                            : std::shared_ptr<const recovery_exhausted_handler>{};
    concurrent_containers::exclusive_latch_guard lock{implementation_->latch};
    implementation_->exhausted.swap(callback);
}

void task_supervisor::request_stop() noexcept
{
    if (!implementation_)
        return;
    const auto owner = implementation_;
    {
        /**
         * @brief Waits for in-progress registration before traversing frozen membership.
         *
         * Registration rechecks stopping under the exclusive latch. Once this
         * barrier completes, entries cannot be inserted or erased. Callbacks
         * run outside the latch so cancellation adapters may query state.
         */
        concurrent_containers::exclusive_latch_guard lock{owner->latch};
        if (owner->stopping.exchange(true, std::memory_order_acq_rel))
            return;
        owner->completed.add();
    }
    for (const auto& [name, item] : owner->entries)
    {
        (void)name;
        item->cancellation->request_cancel();
        if (!item->stop_request)
            continue;

        std::error_code error;
        try
        {
            item->stop_request();
        }
        catch (const std::system_error& failure)
        {
            error = failure.code();
        }
        catch (const std::bad_alloc&)
        {
            error = std::make_error_code(std::errc::not_enough_memory);
        }
        catch (...)
        {
            error = std::make_error_code(std::errc::io_error);
        }
        if (error)
        {
            concurrent_containers::exclusive_latch_guard lock{owner->latch};
            item->stop_error = error;
            if (item->state != supervised_task_state::failed)
                item->error = error;
            item->state = supervised_task_state::failed;
        }
    }

    if (owner->context.running_in_this_thread())
    {
        owner->cancel_on_context();
        return;
    }

    owner->cancellation_lifetime = owner;
    owner->context.post_node_raw(&owner->cancellation_post);
}

auto task_supervisor::join()
    -> task<std::expected<void, std::error_code>>
{
    const auto joining_thread = std::this_thread::get_id();
    {
        concurrent_containers::shared_latch_guard lock{implementation_->latch};
    }
    co_await implementation_->completed.wait();
    if (std::this_thread::get_id() != joining_thread)
        co_await post_awaitable{implementation_->context};
    concurrent_containers::shared_latch_guard lock{implementation_->latch};
    const implementation::entry* failure = nullptr;
    for (const auto& [name, item] : implementation_->entries)
    {
        if (item->required && item->state == supervised_task_state::failed &&
            (!failure || name < failure->name))
            failure = item.get();
    }
    if (failure)
        co_return std::unexpected(failure->error ? failure->error : std::make_error_code(std::errc::io_error));
    co_return {};
}

} // namespace cnetmod::application
