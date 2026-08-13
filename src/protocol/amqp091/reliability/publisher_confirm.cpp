module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp091;
import :publisher_confirm;
import std;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
import :protocol_constants;

namespace cnetmod::amqp091 {
auto publisher_confirm_tracker::reserve_sequence() noexcept -> std::uint64_t
{
    concurrent_containers::exclusive_latch_guard lock{state_latch_};
    auto tag = next_++;
    pending_.insert(tag);
    return tag;
}

void publisher_confirm_tracker::observe(
    std::weak_ptr<publisher_confirm_observer> o)
{
    concurrent_containers::exclusive_latch_guard lock{state_latch_};
    observers_.push_back(std::move(o));
}

void publisher_confirm_tracker::settle(std::uint64_t tag, bool ack,
    bool multiple)
{
    std::vector<std::shared_ptr<publisher_confirm_observer>> listeners;
    {
        concurrent_containers::exclusive_latch_guard lock{state_latch_};
        if (multiple)
            pending_.erase(pending_.begin(), pending_.upper_bound(tag));
        else
            pending_.erase(tag);
        for (auto it = observers_.begin(); it != observers_.end();)
            if (auto p = it->lock())
            {
                listeners.push_back(std::move(p));
                ++it;
            }
            else
                it = observers_.erase(it);
    }
    publisher_confirmation event{tag, ack, multiple};
    for (auto& x : listeners)
        x->on_confirm(event);
}

void publisher_confirm_tracker::fail_all(error reason)
{
    std::vector<std::shared_ptr<publisher_confirm_observer>> listeners;
    {
        concurrent_containers::exclusive_latch_guard lock{state_latch_};
        pending_.clear();
        for (auto it = observers_.begin(); it != observers_.end();)
            if (auto p = it->lock())
            {
                listeners.push_back(std::move(p));
                ++it;
            }
            else
                it = observers_.erase(it);
    }
    for (auto& x : listeners)
        x->on_confirm_failure(reason);
}

auto publisher_confirm_tracker::pending() const noexcept -> std::size_t
{
    concurrent_containers::shared_latch_guard lock{state_latch_};
    return pending_.size();
}
} // namespace cnetmod::amqp091
