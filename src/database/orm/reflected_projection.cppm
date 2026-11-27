export module cnetmod.orm.reflected_projection;

import std;
import boost.pfr;
import cnetmod.orm.model_metadata;
import cnetmod.orm.result_map;
import cnetmod.orm.sql_query_data;

namespace cnetmod::orm {

namespace detail {

template <class T>
inline constexpr bool is_optional_projection_field = false;

template <class T>
inline constexpr bool is_optional_projection_field<std::optional<T>> = true;

template <class T, std::size_t I>
using reflected_member_type =
    std::remove_cvref_t<decltype(boost::pfr::get<I>(std::declval<T&>()))>;

template <class Owner, class Member>
struct reflected_relation
{
    static constexpr bool enabled = false;
};

template <class Owner, class Child>
requires (!std::same_as<Owner, Child> && ResultRecord<Child>)
struct reflected_relation<Owner, std::optional<Child>>
{
    static constexpr bool enabled = true;

    static auto read(const mapped_object& source, std::string_view property)
        -> std::optional<Child>
    {
        return mapped_association_as<Child>(source, property);
    }
};

template <class Owner, class Child>
requires (!std::same_as<Owner, Child> && ResultRecord<Child>)
struct reflected_relation<Owner, std::vector<Child>>
{
    static constexpr bool enabled = true;

    static auto read(const mapped_object& source, std::string_view property)
        -> std::vector<Child>
    {
        return mapped_collection_as<Child>(source, property);
    }
};

template <class T>
struct reflected_relation_binding
{
    std::string_view property;
    void (*assign)(T&, const mapped_object&, std::string_view);
};

template <class T, std::size_t ScalarCount, std::size_t RelationCount>
struct reflected_bindings
{
    std::array<field_mapping<T>, ScalarCount> fields;
    std::array<reflected_relation_binding<T>, RelationCount> relations;

    void bind_xml(T& target, const mapped_object& source) const
    {
        for (const auto& relation : relations)
            relation.assign(target, source, relation.property);
    }
};

template <class T, std::size_t I>
constexpr auto reflected_field(std::string_view label) -> field_mapping<T>
{
    constexpr auto name = boost::pfr::get_name<I, T>();
    constexpr auto flags = is_optional_projection_field<reflected_member_type<T, I>>
        ? col_flag::nullable : col_flag::none;
    constexpr auto access = [](auto& record) -> decltype(auto) {
        return boost::pfr::get<I>(record);
    };
    return make_field_mapping<T, access>({name, label, column_type::unknown, flags});
}

template <class T, std::size_t... I>
constexpr auto make_reflected_bindings(std::index_sequence<I...>,
    const std::array<std::string_view, sizeof...(I)>& labels)
{
    constexpr auto relation_count = (std::size_t{} + ... +
        std::size_t{reflected_relation<T, reflected_member_type<T, I>>::enabled});
    reflected_bindings<T, sizeof...(I) - relation_count, relation_count> result{};
    std::size_t field_index{};
    std::size_t relation_index{};
    const auto add = [&]<std::size_t Index>() {
        using relation = reflected_relation<T, reflected_member_type<T, Index>>;
        if constexpr (relation::enabled)
        {
            result.relations[relation_index++] = {labels[Index],
                [](T& target, const mapped_object& source, std::string_view property) {
                    boost::pfr::get<Index>(target) = relation::read(source, property);
                }};
        }
        else
            result.fields[field_index++] = reflected_field<T, Index>(labels[Index]);
    };
    (add.template operator()<I>(), ...);
    return result;
}

template <class Fields, class Label>
constexpr auto has_duplicate_labels(const Fields& fields, Label label) -> bool
{
    for (auto field = fields.begin(); field != fields.end(); ++field)
        if (std::ranges::find(fields.begin(), field, label(*field), label) != field)
            return true;
    return false;
}

} // namespace detail

export struct projection_column_alias
{
    std::string_view member;
    std::string_view column;
};

/// Read-only result metadata for a simple aggregate whose member names match SQL column labels.
export template <class T>
struct reflected_projection
{
    static_assert(std::is_aggregate_v<T> && std::default_initializable<T>,
        "A reflected ORM projection must be a default-initializable aggregate.");

    [[nodiscard]] static consteval auto bindings_with_aliases(
        std::initializer_list<projection_column_alias> aliases = {})
    {
        constexpr auto names = boost::pfr::names_as_array<T>();
        auto labels = names;
        for (auto alias = aliases.begin(); alias != aliases.end(); ++alias)
        {
            if (alias->column.empty())
                throw "A projection column alias must not be empty.";
            for (auto previous = aliases.begin(); previous != alias; ++previous)
                if (previous->member == alias->member)
                    throw "Each projection member can have only one column alias.";
            const auto name = std::ranges::find(names, alias->member);
            if (name == names.end())
                throw "A projection column alias must refer to a member of the DTO.";
            labels[static_cast<std::size_t>(name - names.begin())] = alias->column;
        }
        auto bindings = detail::make_reflected_bindings<T>(
            std::make_index_sequence<names.size()>{}, labels);
        if (aliases.size() != 0)
        {
            if (detail::has_duplicate_labels(bindings.fields,
                    [](const auto& field) { return field.col.column_name; }))
                throw "Each reflected projection field must have a distinct column label.";
            if (detail::has_duplicate_labels(bindings.relations,
                    [](const auto& relation) { return relation.property; }))
                throw "Each reflected projection relation must have a distinct XML property.";
        }
        return bindings;
    }

    [[nodiscard]] static consteval auto fields_with_aliases(
        std::initializer_list<projection_column_alias> aliases = {})
    {
        return bindings_with_aliases(aliases).fields;
    }

    [[nodiscard]] static auto meta() noexcept -> const projection_meta<T>&
    {
        static const projection_meta<T> metadata{default_bindings().fields};
        return metadata;
    }

    static void bind_xml(T& target, const mapped_object& source)
    {
        default_bindings().bind_xml(target, source);
    }

private:
    [[nodiscard]] static auto default_bindings() noexcept -> const auto&
    {
        static constexpr auto bindings = bindings_with_aliases();
        return bindings;
    }
};

} // namespace cnetmod::orm
