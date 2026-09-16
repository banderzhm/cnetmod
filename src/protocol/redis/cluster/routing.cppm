export module cnetmod.protocol.redis:routing;

import std;
import :value;

export namespace cnetmod::redis {
/**
 * @brief Network address advertised by a Redis Cluster node.
 */
struct endpoint_info
{
    std::string host;
    std::uint16_t port = 0;
};

/**
 * @brief Redirection mode returned by a Redis Cluster node.
 */
enum class redirect_kind
{
    moved,
    ask
};

/**
 * @brief Parsed MOVED or ASK redirection response.
 */
struct cluster_redirect
{
    redirect_kind kind = redirect_kind::moved;
    std::uint16_t slot = 0;
    endpoint_info endpoint;
};

/**
 * @brief Contiguous slot range and its primary and replica endpoints.
 */
struct cluster_slot_range
{
    std::uint16_t start = 0;
    std::uint16_t end = 0;
    endpoint_info master;
    std::vector<endpoint_info> replicas;
};

/**
 * @brief Command and routing key submitted to an ordered cluster pipeline.
 */
struct cluster_pipeline_item
{
    std::vector<std::string> args;
    std::string key;
};

/**
 * @brief Returns true when every key can participate in one cluster command.
 */
[[nodiscard]] auto keys_share_slot(
    std::span<const std::string_view> keys) noexcept -> bool;

/**
 * @brief Builds a namespaced key with an explicit Redis Cluster hash tag.
 */
[[nodiscard]] auto make_cluster_key(std::string_view key_namespace,
    std::string_view partition, std::string_view key)
    -> std::expected<std::string, std::error_code>;

/**
 * @brief In-memory mapping of all Redis Cluster slots to primary endpoints.
 */
class cluster_slot_cache
{
public:
    /**
     * @brief Removes every cached slot mapping.
     */
    void clear();

    /**
     * @brief Replaces slot mappings from a CLUSTER SLOTS response.
     */
    void update(const std::vector<cluster_slot_range>& ranges);

    /**
     * @brief Updates one slot after a MOVED redirection.
     */
    void update_slot(std::uint16_t slot, endpoint_info endpoint);

    /**
     * @brief Returns the primary endpoint for a numeric slot.
     */
    [[nodiscard]] auto endpoint_for_slot(std::uint16_t slot) const
        -> std::optional<endpoint_info>;

    /**
     * @brief Returns the primary endpoint selected by a key hash slot.
     */
    [[nodiscard]] auto endpoint_for_key(std::string_view key) const
        -> std::optional<endpoint_info>;

    /**
     * @brief Returns the number of slots with a known primary endpoint.
     */
    [[nodiscard]] auto covered_slots() const noexcept -> std::size_t;

private:
    std::array<std::optional<endpoint_info>, 16384> slots_{};
};

[[nodiscard]] auto first_value(const std::vector<resp3_node>& nodes) noexcept
    -> std::string_view;
[[nodiscard]] auto all_values(const std::vector<resp3_node>& nodes)
    -> std::vector<std::string_view>;
[[nodiscard]] auto is_ok(const std::vector<resp3_node>& nodes) noexcept -> bool;
[[nodiscard]] auto has_error(const std::vector<resp3_node>& nodes) noexcept
    -> bool;
[[nodiscard]] auto error_message(const std::vector<resp3_node>& nodes) noexcept
    -> std::string_view;
} // namespace cnetmod::redis
