export module cnetmod.protocol.mongodb:topology_monitor;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
import :error;
import :server_description;
import :connection_options;

export namespace cnetmod::mongodb {

enum class topology_kind
{
    unknown,
    single,
    sharded,
    replica_set_no_primary,
    replica_set_with_primary,
    load_balanced
};
enum class read_preference
{
    primary,
    primary_preferred,
    secondary,
    secondary_preferred,
    nearest
};

struct server_selection_options
{
    read_preference preference = read_preference::primary;
    std::vector<std::map<std::string, std::string>> tag_sets;
    std::chrono::seconds maximum_staleness{0};
    std::chrono::milliseconds local_threshold{15};
};

class topology_monitor
{
public:
    explicit topology_monitor(std::optional<std::string> required_replica_set = {});
    void update(server_description description);
    void mark_unknown(const server_address& address, error reason);
    [[nodiscard]] auto kind() const noexcept -> topology_kind;
    [[nodiscard]] auto snapshot() const -> std::vector<server_description>;
    auto select_server(server_selection_options options = {}) const
        -> result<server_description>;
    auto check_server(io_context& context, connection_options options)
        -> task<result<server_description>>;

private:
    void recompute_kind_locked();
    std::optional<std::string> required_replica_set_;
    // Server discovery publishes a consistent view: server descriptions and
    // derived topology kind are changed under the same atomic latch.
    mutable concurrent_containers::atomic_rw_latch topology_latch_;
    std::map<server_address, server_description> servers_;
    topology_kind kind_ = topology_kind::unknown;
};

} // namespace cnetmod::mongodb
