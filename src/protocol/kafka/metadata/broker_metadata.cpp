module cnetmod.protocol.kafka.broker_metadata;
import std;

namespace cnetmod::kafka {
void metadata_cache::update(protocol::metadata_response m)
{
    std::vector<std::shared_ptr<metadata_observer>> live;
    protocol::metadata_response snapshot;
    {
        concurrent_containers::exclusive_latch_guard l{latch_};
        data_ = std::move(m);
        std::erase_if(observers_, [](const auto& w) noexcept
            {
                return w.expired();
            });
        if (observers_.empty())
            return;
        try
        {
            live.reserve(observers_.size());
            for (const auto& w : observers_)
                if (auto observer = w.lock())
                    live.push_back(std::move(observer));
            if (live.empty())
                return;
            snapshot = data_;
        }
        catch (...)
        {
            // Notification preparation is optional; the cache update is committed.
            return;
        }
    }
    for (auto& o : live)
    {
        try
        {
            o->on_metadata_changed(snapshot);
        }
        catch (...)
        {
            // Optional observers cannot invalidate committed metadata or suppress peers.
        }
    }
}

auto metadata_cache::broker(std::int32_t id) const
    -> std::optional<broker_endpoint>
{
    concurrent_containers::shared_latch_guard l{latch_};
    for (auto& b : data_.brokers)
        if (b.node_id == id)
            return b;
    return std::nullopt;
}

auto metadata_cache::leader(const topic_partition& tp) const
    -> result<broker_endpoint>
{
    concurrent_containers::shared_latch_guard l{latch_};
    for (auto& t : data_.topics)
        if (t.name == tp.topic)
            for (auto& p : t.partitions)
                if (p.partition == tp.partition)
                {
                    for (auto& b : data_.brokers)
                        if (b.node_id == p.leader)
                            return b;
                    return std::unexpected(make_error(error_code::leader_not_available));
                }
    return std::unexpected(make_error(error_code::unknown_topic_or_partition));
}

auto metadata_cache::partitions(std::string_view topic) const
    -> std::vector<std::int32_t>
{
    concurrent_containers::shared_latch_guard l{latch_};
    std::vector<std::int32_t> out;
    for (auto& t : data_.topics)
        if (t.name == topic)
            for (auto& p : t.partitions)
                if (p.error == error_code::none)
                    out.push_back(p.partition);
    return out;
}

auto metadata_cache::snapshot() const -> protocol::metadata_response
{
    concurrent_containers::shared_latch_guard l{latch_};
    return data_;
}

void metadata_cache::add_observer(std::weak_ptr<metadata_observer> o)
{
    concurrent_containers::exclusive_latch_guard l{latch_};
    observers_.push_back(std::move(o));
}
} // namespace cnetmod::kafka
