/// cnetmod.protocol.openai:filters — Composable metadata predicates

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:filters;

import std;
import :foundation;

namespace cnetmod::openai {

export enum class metadata_operator
{
    equal,
    not_equal,
    greater_than,
    greater_or_equal,
    less_than,
    less_or_equal,
    contains
};

/// Immutable Composite expression evaluated against document metadata.
export class metadata_filter
{
public:
    metadata_filter() = default;

    [[nodiscard]] static auto where(std::string field,
        metadata_operator comparison, json value) -> metadata_filter;
    [[nodiscard]] static auto all_of(std::vector<metadata_filter> filters)
        -> metadata_filter;
    [[nodiscard]] static auto any_of(std::vector<metadata_filter> filters)
        -> metadata_filter;
    [[nodiscard]] static auto negate(metadata_filter filter) -> metadata_filter;

    [[nodiscard]] auto matches(const json& metadata) const -> bool;
    [[nodiscard]] auto empty() const noexcept -> bool;

private:
    struct expression;
    explicit metadata_filter(std::shared_ptr<const expression> root);
    [[nodiscard]] static auto evaluate(const expression& value,
        const json& metadata) -> bool;
    std::shared_ptr<const expression> root_;
};

} // namespace cnetmod::openai
