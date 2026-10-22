module cnetmod.coro.task_group;

import std;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.cancel;
import cnetmod.coro.wait_group;
import cnetmod.io.io_context;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod {
namespace detail {
    void retire_task_group_child(
        const std::shared_ptr<task_group_state>& state, std::size_t slot) noexcept
    {
        {
            concurrent_containers::exclusive_latch_guard lock{state->latch};
            // Stable slot indices let cancellation copy one token under the
            // latch without retaining every completed child for the lifetime
            // of a long-lived connection. Reuse requires no allocation.
            auto& entry = state->tokens[slot];
            entry.active = false;
            entry.next_free = state->first_free;
            state->first_free = slot;
        }
        state->completed.done();
    }

    auto run_task_group_child(std::shared_ptr<task_group_state> state,
        std::shared_ptr<cancel_token> token,
        std::size_t slot,
        std::function<task<std::expected<void, std::error_code>>(cancel_token&)> operation)
        -> task<void>
    {
        try
        {
            auto result = co_await operation(*token);
            if (!result)
                fail_task_group(state, result.error());
        }
        catch (...)
        {
            // task_group is an error-code boundary: never let a detached child
            // terminate the process while it is being structurally joined.
            fail_task_group(state, std::make_error_code(std::errc::operation_canceled));
        }
        retire_task_group_child(state, slot);
    }
} // namespace detail

auto task_group::run(std::function<task<std::expected<void, std::error_code>>(cancel_token&)> operation)
    -> bool
{
    std::shared_ptr<cancel_token> token;
    std::size_t slot{};
    {
        concurrent_containers::exclusive_latch_guard lock{state_->latch};
        if (state_->joining)
            return false;
        slot = state_->first_free;
        if (slot == std::numeric_limits<std::size_t>::max())
        {
            slot = state_->tokens.size();
            token = std::make_shared<cancel_token>();
            state_->tokens.push_back({token});
        }
        else
        {
            auto& entry = state_->tokens[slot];
            // Only recycle an exclusively owned token. A cancellation
            // snapshot or the finishing child may still own the old one;
            // give this child a different token in that case so no delayed
            // cancellation can target a newly registered operation.
            if (entry.token.use_count() == 1)
            {
                token = entry.token;
                token->reset();
            }
            else
                token = std::make_shared<cancel_token>();
            state_->first_free = entry.next_free;
            entry.token = token;
            entry.active = true;
        }
        state_->completed.add();
    }
    auto failed = [state = state_, slot](std::exception_ptr) noexcept
    {
        try
        {
            detail::fail_task_group(state, std::make_error_code(std::errc::operation_canceled));
        }
        catch (...)
        {
            // Sibling cancellation must not strand this completion slot.
        }
        detail::retire_task_group_child(state, slot);
    };
    try
    {
        spawn_guarded(*context_, detail::run_task_group_child(state_, std::move(token), slot, std::move(operation)), failed);
    }
    catch (...)
    {
        failed(std::current_exception());
        return false;
    }
    return true;
}
} // namespace cnetmod
