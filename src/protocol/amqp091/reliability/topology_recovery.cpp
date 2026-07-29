module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp091;
import :topology_recovery;
import std;

namespace cnetmod::amqp091 {
void topology_recorder::remember(recorded_exchange v)
{
    std::scoped_lock l(mutex_);
    std::erase_if(topology_.exchanges, [&](const auto& x)
        {
            return x.options.name == v.options.name;
        });
    topology_.exchanges.push_back(std::move(v));
}

void topology_recorder::remember(recorded_queue v)
{
    std::scoped_lock l(mutex_);
    std::erase_if(topology_.queues, [&](const auto& x)
        {
            return (!v.server_name.empty() && x.server_name == v.server_name) ||
                (!v.options.name.empty() && x.options.name == v.options.name);
        });
    topology_.queues.push_back(std::move(v));
}

void topology_recorder::remember(recorded_binding v)
{
    std::scoped_lock l(mutex_);
    std::erase_if(topology_.bindings, [&](const auto& x)
        {
            return x.options.queue == v.options.queue &&
                x.options.exchange == v.options.exchange &&
                x.options.routing_key == v.options.routing_key;
        });
    topology_.bindings.push_back(std::move(v));
}

void topology_recorder::remember(recorded_consumer v)
{
    std::scoped_lock l(mutex_);
    std::erase_if(topology_.consumers, [&](const auto& x)
        {
            return x.options.consumer_tag == v.options.consumer_tag;
        });
    topology_.consumers.push_back(std::move(v));
}

void topology_recorder::forget_exchange(std::string_view name)
{
    std::scoped_lock l(mutex_);
    std::erase_if(topology_.exchanges,
        [&](const auto& v)
        {
            return v.options.name == name;
        });
    std::erase_if(topology_.bindings,
        [&](const auto& v)
        {
            return v.options.exchange == name;
        });
}

void topology_recorder::forget_queue(std::string_view name)
{
    std::scoped_lock l(mutex_);
    std::erase_if(topology_.queues, [&](const auto& v)
        {
            return v.options.name == name || v.server_name == name;
        });
    std::erase_if(topology_.bindings,
        [&](const auto& v)
        {
            return v.options.queue == name;
        });
    std::erase_if(topology_.consumers,
        [&](const auto& v)
        {
            return v.options.queue == name;
        });
}

void topology_recorder::forget_binding(const binding_options& o)
{
    std::scoped_lock l(mutex_);
    std::erase_if(topology_.bindings, [&](const auto& v)
        {
            return v.options.queue == o.queue && v.options.exchange == o.exchange &&
                v.options.routing_key == o.routing_key;
        });
}

void topology_recorder::forget_consumer(std::string_view tag)
{
    std::scoped_lock l(mutex_);
    std::erase_if(topology_.consumers,
        [&](const auto& v)
        {
            return v.options.consumer_tag == tag;
        });
}

void topology_recorder::clear()
{
    std::scoped_lock l(mutex_);
    topology_ = {};
}

auto topology_recorder::snapshot() const -> topology_snapshot
{
    std::scoped_lock l(mutex_);
    return topology_;
}

automatic_recovery_strategy::automatic_recovery_strategy(
    std::shared_ptr<reconnect_policy> p, bool r)
    : policy_(std::move(p)), restore_(r) {}

auto automatic_recovery_strategy::next_delay(
    const reconnect_context& c) const
    -> std::optional<std::chrono::milliseconds>
{
    return policy_ ? policy_->next_delay(c) : std::nullopt;
}

auto automatic_recovery_strategy::restore_topology() const noexcept -> bool
{
    return restore_;
}
} // namespace cnetmod::amqp091
