module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.kafka.broker_metadata;
import std;
import cnetmod.protocol.kafka.protocol_constants;
import cnetmod.protocol.kafka.broker_request_codec;

export namespace cnetmod::kafka {
class metadata_observer
{
public:
    virtual ~metadata_observer() = default;
    virtual void on_metadata_changed(const protocol::metadata_response&) = 0;
};

class metadata_cache
{
public:
    void update(protocol::metadata_response);
    [[nodiscard]] auto leader(const topic_partition&) const
        -> result<broker_endpoint>;
    [[nodiscard]] auto partitions(std::string_view) const
        -> std::vector<std::int32_t>;
    [[nodiscard]] auto broker(std::int32_t) const
        -> std::optional<broker_endpoint>;
    [[nodiscard]] auto snapshot() const -> protocol::metadata_response;
    void add_observer(std::weak_ptr<metadata_observer>);

private:
    mutable std::shared_mutex mutex_;
    protocol::metadata_response data_;
    std::vector<std::weak_ptr<metadata_observer>> observers_;
};
} // namespace cnetmod::kafka
