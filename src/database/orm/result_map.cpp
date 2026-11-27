module cnetmod.orm.result_map;

import std;

namespace cnetmod::orm {

auto result_map_def::all_mappings() const -> std::vector<result_mapping>
{
    std::vector<result_mapping> all;
    all.reserve(id_mappings.size() + result_mappings.size());
    all.insert(all.end(), id_mappings.begin(), id_mappings.end());
    all.insert(all.end(), result_mappings.begin(), result_mappings.end());
    return all;
}

auto result_map_def::find_by_property(std::string_view prop) const
    -> const result_mapping*
{
    for (const auto& mapping : id_mappings)
    {
        if (mapping.property == prop)
            return &mapping;
    }
    for (const auto& mapping : result_mappings)
    {
        if (mapping.property == prop)
            return &mapping;
    }
    return nullptr;
}

auto result_map_def::find_by_column(std::string_view col) const
    -> const result_mapping*
{
    for (const auto& mapping : id_mappings)
    {
        if (mapping.column == col)
            return &mapping;
    }
    for (const auto& mapping : result_mappings)
    {
        if (mapping.column == col)
            return &mapping;
    }
    return nullptr;
}

auto result_map_parser::parse(const xml_node& node)
    -> std::expected<result_map_def, std::string>
{
    if (node.tag != "resultMap")
        return std::unexpected("Expected <resultMap> tag");

    result_map_def def;
    def.id = std::string(node.attr("id"));
    def.type = std::string(node.attr("type"));
    if (const auto auto_mapping = node.attr("autoMapping");
        !auto_mapping.empty())
    {
        def.auto_mapping = auto_mapping == "true";
    }

    for (const auto& child : node.children)
    {
        if (child.is_text || !child.element)
            continue;
        const auto& element = *child.element;
        if (element.tag == "id")
        {
            auto mapping = parse_result_mapping(element);
            mapping.is_id = true;
            def.id_mappings.push_back(std::move(mapping));
        }
        else if (element.tag == "result")
        {
            def.result_mappings.push_back(parse_result_mapping(element));
        }
        else if (element.tag == "association")
        {
            def.associations.push_back(parse_association(element));
        }
        else if (element.tag == "collection")
        {
            def.collections.push_back(parse_collection(element));
        }
    }
    return def;
}

auto result_map_parser::parse_result_mapping(const xml_node& node)
    -> result_mapping
{
    return {.property = std::string(node.attr("property")),
        .column = std::string(node.attr("column")),
        .jdbc_type = std::string(node.attr("jdbcType")),
        .type_handler = std::string(node.attr("typeHandler"))};
}

auto result_map_parser::parse_association(const xml_node& node) -> association
{
    return {.property = std::string(node.attr("property")),
        .column = std::string(node.attr("column")),
        .select = std::string(node.attr("select")),
        .result_map = std::string(node.attr("resultMap")),
        .jdbc_type = std::string(node.attr("jdbcType"))};
}

auto result_map_parser::parse_collection(const xml_node& node) -> collection
{
    return {.property = std::string(node.attr("property")),
        .column = std::string(node.attr("column")),
        .select = std::string(node.attr("select")),
        .result_map = std::string(node.attr("resultMap")),
        .of_type = std::string(node.attr("ofType"))};
}

void result_map_registry::register_result_map(result_map_def def)
{
    result_maps_[def.id] = std::move(def);
}

auto result_map_registry::find(std::string_view id) const
    -> const result_map_def*
{
    const auto it = result_maps_.find(std::string(id));
    return it == result_maps_.end() ? nullptr : &it->second;
}

auto result_map_registry::load_from_xml(const xml_node& node)
    -> std::expected<void, std::string>
{
    auto result = result_map_parser::parse(node);
    if (!result)
        return std::unexpected(result.error());
    register_result_map(std::move(*result));
    return {};
}

namespace {
    auto field_to_param_value(const field_value& field) -> param_value
    {
        if (field.is_null())
            return param_value::null();
        if (field.is_int64())
            return param_value::from_int(field.get_int64());
        if (field.is_uint64())
            return param_value::from_uint(field.get_uint64());
        if (field.is_float())
            return param_value::from_double(field.get_float());
        if (field.is_blob())
            return param_value::from_blob(std::string{field.as_string()});
        if (field.is_double())
            return param_value::from_double(field.get_double());
        if (field.is_string())
            return param_value::from_string(std::string(field.get_string()));

        param_value value;
        if (field.is_date())
        {
            value.kind = param_value::kind_t::date_kind;
            value.date_val = field.get_date();
        }
        else if (field.is_datetime())
        {
            value.kind = param_value::kind_t::datetime_kind;
            value.datetime_val = field.get_datetime();
        }
        else if (field.is_time())
        {
            value.kind = param_value::kind_t::time_kind;
            value.time_val = field.get_time();
        }
        return value;
    }

    template <class Value>
    void append_identity_value(std::string& key, Value value)
    {
        const auto bytes = std::bit_cast<std::array<char, sizeof(Value)>>(value);
        key.append(bytes.data(), bytes.size());
    }

    void append_identity_text(std::string& key, std::string_view value)
    {
        append_identity_value(key, value.size());
        key.append(value);
    }

    auto mapped_identity(const result_map_def& result_map,
        const std::unordered_map<std::string, param_value>& values)
        -> std::optional<std::string>
    {
        if (result_map.id_mappings.empty())
            return std::nullopt;

        std::string key;
        for (const auto& id : result_map.id_mappings)
        {
            const auto it = values.find(id.property);
            if (it == values.end())
                return std::nullopt;
            const auto& value = it->second;
            using kind = param_value::kind_t;
            append_identity_value(key, value.kind);
            switch (value.kind)
            {
            case kind::null_kind:
                break;
            case kind::int64_kind:
                append_identity_value(key, value.int_val);
                break;
            case kind::uint64_kind:
                append_identity_value(key, value.uint_val);
                break;
            case kind::double_kind:
                append_identity_value(key, value.double_val == 0.0 ? 0.0 : value.double_val);
                break;
            case kind::string_kind:
            case kind::blob_kind:
                append_identity_text(key, value.str_val);
                break;
            case kind::date_kind:
                append_identity_text(key, value.date_val.to_string());
                break;
            case kind::datetime_kind:
                append_identity_text(key, value.datetime_val.to_string());
                break;
            case kind::time_kind:
                append_identity_text(key, value.time_val.to_string());
                break;
            }
        }
        return key;
    }

    auto has_mapped_identity(const result_map_def& result_map,
        const std::unordered_map<std::string, param_value>& values) -> bool
    {
        const auto present = [&](const result_mapping& mapping) {
            const auto value = values.find(mapping.property);
            return value != values.end() && value->second.kind != param_value::kind_t::null_kind;
        };
        return std::ranges::any_of(result_map.id_mappings, present) ||
            (result_map.id_mappings.empty() &&
                std::ranges::any_of(result_map.result_mappings, present));
    }

    struct result_column_binding
    {
        std::size_t index;
        std::string property;
    };

    using result_column_bindings = std::vector<result_column_binding>;

    template <class Columns, class Name, class AutoProperty>
    auto bind_columns(const result_map_def& map, const Columns& columns,
        Name name, AutoProperty auto_property) -> result_column_bindings
    {
        result_column_bindings bindings;
        bindings.reserve(map.id_mappings.size() + map.result_mappings.size() +
            (map.auto_mapping ? columns.size() : 0));
        const auto bind = [&](const result_mapping& mapping) {
            const auto column = std::ranges::find(columns, mapping.column, name);
            if (column != columns.end())
                bindings.push_back({static_cast<std::size_t>(column - columns.begin()),
                    mapping.property});
        };
        std::ranges::for_each(map.id_mappings, bind);
        std::ranges::for_each(map.result_mappings, bind);
        if (map.auto_mapping)
        {
            for (std::size_t index = 0; index < columns.size(); ++index)
            {
                const std::string_view label = std::invoke(name, columns[index]);
                if (!map.find_by_column(label))
                    bindings.push_back({index, auto_property(label)});
            }
        }
        return bindings;
    }

    auto apply_bound_row(const result_column_bindings& bindings, const row& result_row)
        -> std::unordered_map<std::string, param_value>
    {
        std::unordered_map<std::string, param_value> properties;
        properties.reserve(bindings.size());
        for (const auto& binding : bindings)
        {
            if (binding.index < result_row.size())
                properties.insert_or_assign(binding.property,
                    field_to_param_value(result_row[binding.index]));
        }
        return properties;
    }

    struct joined_result_binding
    {
        std::string property;
        const result_map_def* map;
        result_column_bindings columns;
    };
} // namespace

auto result_map_applier::apply_to_row(
    const result_map_def& result_map, const row& result_row,
    const std::vector<std::string>& column_names)
    -> std::unordered_map<std::string, param_value>
{
    return apply_bound_row(
        bind_columns(result_map, column_names, std::identity{}, snake_to_camel), result_row);
}

auto result_map_applier::materialize_joined(const result_map_def& result_map,
    const result_set& result, const result_map_registry& registry)
    -> std::vector<mapped_object>
{
    if (result.rows.empty())
        return {};

    const auto bind = [&](const result_map_def& map) {
        return bind_columns(map, result.columns, &column_meta::name, snake_to_camel);
    };
    const auto root_columns = bind(result_map);
    const auto bind_joined = [&](const auto& relations) {
        std::vector<joined_result_binding> bindings;
        for (const auto& relation : relations)
        {
            // Relations loaded by a separate select have no columns in these rows.
            if (!relation.select.empty() || relation.result_map.empty())
                continue;
            if (const auto* nested_map = registry.find(relation.result_map))
                bindings.push_back({relation.property, nested_map, bind(*nested_map)});
        }
        return bindings;
    };
    const auto associations = bind_joined(result_map.associations);
    const auto collections = bind_joined(result_map.collections);

    using collection_identities =
        std::unordered_map<std::string_view, std::unordered_set<std::string>>;
    std::vector<mapped_object> roots;
    std::vector<collection_identities> child_indexes;
    std::unordered_map<std::string, std::size_t> root_indexes;
    for (const auto& row : result.rows)
    {
        auto values = apply_bound_row(root_columns, row);
        auto root_index = roots.size();
        if (auto key = mapped_identity(result_map, values))
            root_index = root_indexes.try_emplace(std::move(*key), root_index).first->second;
        if (root_index == roots.size())
        {
            roots.push_back({.values = std::move(values)});
            if (!collections.empty())
                child_indexes.emplace_back();
        }

        auto& root = roots[root_index];
        for (const auto& relation : associations)
        {
            if (root.associations.contains(relation.property))
                continue;
            auto child = apply_bound_row(relation.columns, row);
            if (has_mapped_identity(*relation.map, child))
                root.associations.try_emplace(relation.property,
                    mapped_object{.values = std::move(child)});
        }
        for (const auto& relation : collections)
        {
            auto child = apply_bound_row(relation.columns, row);
            if (!has_mapped_identity(*relation.map, child))
                continue;
            if (auto key = mapped_identity(*relation.map, child);
                key && !child_indexes[root_index][relation.property].insert(std::move(*key)).second)
                continue;
            root.collections[relation.property].push_back({.values = std::move(child)});
        }
    }
    return roots;
}

auto result_map_applier::snake_to_camel(std::string_view snake) -> std::string
{
    std::string camel;
    camel.reserve(snake.size());
    bool uppercase_next = false;
    for (const char character : snake)
    {
        if (character == '_')
        {
            uppercase_next = true;
        }
        else if (uppercase_next)
        {
            camel.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
            uppercase_next = false;
        }
        else
        {
            camel.push_back(character);
        }
    }
    return camel;
}

auto global_result_map_registry() -> result_map_registry&
{
    static result_map_registry instance;
    return instance;
}

} // namespace cnetmod::orm
