export module cnetmod.protocol.mysql:orm_xml_mapper;

import std;
import :orm_xml_parser;
import :orm_dynamic_sql;

namespace cnetmod::mysql::orm {

// =============================================================================
// mapper_def — Parsed mapper file
// =============================================================================

export struct mapper_def {
    std::string                                    namespace_id;
    std::unordered_map<std::string, xml_node>      statements;
    std::unordered_map<std::string, xml_node>      fragment_nodes;  // Own the fragment nodes
    fragment_map                                    fragments;        // Pointers to fragment_nodes
};

// =============================================================================
// mapper_registry — Global mapper registry
// =============================================================================

export class mapper_registry {
public:
    /// Load a mapper XML file
    auto load_file(const std::filesystem::path& path) -> std::expected<void, std::string> {
        auto result = parse_xml_file(path);
        if (!result) return std::unexpected(result.error());
        return load_mapper_node(std::move(*result));
    }

    /// Load mapper XML from string
    auto load_xml(std::string_view xml_content) -> std::expected<void, std::string> {
        auto result = parse_xml(xml_content);
        if (!result) return std::unexpected(result.error());
        return load_mapper_node(std::move(*result));
    }

    /// Load all .xml files from a directory
    auto load_directory(const std::filesystem::path& dir) -> std::expected<void, std::string> {
        if (!std::filesystem::is_directory(dir))
            return std::unexpected("not a directory");

        for (auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".xml") {
                auto result = load_file(entry.path());
                if (!result) return result;
            }
        }
        return {};
    }

    /// Look up a statement by fully-qualified ID ("namespace.statementId") or plain ID
    auto find_statement(std::string_view id) const -> const xml_node* {
        // Try direct lookup in id_index_
        auto it = id_index_.find(std::string(id));
        if (it != id_index_.end()) {
            auto& [ns, local_id] = it->second;
            auto mapper_it = mappers_.find(ns);
            if (mapper_it != mappers_.end()) {
                auto stmt_it = mapper_it->second.statements.find(local_id);
                if (stmt_it != mapper_it->second.statements.end())
                    return &stmt_it->second;
            }
        }

        // Try splitting by '.'
        auto dot = id.find('.');
        if (dot != std::string_view::npos) {
            auto ns = id.substr(0, dot);
            auto local_id = id.substr(dot + 1);
            auto mapper_it = mappers_.find(std::string(ns));
            if (mapper_it != mappers_.end()) {
                auto stmt_it = mapper_it->second.statements.find(std::string(local_id));
                if (stmt_it != mapper_it->second.statements.end())
                    return &stmt_it->second;
            }
        }

        return nullptr;
    }

    /// Get fragments for a namespace
    auto get_fragments(std::string_view namespace_id) const -> const fragment_map* {
        auto it = mappers_.find(std::string(namespace_id));
        if (it != mappers_.end()) return &it->second.fragments;
        return nullptr;
    }

    /// Get the statement type (select/insert/update/delete)
    auto statement_type(std::string_view id) const -> std::string_view {
        auto* stmt = find_statement(id);
        if (stmt) return stmt->tag;
        return {};
    }

    /// Get namespace from statement ID
    auto get_namespace(std::string_view id) const -> std::string_view {
        auto it = id_index_.find(std::string(id));
        if (it != id_index_.end()) return it->second.first;

        auto dot = id.find('.');
        if (dot != std::string_view::npos) return id.substr(0, dot);

        return {};
    }

private:
    std::unordered_map<std::string, mapper_def> mappers_;
    std::unordered_map<std::string, std::pair<std::string, std::string>> id_index_;

    auto load_mapper_node(std::unique_ptr<xml_node> root) -> std::expected<void, std::string> {
        if (root->tag != "mapper")
            return std::unexpected("root element must be <mapper>");

        auto ns = root->attr("namespace");
        if (ns.empty())
            return std::unexpected("<mapper> must have 'namespace' attribute");

        mapper_def def;
        def.namespace_id = std::string(ns);

        // Extract statements and fragments
        for (auto& child : root->children) {
            if (child.is_text) continue;
            if (!child.element) continue;

            auto& tag = child.element->tag;
            auto id = child.element->attr("id");

            if (tag == "sql") {
                // Fragment definition - move and store ownership
                if (!id.empty()) {
                    std::string id_str(id);
                    def.fragment_nodes[id_str] = std::move(*child.element);
                }
            } else if (tag == "select" || tag == "insert" || tag == "update" || tag == "delete") {
                // Statement
                if (!id.empty()) {
                    def.statements[std::string(id)] = std::move(*child.element);
                    // Index by both plain ID and fully-qualified ID
                    id_index_[std::string(id)] = {def.namespace_id, std::string(id)};
                    id_index_[def.namespace_id + "." + std::string(id)] = {def.namespace_id, std::string(id)};
                }
            }
        }

        // Build fragment pointer map after all nodes are moved
        for (auto& [id, node] : def.fragment_nodes) {
            def.fragments[id] = &node;
        }

        mappers_[def.namespace_id] = std::move(def);
        return {};
    }
};

} // namespace cnetmod::mysql::orm
