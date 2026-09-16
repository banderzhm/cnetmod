/**
 * @brief Error contract for ORM shard routing and transaction boundaries.
 */
export module cnetmod.orm.sharding.error;

import std;

export namespace cnetmod::orm {

/**
 * @brief Errors produced before a database command is dispatched.
 */
enum class sharding_errc
{
    invalid_shard_key = 1,
    invalid_topology,
    unknown_shard,
    invalid_table_name,
    cross_shard_transaction,
    scatter_query_required
};

/**
 * @brief Creates a standard error code in the ORM sharding category.
 */
[[nodiscard]] auto make_error_code(sharding_errc error) noexcept
    -> std::error_code;

} // namespace cnetmod::orm

export namespace std {

template <>
struct is_error_code_enum<cnetmod::orm::sharding_errc> : true_type
{
};

} // namespace std
