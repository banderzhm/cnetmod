module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.quic;

import std;
import :write_queue;

namespace cnetmod::quic {

void quic_write_completion::spin_lock::lock() noexcept
{
    std::uint32_t expected = 0U;
    if (state_.compare_exchange_strong(expected, 1U, std::memory_order_acquire,
            std::memory_order_relaxed))
        return;
    // Advertise a sleeper before waiting; the exchange is a full barrier, so
    // an unlock that observes 1 cannot race past a contender about to sleep.
    if (expected != 2U)
        expected = state_.exchange(2U, std::memory_order_acquire);
    while (expected != 0U)
    {
        state_.wait(2U, std::memory_order_relaxed);
        expected = state_.exchange(2U, std::memory_order_acquire);
    }
}

void quic_write_completion::spin_lock::unlock() noexcept
{
    if (state_.exchange(0U, std::memory_order_release) == 2U)
        state_.notify_one();
}

auto quic_write_completion::receiver::await_ready() noexcept -> bool
{
    std::scoped_lock guard{owner.lock_};
    if (!owner.value_)
        return false;
    value = std::move(owner.value_);
    owner.value_.reset();
    return true;
}

auto quic_write_completion::receiver::await_suspend(
    std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>
{
    std::scoped_lock guard{owner.lock_};
    if (owner.value_)
    {
        value = std::move(owner.value_);
        owner.value_.reset();
        return handle;
    }
    owner.waiter_ = handle;
    return std::noop_coroutine();
}

auto quic_write_completion::receiver::await_resume() noexcept
    -> std::expected<void, std::error_code>
{
    if (!value)
    {
        std::scoped_lock guard{owner.lock_};
        value = std::move(owner.value_);
        owner.value_.reset();
    }
    return std::move(*value);
}

auto quic_write_completion::receive() noexcept -> receiver
{
    return receiver{*this, std::nullopt};
}

void quic_write_completion::complete(
    std::expected<void, std::error_code> value) noexcept
{
    std::coroutine_handle<> waiter;
    {
        std::scoped_lock guard{lock_};
        value_ = std::move(value);
        waiter = std::exchange(waiter_, {});
    }
    if (waiter)
        waiter.resume();
}

quic_write_queue::quic_write_queue()
    : slots_(std::make_unique<quic_write_completion[]>(capacity)),
      available_(capacity),
      commands_(capacity)
{
    for (std::size_t index{}; index < capacity; ++index)
        (void)available_.try_enqueue(std::addressof(slots_[index]));
}

auto quic_write_queue::try_acquire() noexcept -> quic_write_completion*
{
    const auto completion = available_.try_dequeue();
    if (!completion)
        return nullptr;
    (*completion)->rejected_bytes.clear();
    (*completion)->rejected_shared_bytes.reset();
    return *completion;
}

void quic_write_queue::release(quic_write_completion& completion) noexcept
{
    if (!available_.try_enqueue(std::addressof(completion)))
        std::terminate();
}

auto quic_write_queue::try_enqueue(quic_write_command command) noexcept -> bool
{
    return commands_.try_enqueue(std::move(command));
}

auto quic_write_queue::try_dequeue() noexcept -> std::optional<quic_write_command>
{
    return commands_.try_dequeue();
}

auto quic_write_queue::approximate_size() const noexcept -> std::size_t
{
    return commands_.approximate_size();
}

void quic_write_queue::fail_all(std::error_code error) noexcept
{
    while (auto command = commands_.try_dequeue())
        if (command->completion)
            command->completion->complete(std::unexpected(error));
}

} // namespace cnetmod::quic
