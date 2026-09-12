/// cnetmod.protocol.openai:filters — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :foundation;
import :filters;

namespace cnetmod::openai {

struct metadata_filter::expression
{
    enum class operation
    {
        condition,
        conjunction,
        disjunction,
        negation
    };

    operation operation_kind = operation::condition;
    std::string field;
    metadata_operator comparison = metadata_operator::equal;
    json expected;
    std::vector<std::shared_ptr<const expression>> children;
};

namespace {
    auto find_field(const json& metadata, std::string_view path)
        -> const json*
    {
        const json* current = &metadata;
        std::size_t begin = 0;
        while (begin <= path.size())
        {
            const auto end = path.find('.', begin);
            const auto part = path.substr(begin,
                end == std::string_view::npos ? path.size() - begin
                                              : end - begin);
            if (part.empty() || !current->is_object())
                return nullptr;
            const auto found = current->find(std::string(part));
            if (found == current->end())
                return nullptr;
            current = &*found;
            if (end == std::string_view::npos)
                return current;
            begin = end + 1;
        }
        return current;
    }

    auto ordered_compare(const json& actual, const json& expected)
        -> std::optional<std::strong_ordering>
    {
        if (actual.is_number() && expected.is_number())
        {
            const auto left = actual.get<double>();
            const auto right = expected.get<double>();
            if (left < right)
                return std::strong_ordering::less;
            if (left > right)
                return std::strong_ordering::greater;
            return std::strong_ordering::equal;
        }
        if (actual.is_string() && expected.is_string())
            return actual.get_ref<const std::string&>() <=>
                expected.get_ref<const std::string&>();
        return std::nullopt;
    }

    auto compare_value(const json* actual, metadata_operator comparison,
        const json& expected) -> bool
    {
        if (!actual)
            return comparison == metadata_operator::not_equal;
        if (comparison == metadata_operator::equal)
            return *actual == expected;
        if (comparison == metadata_operator::not_equal)
            return *actual != expected;
        if (comparison == metadata_operator::contains)
        {
            if (actual->is_string() && expected.is_string())
                return actual->get_ref<const std::string&>().contains(
                    expected.get_ref<const std::string&>());
            if (actual->is_array())
                return std::ranges::find(*actual, expected) != actual->end();
            return false;
        }
        const auto ordering = ordered_compare(*actual, expected);
        if (!ordering)
            return false;
        switch (comparison)
        {
        case metadata_operator::greater_than:
            return *ordering == std::strong_ordering::greater;
        case metadata_operator::greater_or_equal:
            return *ordering != std::strong_ordering::less;
        case metadata_operator::less_than:
            return *ordering == std::strong_ordering::less;
        case metadata_operator::less_or_equal:
            return *ordering != std::strong_ordering::greater;
        default:
            return false;
        }
    }

} // namespace

auto metadata_filter::evaluate(const expression& value,
    const json& metadata) -> bool
{
    switch (value.operation_kind)
    {
    case expression::operation::condition:
        return compare_value(find_field(metadata, value.field),
            value.comparison, value.expected);
    case expression::operation::conjunction:
        return std::ranges::all_of(value.children,
            [&](const auto& child)
            {
                return evaluate(*child, metadata);
            });
    case expression::operation::disjunction:
        return std::ranges::any_of(value.children,
            [&](const auto& child)
            {
                return evaluate(*child, metadata);
            });
    case expression::operation::negation:
        return value.children.empty() ||
            !evaluate(*value.children.front(), metadata);
    }
    return false;
}

metadata_filter::metadata_filter(std::shared_ptr<const expression> root)
    : root_(std::move(root))
{
}

auto metadata_filter::where(std::string field,
    metadata_operator comparison, json value) -> metadata_filter
{
    if (field.empty())
        throw std::invalid_argument("metadata filter field cannot be empty");
    auto result = std::make_shared<expression>();
    result->field = std::move(field);
    result->comparison = comparison;
    result->expected = std::move(value);
    return metadata_filter{std::move(result)};
}

auto metadata_filter::all_of(std::vector<metadata_filter> filters)
    -> metadata_filter
{
    auto result = std::make_shared<expression>();
    result->operation_kind = expression::operation::conjunction;
    for (auto& filter : filters)
        if (filter.root_)
            result->children.push_back(std::move(filter.root_));
    return metadata_filter{std::move(result)};
}

auto metadata_filter::any_of(std::vector<metadata_filter> filters)
    -> metadata_filter
{
    auto result = std::make_shared<expression>();
    result->operation_kind = expression::operation::disjunction;
    for (auto& filter : filters)
        if (filter.root_)
            result->children.push_back(std::move(filter.root_));
    return metadata_filter{std::move(result)};
}

auto metadata_filter::negate(metadata_filter filter) -> metadata_filter
{
    auto result = std::make_shared<expression>();
    result->operation_kind = expression::operation::negation;
    if (filter.root_)
        result->children.push_back(std::move(filter.root_));
    return metadata_filter{std::move(result)};
}

auto metadata_filter::matches(const json& metadata) const -> bool
{
    return !root_ || evaluate(*root_, metadata);
}

auto metadata_filter::empty() const noexcept -> bool
{
    return !root_;
}

} // namespace cnetmod::openai
