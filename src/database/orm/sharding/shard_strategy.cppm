/**
 * @brief Pluggable database and table shard routing strategies.
 */
export module cnetmod.orm.sharding.shard_strategy;

import std;
import cnetmod.orm.sharding.error;
import cnetmod.orm.sharding.shard_key;

export namespace cnetmod::orm {

/**
 * @brief Database and table indexes selected for one operation.
 */
struct shard_selection
{
    std::size_t database{};
    std::size_t table{};
};

/**
 * @brief Strategy interface for deterministic shard selection.
 */
class shard_strategy
{
public:
    virtual ~shard_strategy() = default;

    /**
     * @brief Selects database and table indexes for a shard key.
     */
    [[nodiscard]] virtual auto select(const shard_key& key,
        std::size_t database_count, std::size_t table_count) const noexcept
        -> std::expected<shard_selection, std::error_code> = 0;
};

/**
 * @brief Maps a stable key hash independently across database and table counts.
 */
class hash_shard_strategy final : public shard_strategy
{
public:
    /**
     * @brief Selects bounded shard indexes from the key's stable hash.
     */
    [[nodiscard]] auto select(const shard_key& key,
        std::size_t database_count, std::size_t table_count) const noexcept
        -> std::expected<shard_selection, std::error_code> override
    {
        if (key.empty())
            return std::unexpected(make_error_code(sharding_errc::invalid_shard_key));
        if (database_count == 0 || table_count == 0)
            return std::unexpected(make_error_code(sharding_errc::invalid_topology));
        const auto hash = key.stable_hash();
        // SplitMix64 decorrelates table selection from database selection.
        auto table_hash = hash + 0x9e3779b97f4a7c15ULL;
        table_hash = (table_hash ^ (table_hash >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        table_hash = (table_hash ^ (table_hash >> 27U)) * 0x94d049bb133111ebULL;
        table_hash ^= table_hash >> 31U;
        return shard_selection{
            .database = static_cast<std::size_t>(hash % database_count),
            .table = static_cast<std::size_t>(table_hash % table_count),
        };
    }
};

} // namespace cnetmod::orm
