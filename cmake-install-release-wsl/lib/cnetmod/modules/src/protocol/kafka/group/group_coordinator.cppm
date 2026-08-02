module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.kafka.group_coordinator;
import std;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.protocol.kafka.protocol_constants;

export namespace cnetmod::kafka {
enum class rebalance_protocol
{
    eager,
    cooperative
};

struct group_member
{
    std::string member_id;
    bytes metadata;
};

struct group_assignment
{
    std::string member_id;
    bytes assignment;
};

struct group_state
{
    std::string member_id;
    std::optional<std::string> group_instance_id;
    std::int32_t generation = -1;
    std::string protocol_name;
    bool leader = false;
    std::vector<topic_partition> assigned_partitions;
};

class rebalance_listener
{
public:
    virtual ~rebalance_listener() = default;
    virtual auto on_partitions_revoked(std::span<const topic_partition>)
        -> task<void> = 0;
    virtual auto on_partitions_assigned(std::span<const topic_partition>)
        -> task<void> = 0;
};

class assignment_strategy
{
public:
    virtual ~assignment_strategy() = default;
    virtual auto name() const noexcept -> std::string_view = 0;
    virtual auto protocol() const noexcept -> rebalance_protocol = 0;
    virtual auto metadata(std::span<const std::string>,
        std::span<const topic_partition>) -> bytes = 0;
    virtual auto
    assign(const std::vector<group_member>&,
        const std::map<std::string, std::vector<std::int32_t>, std::less<>>&)
        -> result<std::vector<group_assignment>> = 0;
};

class range_assignment final : public assignment_strategy
{
public:
    auto name() const noexcept -> std::string_view override;
    auto protocol() const noexcept -> rebalance_protocol override;
    auto metadata(std::span<const std::string>, std::span<const topic_partition>)
        -> bytes override;
    auto
    assign(const std::vector<group_member>&,
        const std::map<std::string, std::vector<std::int32_t>, std::less<>>&)
        -> result<std::vector<group_assignment>> override;
};

class cooperative_sticky_assignment final : public assignment_strategy
{
public:
    auto name() const noexcept -> std::string_view override;
    auto protocol() const noexcept -> rebalance_protocol override;
    auto metadata(std::span<const std::string>, std::span<const topic_partition>)
        -> bytes override;
    auto
    assign(const std::vector<group_member>&,
        const std::map<std::string, std::vector<std::int32_t>, std::less<>>&)
        -> result<std::vector<group_assignment>> override;
};

class group_backend
{
public:
    virtual ~group_backend() = default;
    virtual auto join(std::string_view, const group_state&,
        std::span<const std::string>, assignment_strategy&,
        cancel_token*) -> task<result<group_state>> = 0;
    virtual auto heartbeat(std::string_view, const group_state&, cancel_token*)
        -> task<result<void>> = 0;
    virtual auto leave(std::string_view, const group_state&, cancel_token*)
        -> task<result<void>> = 0;
};

class group_coordinator
{
public:
    group_coordinator(std::string, std::shared_ptr<group_backend>,
        std::unique_ptr<assignment_strategy>,
        std::optional<std::string> group_instance_id = {});
    auto join(std::span<const std::string>, cancel_token* = nullptr)
        -> task<result<group_state>>;
    auto heartbeat(cancel_token* = nullptr) -> task<result<void>>;
    auto leave(cancel_token* = nullptr) -> task<result<void>>;
    void set_listener(std::weak_ptr<rebalance_listener>);
    [[nodiscard]] auto state() const -> const group_state&;

private:
    std::string group_id_;
    std::shared_ptr<group_backend> backend_;
    std::unique_ptr<assignment_strategy> strategy_;
    std::weak_ptr<rebalance_listener> listener_;
    group_state state_;
};
} // namespace cnetmod::kafka
