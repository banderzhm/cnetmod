/**
 * @brief Immutable mapping from logical shards to named application services.
 */
export module cnetmod.orm.sharding.shard_catalog;

import std;
import cnetmod.orm.sharding.error;
import cnetmod.orm.sharding.shard_key;
import cnetmod.orm.sharding.shard_strategy;

export namespace cnetmod::orm {

/**
 * @brief Physical destination selected for one ORM operation.
 */
struct shard_route
{
    std::string instance;
    std::string physical_table;
    std::size_t database_shard{};
    std::size_t table_shard{};
};

/**
 * @brief Validates one SQL identifier without accepting quoted fragments.
 */
[[nodiscard]] auto valid_shard_identifier(std::string_view value) noexcept -> bool;

/**
 * @brief Builds and freezes one logical model's shard topology.
 */
class shard_catalog
{
public:
    /**
     * @brief Adds one database shard before the catalog is frozen.
     */
    [[nodiscard]] auto add_database(std::string instance)
        -> std::expected<void, std::error_code>;

    /**
     * @brief Validates and freezes the topology for lock-free reads.
     */
    [[nodiscard]] auto freeze(std::string logical_table,
        std::size_t table_count, std::shared_ptr<const shard_strategy> strategy)
        -> std::expected<void, std::error_code>;

    /**
     * @brief Routes a key to a named service and validated physical table.
     */
    [[nodiscard]] auto route(const shard_key& key) const
        -> std::expected<shard_route, std::error_code>;

    /**
     * @brief Returns the number of configured database shards.
     */
    [[nodiscard]] auto database_count() const noexcept -> std::size_t;

    /**
     * @brief Returns the number of physical tables per database shard.
     */
    [[nodiscard]] auto table_count() const noexcept -> std::size_t;

    /**
     * @brief Reports whether the topology is immutable and routable.
     */
    [[nodiscard]] auto frozen() const noexcept -> bool;

    /**
     * @brief Returns the frozen named database instances in shard order.
     */
    [[nodiscard]] auto instances() const noexcept
        -> std::span<const std::string>;

    /**
     * @brief Enumerates every physical database and table destination.
     *
     * The returned order is stable: database shard first, then table shard.
     * This is the authoritative input for explicit scatter-gather operations.
     */
    [[nodiscard]] auto routes() const
        -> std::expected<std::vector<shard_route>, std::error_code>;

private:
    std::vector<std::string> instances_;
    std::string logical_table_;
    std::size_t table_count_{};
    std::shared_ptr<const shard_strategy> strategy_;
    bool frozen_ = false;
};

} // namespace cnetmod::orm
