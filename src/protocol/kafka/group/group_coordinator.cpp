module cnetmod.protocol.kafka.group_coordinator;
import std;
import cnetmod.protocol.kafka.protocol_value_codec;

namespace cnetmod::kafka {
namespace {
    struct consumer_subscription
    {
        std::vector<std::string> topics;
        std::set<topic_partition> owned;
        bytes user_data;
    };

    auto decode_subscription(std::span<const std::byte> metadata)
        -> result<consumer_subscription>
    {
        protocol::decoder decoder(metadata);
        auto version = decoder.int16();
        auto count = decoder.int32();
        if (!version || !count || *count < 0)
            return std::unexpected(
                make_error(error_code::malformed_response,
                    "invalid consumer subscription metadata"));
        consumer_subscription out;
        out.topics.reserve(static_cast<std::size_t>(*count));
        for (std::int32_t i = 0; i < *count; ++i)
        {
            auto topic = decoder.string();
            if (!topic)
                return std::unexpected(topic.error());
            out.topics.push_back(std::move(*topic));
        }
        auto user_data = decoder.byte_array();
        if (!user_data)
            return std::unexpected(user_data.error());
        if (*user_data)
            out.user_data = std::move(**user_data);
        if (*version >= 1)
        {
            auto topic_count = decoder.int32();
            if (!topic_count || *topic_count < 0)
                return std::unexpected(make_error(error_code::malformed_response,
                    "invalid owned partition metadata"));
            for (std::int32_t i = 0; i < *topic_count; ++i)
            {
                auto topic = decoder.string();
                auto partition_count = decoder.int32();
                if (!topic || !partition_count || *partition_count < 0)
                    return std::unexpected(
                        make_error(error_code::malformed_response,
                            "truncated owned partition metadata"));
                for (std::int32_t p = 0; p < *partition_count; ++p)
                {
                    auto partition = decoder.int32();
                    if (!partition)
                        return std::unexpected(partition.error());
                    out.owned.insert({*topic, *partition});
                }
            }
        }
        return out;
    }

    auto encode_assignment(
        const std::map<std::string, std::vector<std::int32_t>, std::less<>>& topics)
        -> bytes
    {
        protocol::encoder encoder;
        encoder.int16(0);
        encoder.int32(static_cast<std::int32_t>(topics.size()));
        for (auto& [topic, partitions] : topics)
        {
            encoder.string(topic);
            encoder.int32(static_cast<std::int32_t>(partitions.size()));
            for (auto partition : partitions)
                encoder.int32(partition);
        }
        encoder.byte_array(std::nullopt);
        return std::move(encoder).take();
    }
} // namespace

auto range_assignment::name() const noexcept -> std::string_view
{
    return "range";
}

auto range_assignment::protocol() const noexcept -> rebalance_protocol
{
    return rebalance_protocol::eager;
}

auto range_assignment::metadata(std::span<const std::string> topics,
    std::span<const topic_partition>) -> bytes
{
    protocol::encoder encoder;
    encoder.int16(0);
    encoder.int32(static_cast<std::int32_t>(topics.size()));
    for (auto& topic : topics)
        encoder.string(topic);
    encoder.byte_array(std::nullopt);
    return std::move(encoder).take();
}

auto range_assignment::assign(
    const std::vector<group_member>& members,
    const std::map<std::string, std::vector<std::int32_t>, std::less<>>& topics)
    -> result<std::vector<group_assignment>>
{
    std::map<std::string, std::vector<std::string>, std::less<>> subscriptions;
    for (auto& member : members)
    {
        auto subscribed = decode_subscription(member.metadata);
        if (!subscribed)
            return std::unexpected(subscribed.error());
        for (auto& topic : subscribed->topics)
            subscriptions[topic].push_back(member.member_id);
    }
    std::map<std::string,
        std::map<std::string, std::vector<std::int32_t>, std::less<>>,
        std::less<>>
        assignments;
    for (auto& member : members)
        assignments[member.member_id];
    for (auto& [topic, partitions] : topics)
    {
        auto consumers = subscriptions[topic];
        if (consumers.empty())
            continue;
        std::ranges::sort(consumers);
        auto ordered = partitions;
        std::ranges::sort(ordered);
        for (std::size_t member_index = 0; member_index < consumers.size();
             ++member_index)
        {
            auto base = ordered.size() / consumers.size();
            auto extra = ordered.size() % consumers.size();
            auto begin = member_index * base + std::min(member_index, extra);
            auto count = base + (member_index < extra ? 1U : 0U);
            auto& target = assignments[consumers[member_index]][topic];
            target.insert(
                target.end(), ordered.begin() + static_cast<std::ptrdiff_t>(begin),
                ordered.begin() + static_cast<std::ptrdiff_t>(begin + count));
        }
    }
    std::vector<group_assignment> out;
    out.reserve(members.size());
    for (auto& member : members)
        out.push_back(
            {member.member_id, encode_assignment(assignments[member.member_id])});
    return out;
}

auto cooperative_sticky_assignment::name() const noexcept -> std::string_view
{
    return "cooperative-sticky";
}

auto cooperative_sticky_assignment::protocol() const noexcept
    -> rebalance_protocol
{
    return rebalance_protocol::cooperative;
}

auto cooperative_sticky_assignment::metadata(
    std::span<const std::string> topics, std::span<const topic_partition> owned)
    -> bytes
{
    std::map<std::string, std::vector<std::int32_t>, std::less<>> grouped;
    for (auto& partition : owned)
        grouped[partition.topic].push_back(partition.partition);
    protocol::encoder user;
    user.int16(0);
    user.int32(static_cast<std::int32_t>(grouped.size()));
    for (auto& [topic, partitions] : grouped)
    {
        user.string(topic);
        user.int32(static_cast<std::int32_t>(partitions.size()));
        for (auto partition : partitions)
            user.int32(partition);
    }
    auto user_data = std::move(user).take();
    protocol::encoder encoder;
    encoder.int16(1);
    encoder.int32(static_cast<std::int32_t>(topics.size()));
    for (auto& topic : topics)
        encoder.string(topic);
    encoder.byte_array(std::optional<bytes>{std::move(user_data)});
    encoder.int32(static_cast<std::int32_t>(grouped.size()));
    for (auto& [topic, partitions] : grouped)
    {
        encoder.string(topic);
        encoder.int32(static_cast<std::int32_t>(partitions.size()));
        for (auto partition : partitions)
            encoder.int32(partition);
    }
    return std::move(encoder).take();
}

auto cooperative_sticky_assignment::assign(
    const std::vector<group_member>& members,
    const std::map<std::string, std::vector<std::int32_t>, std::less<>>& topics)
    -> result<std::vector<group_assignment>>
{
    std::map<std::string, consumer_subscription, std::less<>> subscriptions;
    std::vector<std::string> member_ids;
    for (auto& member : members)
    {
        auto decoded = decode_subscription(member.metadata);
        if (!decoded)
            return std::unexpected(decoded.error());
        subscriptions.emplace(member.member_id, std::move(*decoded));
        member_ids.push_back(member.member_id);
    }
    std::ranges::sort(member_ids);
    std::map<std::string, std::set<topic_partition>, std::less<>> assigned;
    std::map<topic_partition, std::string> previous_owner;
    std::set<topic_partition> available;
    for (auto& [topic, partitions] : topics)
        for (auto partition : partitions)
            available.insert({topic, partition});
    for (auto& member : member_ids)
    {
        for (auto& partition : subscriptions[member].owned)
        {
            if (!available.contains(partition) ||
                !std::ranges::contains(subscriptions[member].topics,
                    partition.topic) ||
                previous_owner.contains(partition))
                continue;
            previous_owner[partition] = member;
            assigned[member].insert(partition);
        }
    }
    auto eligible = [&](std::string_view member,
                        const topic_partition& partition)
    {
        return std::ranges::contains(subscriptions[std::string(member)].topics,
            partition.topic);
    };
    for (auto& partition : available)
    {
        if (previous_owner.contains(partition))
            continue;
        auto best = member_ids.end();
        for (auto member = member_ids.begin(); member != member_ids.end(); ++member)
            if (eligible(*member, partition) &&
                (best == member_ids.end() ||
                    assigned[*member].size() < assigned[*best].size()))
                best = member;
        if (best != member_ids.end())
            assigned[*best].insert(partition);
    }
    std::set<topic_partition> pending_transfer;
    while (!member_ids.empty())
    {
        auto most = std::ranges::max_element(
            member_ids, {}, [&](auto& member)
            {
                return assigned[member].size();
            });
        auto least = std::ranges::min_element(
            member_ids, {}, [&](auto& member)
            {
                return assigned[member].size();
            });
        if (assigned[*most].size() <= assigned[*least].size() + 1)
            break;
        auto movable = std::ranges::find_if(assigned[*most], [&](auto& partition)
            {
                return eligible(*least, partition);
            });
        if (movable == assigned[*most].end())
            break;
        auto partition = *movable;
        assigned[*most].erase(movable);
        if (auto owner = previous_owner.find(partition);
            owner != previous_owner.end() && owner->second != *least)
            pending_transfer.insert(partition);
        else
            assigned[*least].insert(partition);
    }
    std::vector<group_assignment> out;
    for (auto& member : member_ids)
    {
        std::map<std::string, std::vector<std::int32_t>, std::less<>> grouped;
        for (auto& partition : assigned[member])
            if (!pending_transfer.contains(partition))
                grouped[partition.topic].push_back(partition.partition);
        out.push_back({member, encode_assignment(grouped)});
    }
    return out;
}

group_coordinator::group_coordinator(
    std::string group, std::shared_ptr<group_backend> backend,
    std::unique_ptr<assignment_strategy> strategy,
    std::optional<std::string> instance)
    : group_id_(std::move(group)), backend_(std::move(backend)), strategy_(std::move(strategy))
{
    state_.group_instance_id = std::move(instance);
    if (!strategy_)
        strategy_ = std::make_unique<range_assignment>();
}

auto group_coordinator::join(std::span<const std::string> topics,
    cancel_token* token) -> task<result<group_state>>
{
    auto previous = state_.assigned_partitions;
    if (strategy_->protocol() == rebalance_protocol::eager)
        if (auto listener = listener_.lock(); listener && !previous.empty())
            co_await listener->on_partitions_revoked(previous);
    auto joined =
        co_await backend_->join(group_id_, state_, topics, *strategy_, token);
    if (!joined)
        co_return std::unexpected(joined.error());
    bool cooperative_followup = false;
    if (strategy_->protocol() == rebalance_protocol::cooperative)
    {
        std::vector<topic_partition> revoked, assigned;
        std::ranges::sort(previous);
        auto next = joined->assigned_partitions;
        std::ranges::sort(next);
        std::ranges::set_difference(previous, next, std::back_inserter(revoked));
        std::ranges::set_difference(next, previous, std::back_inserter(assigned));
        cooperative_followup = !revoked.empty();
        if (auto listener = listener_.lock())
        {
            if (!revoked.empty())
                co_await listener->on_partitions_revoked(revoked);
            if (!assigned.empty())
                co_await listener->on_partitions_assigned(assigned);
        }
    }
    else if (auto listener = listener_.lock();
             listener && !joined->assigned_partitions.empty())
        co_await listener->on_partitions_assigned(joined->assigned_partitions);
    state_ = *joined;
    if (cooperative_followup)
        co_return co_await join(topics, token);
    co_return state_;
}

auto group_coordinator::heartbeat(cancel_token* token) -> task<result<void>>
{
    co_return co_await backend_->heartbeat(group_id_, state_, token);
}

auto group_coordinator::leave(cancel_token* token) -> task<result<void>>
{
    auto left = co_await backend_->leave(group_id_, state_, token);
    if (left)
    {
        auto instance = state_.group_instance_id;
        state_ = {};
        state_.group_instance_id = std::move(instance);
    }
    co_return left;
}

void group_coordinator::set_listener(
    std::weak_ptr<rebalance_listener> listener)
{
    listener_ = std::move(listener);
}

auto group_coordinator::state() const -> const group_state&
{
    return state_;
}
} // namespace cnetmod::kafka
