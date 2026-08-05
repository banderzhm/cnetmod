module cnetmod.orm.result_map;

import std;

namespace cnetmod::orm {

auto result_map_def::all_mappings() const -> std::vector<result_mapping>
{
    std::vector<result_mapping> all;
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

    auto mapped_identity(const result_map_def& result_map,
        const std::unordered_map<std::string, param_value>& values,
        std::size_t row_index) -> std::string
    {
        if (result_map.id_mappings.empty())
            return std::format("#{}", row_index);
        std::string key;
        for (const auto& id : result_map.id_mappings)
        {
            const auto it = values.find(id.property);
            if (it == values.end())
                return std::format("#{}", row_index);
            key += id.property;
            key += '=';
            switch (it->second.kind)
            {
            case param_value::kind_t::int64_kind: key += std::to_string(it->second.int_val); break;
            case param_value::kind_t::uint64_kind: key += std::to_string(it->second.uint_val); break;
            case param_value::kind_t::double_kind: key += std::to_string(it->second.double_val); break;
            case param_value::kind_t::string_kind:
            case param_value::kind_t::blob_kind: key += it->second.str_val; break;
            case param_value::kind_t::null_kind: key += "<null>"; break;
            default: key += "<temporal>"; break;
            }
            key += ';';
        }
        return key;
    }
} // namespace

auto result_map_applier::apply_to_row(
    const result_map_def& result_map, const row& result_row,
    const std::vector<std::string>& column_names)
    -> std::unordered_map<std::string, param_value>
{
    std::unordered_map<std::string, param_value> properties;
    for (const auto& mapping : result_map.all_mappings())
    {
        const auto it =
            std::find(column_names.begin(), column_names.end(), mapping.column);
        if (it == column_names.end())
            continue;
        const auto index =
            static_cast<std::size_t>(std::distance(column_names.begin(), it));
        if (index < result_row.size())
            properties[mapping.property] = field_to_param_value(result_row[index]);
    }

    if (result_map.auto_mapping)
    {
        const auto count = std::min(column_names.size(), result_row.size());
        for (std::size_t index = 0; index < count; ++index)
        {
            if (!result_map.find_by_column(column_names[index]))
            {
                properties[snake_to_camel(column_names[index])] =
                    field_to_param_value(result_row[index]);
            }
        }
    }
    return properties;
}

auto result_map_applier::materialize_joined(const result_map_def& result_map,
    const result_set& result, const result_map_registry& registry)
    -> std::vector<mapped_object>
{
    std::vector<std::string> names;
    names.reserve(result.columns.size());
    for (const auto& column : result.columns)
        names.push_back(column.name);

    std::vector<mapped_object> roots;
    std::unordered_map<std::string, std::size_t> root_indexes;
    for (std::size_t row_index = 0; row_index < result.rows.size(); ++row_index)
    {
        const auto& row = result.rows[row_index];
        auto values = apply_to_row(result_map, row, names);
        const auto key = mapped_identity(result_map, values, row_index);
        const auto [root_it, inserted] = root_indexes.emplace(key, roots.size());
        if (inserted)
            roots.push_back({.values = std::move(values)});
        auto& root = roots[root_it->second];

        for (const auto& relation : result_map.associations)
        {
            // A `select` relation is populated by mapper_session after the
            // parent query; its columns do not belong to this joined row.
            if (!relation.select.empty() || relation.result_map.empty() ||
                root.associations.contains(relation.property))
                continue;
            if (const auto* nested_map = registry.find(relation.result_map))
                root.associations.emplace(relation.property,
                    mapped_object{.values = apply_to_row(*nested_map, row, names)});
        }
        for (const auto& relation : result_map.collections)
        {
            if (!relation.select.empty() || relation.result_map.empty())
                continue;
            const auto* nested_map = registry.find(relation.result_map);
            if (!nested_map)
                continue;
            auto child_values = apply_to_row(*nested_map, row, names);
            const auto child_key = mapped_identity(*nested_map, child_values, row_index);
            auto& children = root.collections[relation.property];
            const auto duplicate = std::ranges::any_of(children,
                [&](const mapped_object& child) {
                    return mapped_identity(*nested_map, child.values, row_index) == child_key;
                });
            if (!duplicate)
                children.push_back({.values = std::move(child_values)});
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
