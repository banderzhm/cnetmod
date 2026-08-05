module cnetmod.orm.xml_mapper_registry;

import std;

namespace cnetmod::orm {

auto mapper_registry::load_file(const std::filesystem::path& path)
    -> std::expected<void, std::string>
{
    auto result = parse_xml_file(path);
    if (!result)
        return std::unexpected(result.error());
    return load_mapper_node(std::move(*result));
}

auto mapper_registry::load_xml(std::string_view xml_content)
    -> std::expected<void, std::string>
{
    auto result = parse_xml(xml_content);
    if (!result)
        return std::unexpected(result.error());
    return load_mapper_node(std::move(*result));
}

auto mapper_registry::load_directory(const std::filesystem::path& dir)
    -> std::expected<void, std::string>
{
    if (!std::filesystem::is_directory(dir))
        return std::unexpected("not a directory");
    for (auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".xml")
        {
            auto result = load_file(entry.path());
            if (!result)
                return result;
        }
    }
    return {};
}

auto mapper_registry::find_statement(std::string_view id) const
    -> const xml_node*
{
    if (auto it = id_index_.find(std::string(id)); it != id_index_.end())
    {
        auto& [ns, local_id] = it->second;
        if (auto mapper_it = mappers_.find(ns); mapper_it != mappers_.end())
        {
            if (auto stmt_it = mapper_it->second.statements.find(local_id);
                stmt_it != mapper_it->second.statements.end())
                return &stmt_it->second;
        }
    }
    const auto dot = id.find('.');
    if (dot != std::string_view::npos)
    {
        const auto ns = id.substr(0, dot);
        const auto local_id = id.substr(dot + 1);
        if (auto mapper_it = mappers_.find(std::string(ns));
            mapper_it != mappers_.end())
        {
            if (auto stmt_it =
                    mapper_it->second.statements.find(std::string(local_id));
                stmt_it != mapper_it->second.statements.end())
                return &stmt_it->second;
        }
    }
    return nullptr;
}

auto mapper_registry::get_fragments(std::string_view namespace_id) const
    -> const fragment_map*
{
    if (auto it = mappers_.find(std::string(namespace_id)); it != mappers_.end())
        return &it->second.fragments;
    return nullptr;
}

auto mapper_registry::statement_type(std::string_view id) const
    -> std::string_view
{
    if (auto* stmt = find_statement(id))
        return stmt->tag;
    return {};
}

auto mapper_registry::find_result_map(std::string_view id) const
    -> const result_map_def*
{
    const auto dot = id.find('.');
    if (dot != std::string_view::npos)
    {
        const auto namespace_id = id.substr(0, dot);
        const auto local_id = id.substr(dot + 1);
        if (const auto mapper = mappers_.find(std::string(namespace_id));
            mapper != mappers_.end())
            return mapper->second.result_maps.find(local_id);
        return nullptr;
    }

    // An unqualified resultMap ID is accepted only when it is unambiguous.
    const result_map_def* found = nullptr;
    for (const auto& [_, mapper] : mappers_)
    {
        if (const auto* candidate = mapper.result_maps.find(id))
        {
            if (found)
                return nullptr;
            found = candidate;
        }
    }
    return found;
}

auto mapper_registry::result_maps(std::string_view namespace_id) const
    -> const result_map_registry*
{
    const auto it = mappers_.find(std::string(namespace_id));
    return it == mappers_.end() ? nullptr : &it->second.result_maps;
}

auto mapper_registry::get_namespace(std::string_view id) const
    -> std::string_view
{
    if (auto it = id_index_.find(std::string(id)); it != id_index_.end())
        return it->second.first;
    if (const auto dot = id.find('.'); dot != std::string_view::npos)
        return id.substr(0, dot);
    return {};
}

auto mapper_registry::load_mapper_node(std::unique_ptr<xml_node> root)
    -> std::expected<void, std::string>
{
    if (root->tag != "mapper")
        return std::unexpected("root element must be <mapper>");
    const auto ns = root->attr("namespace");
    if (ns.empty())
        return std::unexpected("<mapper> must have 'namespace' attribute");

    mapper_def def;
    def.namespace_id = std::string(ns);
    for (auto& child : root->children)
    {
        if (child.is_text || !child.element)
            continue;
        const auto& tag = child.element->tag;
        const auto id = child.element->attr("id");
        if (id.empty())
            continue;
        const auto id_string = std::string(id);
        if (tag == "sql")
        {
            def.fragment_nodes[id_string] = std::move(*child.element);
        }
        else if (tag == "resultMap")
        {
            auto loaded = def.result_maps.load_from_xml(*child.element);
            if (!loaded)
                return std::unexpected(std::format(
                    "invalid <resultMap id='{}'>: {}", id, loaded.error()));
        }
        else if (tag == "select" || tag == "insert" || tag == "update" ||
            tag == "delete")
        {
            def.statements[id_string] = std::move(*child.element);
            id_index_[id_string] = {def.namespace_id, id_string};
            id_index_[def.namespace_id + "." + id_string] = {def.namespace_id,
                id_string};
        }
    }
    for (auto& [id, node] : def.fragment_nodes)
        def.fragments[id] = &node;
    mappers_[def.namespace_id] = std::move(def);
    return {};
}

} // namespace cnetmod::orm
