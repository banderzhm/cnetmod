module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp091;
import :publisher_confirm;
import std;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
import :protocol_constants;

namespace cnetmod::amqp091 {
auto publisher_confirm_tracker::reserve_sequence() -> std::uint64_t
{
    concurrent_containers::exclusive_latch_guard lock{state_latch_};
    if (next_ == 0)
        throw std::overflow_error("publisher confirmation sequence exhausted");
    const auto tag = next_;
    pending_.insert(tag);
    ++next_;
    return tag;
}

void publisher_confirm_tracker::observe(
    std::weak_ptr<publisher_confirm_observer> o)
{
    concurrent_containers::exclusive_latch_guard lock{state_latch_};
    auto next = std::make_shared<observer_list>();
    if (observers_)
        for (const auto& observer : *observers_)
            if (!observer.expired())
                next->push_back(observer);
    next->push_back(std::move(o));
    observers_ = std::move(next);
}

void publisher_confirm_tracker::settle(std::uint64_t tag, bool ack,
    bool multiple)
{
    std::shared_ptr<const observer_list> listeners;
    {
        concurrent_containers::exclusive_latch_guard lock{state_latch_};
        if (multiple)
            pending_.erase(pending_.begin(), pending_.upper_bound(tag));
        else
            pending_.erase(tag);
        listeners = observers_;
    }
    publisher_confirmation event{tag, ack, multiple};
    std::exception_ptr first_failure;
    if (listeners)
        for (const auto& observer : *listeners)
            if (auto listener = observer.lock())
                try
                {
                    listener->on_confirm(event);
                }
                catch (...)
                {
                    if (!first_failure)
                        first_failure = std::current_exception();
                }
    if (first_failure)
        std::rethrow_exception(first_failure);
}

void publisher_confirm_tracker::fail_all(const error& reason) noexcept
{
    std::shared_ptr<const observer_list> listeners;
    {
        concurrent_containers::exclusive_latch_guard lock{state_latch_};
        pending_.clear();
        listeners = observers_;
    }
    if (listeners)
        for (const auto& observer : *listeners)
            if (auto listener = observer.lock())
                try
                {
                    listener->on_confirm_failure(reason);
                }
                catch (...)
                {}
}

auto publisher_confirm_tracker::pending() const noexcept -> std::size_t
{
    concurrent_containers::shared_latch_guard lock{state_latch_};
    return pending_.size();
}
} // namespace cnetmod::amqp091
