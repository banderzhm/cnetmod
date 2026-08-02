module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp091:topology_recovery;
import std;
import :reconnect_policy;
import :channel_options;
import :field_table_codec;
import :message_delivery;

export namespace cnetmod::amqp091 {
struct recorded_exchange
{
    exchange_declare_options options;
    field_table arguments;
};

struct recorded_queue
{
    queue_declare_options options;
    field_table arguments;
    std::string server_name;
};

struct recorded_binding
{
    binding_options options;
    field_table arguments;
};

struct recorded_consumer
{
    consume_options options;
    field_table arguments;
    delivery_handler handler;
};

struct topology_snapshot
{
    std::vector<recorded_exchange> exchanges;
    std::vector<recorded_queue> queues;
    std::vector<recorded_binding> bindings;
    std::vector<recorded_consumer> consumers;
};

class topology_recorder
{
public:
    void remember(recorded_exchange value);
    void remember(recorded_queue value);
    void remember(recorded_binding value);
    void remember(recorded_consumer value);
    void forget_exchange(std::string_view name);
    void forget_queue(std::string_view name);
    void forget_binding(const binding_options& options);
    void forget_consumer(std::string_view tag);
    void clear();
    [[nodiscard]] auto snapshot() const -> topology_snapshot;

private:
    mutable std::mutex mutex_;
    topology_snapshot topology_;
};

class recovery_strategy
{
public:
    virtual ~recovery_strategy() = default;
    [[nodiscard]] virtual auto
    next_delay(const reconnect_context& context) const
        -> std::optional<std::chrono::milliseconds> = 0;
    [[nodiscard]] virtual auto restore_topology() const noexcept -> bool = 0;
};

class automatic_recovery_strategy final : public recovery_strategy
{
public:
    explicit automatic_recovery_strategy(
        std::shared_ptr<reconnect_policy> policy, bool restore = true);
    [[nodiscard]] auto
    next_delay(const reconnect_context& context) const
        -> std::optional<std::chrono::milliseconds> override;
    [[nodiscard]] auto restore_topology() const noexcept -> bool override;

private:
    std::shared_ptr<reconnect_policy> policy_;
    bool restore_;
};
} // namespace cnetmod::amqp091
