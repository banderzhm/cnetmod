module cnetmod.orm.sharding.shard_catalog;

import std;

namespace cnetmod::orm {

auto valid_shard_identifier(std::string_view value) noexcept -> bool
{
    constexpr std::size_t portable_identifier_limit = 63;
    if (value.empty() || value.size() > portable_identifier_limit)
        return false;
    const auto first = static_cast<unsigned char>(value.front());
    if (!(std::isalpha(first) || first == '_'))
        return false;
    return std::ranges::all_of(value, [](char character)
        {
            const auto byte = static_cast<unsigned char>(character);
            return std::isalnum(byte) || byte == '_';
        });
}

auto shard_catalog::add_database(std::string instance)
    -> std::expected<void, std::error_code>
{
    if (frozen_ || instance.empty())
        return std::unexpected(make_error_code(sharding_errc::invalid_topology));
    if (std::ranges::find(instances_, instance) != instances_.end())
        return std::unexpected(std::make_error_code(std::errc::file_exists));
    try
    {
        instances_.push_back(std::move(instance));
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
    }
    return {};
}

auto shard_catalog::freeze(std::string logical_table, std::size_t table_count,
    std::shared_ptr<const shard_strategy> strategy)
    -> std::expected<void, std::error_code>
{
    if (frozen_ || instances_.empty() || table_count == 0 || !strategy)
        return std::unexpected(make_error_code(sharding_errc::invalid_topology));
    auto maximum_table = table_count - 1;
    std::size_t suffix_width = 1;
    while (maximum_table >= 10)
    {
        maximum_table /= 10;
        ++suffix_width;
    }
    suffix_width = std::max<std::size_t>(2, suffix_width);
    if (!valid_shard_identifier(logical_table) ||
        logical_table.size() + 1 + suffix_width > 63)
        return std::unexpected(make_error_code(sharding_errc::invalid_table_name));
    logical_table_ = std::move(logical_table);
    table_count_ = table_count;
    strategy_ = std::move(strategy);
    frozen_ = true;
    return {};
}

auto shard_catalog::route(const shard_key& key) const
    -> std::expected<shard_route, std::error_code>
{
    if (!frozen_)
        return std::unexpected(make_error_code(sharding_errc::invalid_topology));
    const auto selected = strategy_->select(key, instances_.size(), table_count_);
    if (!selected || selected->database >= instances_.size() ||
        selected->table >= table_count_)
        return std::unexpected(selected ? make_error_code(sharding_errc::unknown_shard)
                                        : selected.error());
    try
    {
        auto physical_table = std::format("{}_{:0{}}", logical_table_, selected->table,
            std::max<std::size_t>(2, std::to_string(table_count_ - 1).size()));
        if (!valid_shard_identifier(physical_table))
            return std::unexpected(make_error_code(sharding_errc::invalid_table_name));
        return shard_route{
            .instance = instances_[selected->database],
            .physical_table = std::move(physical_table),
            .database_shard = selected->database,
            .table_shard = selected->table,
        };
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
    }
}

auto shard_catalog::database_count() const noexcept -> std::size_t
{
    return instances_.size();
}

auto shard_catalog::table_count() const noexcept -> std::size_t
{
    return table_count_;
}

auto shard_catalog::frozen() const noexcept -> bool
{
    return frozen_;
}

auto shard_catalog::instances() const noexcept
    -> std::span<const std::string>
{
    return instances_;
}

} // namespace cnetmod::orm
