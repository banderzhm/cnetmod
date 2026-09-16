module cnetmod.orm.sharding.error;

import std;

namespace cnetmod::orm {
namespace {

    class sharding_error_category final : public std::error_category
    {
    public:
        [[nodiscard]] auto name() const noexcept -> const char* override
        {
            return "cnetmod.orm.sharding";
        }

        [[nodiscard]] auto message(int value) const -> std::string override
        {
            switch (static_cast<sharding_errc>(value))
            {
            case sharding_errc::invalid_shard_key:
                return "invalid shard key";
            case sharding_errc::invalid_topology:
                return "invalid shard topology";
            case sharding_errc::unknown_shard:
                return "unknown shard";
            case sharding_errc::invalid_table_name:
                return "invalid physical table name";
            case sharding_errc::cross_shard_transaction:
                return "cross-shard transaction is not supported";
            case sharding_errc::scatter_query_required:
                return "query requires explicit scatter execution";
            }
            return "unknown ORM sharding error";
        }
    };

    const sharding_error_category category;

} // namespace

auto make_error_code(sharding_errc error) noexcept -> std::error_code
{
    return {static_cast<int>(error), category};
}

} // namespace cnetmod::orm
