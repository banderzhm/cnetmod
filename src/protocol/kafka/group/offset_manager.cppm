module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.kafka.offset_manager;
import std;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.protocol.kafka.protocol_constants;

export namespace cnetmod::kafka {
struct offset_and_metadata
{
    std::int64_t offset = 0;
    std::optional<std::int32_t> leader_epoch;
    std::string metadata;
};

class offset_backend
{
public:
    virtual ~offset_backend() = default;
    virtual auto commit(std::string_view, std::int32_t, std::string_view,
        const std::map<topic_partition, offset_and_metadata>&,
        cancel_token*) -> task<result<void>> = 0;
    virtual auto fetch(std::string_view, std::span<const topic_partition>,
        cancel_token*)
        -> task<result<std::map<topic_partition, offset_and_metadata>>> = 0;
};

class offset_manager
{
public:
    explicit offset_manager(std::shared_ptr<offset_backend>);
    void stage(topic_partition, offset_and_metadata);
    auto commit(std::string_view, std::int32_t, std::string_view,
        cancel_token* = nullptr) -> task<result<void>>;
    auto fetch(std::string_view, std::span<const topic_partition>,
        cancel_token* = nullptr)
        -> task<result<std::map<topic_partition, offset_and_metadata>>>;

private:
    std::shared_ptr<offset_backend> backend_;
    std::map<topic_partition, offset_and_metadata> staged_;
};
} // namespace cnetmod::kafka
