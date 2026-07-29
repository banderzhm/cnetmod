module cnetmod.protocol.kafka.broker_metadata;
import std;

namespace cnetmod::kafka {
void metadata_cache::update(protocol::metadata_response m)
{
    std::vector<std::shared_ptr<metadata_observer>> live;
    {
        std::unique_lock l(mutex_);
        data_ = std::move(m);
        std::erase_if(observers_, [&](auto& w)
            {
                if (auto x = w.lock())
                {
                    live.push_back(x);
                    return false;
                }
                return true;
            });
    }
    for (auto& o : live)
        o->on_metadata_changed(data_);
}

auto metadata_cache::broker(std::int32_t id) const
    -> std::optional<broker_endpoint>
{
    std::shared_lock l(mutex_);
    for (auto& b : data_.brokers)
        if (b.node_id == id)
            return b;
    return std::nullopt;
}

auto metadata_cache::leader(const topic_partition& tp) const
    -> result<broker_endpoint>
{
    std::shared_lock l(mutex_);
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
    std::shared_lock l(mutex_);
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
    std::shared_lock l(mutex_);
    return data_;
}

void metadata_cache::add_observer(std::weak_ptr<metadata_observer> o)
{
    std::unique_lock l(mutex_);
    observers_.push_back(std::move(o));
}
} // namespace cnetmod::kafka
