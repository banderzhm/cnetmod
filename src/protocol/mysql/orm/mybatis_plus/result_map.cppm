export module cnetmod.protocol.mysql:orm_result_map;

import std;
import :types;
import :orm_xml_parser;

namespace cnetmod::mysql::orm {

// =============================================================================
// result_mapping — Single column-to-property mapping
// =============================================================================

export struct result_mapping {
    std::string property;      // Property name in model
    std::string column;        // Column name in result set
    std::string jdbc_type;     // JDBC type (optional)
    std::string type_handler;  // Custom type handler (optional)
    bool is_id = false;        // Is this the ID field?
};

// =============================================================================
// association — One-to-one or many-to-one relationship
// =============================================================================

export struct association {
    std::string property;      // Property name in model
    std::string column;        // Column name for join
    std::string select;        // Statement ID for nested select
    std::string result_map;    // ResultMap ID for nested result
    std::string jdbc_type;
};

// =============================================================================
// collection — One-to-many relationship
// =============================================================================

export struct collection {
    std::string property;      // Property name in model (collection)
    std::string column;        // Column name for join
    std::string select;        // Statement ID for nested select
    std::string result_map;    // ResultMap ID for nested result
    std::string of_type;       // Element type of collection
};

// =============================================================================
// result_map_def — Complete ResultMap definition
// =============================================================================

export struct result_map_def {
    std::string id;            // ResultMap ID
    std::string type;          // Target type (class name)
    bool auto_mapping = true;  // Enable auto-mapping for unmapped columns

    std::vector<result_mapping> id_mappings;      // <id> mappings
    std::vector<result_mapping> result_mappings;  // <result> mappings
    std::vector<association> associations;        // <association> mappings
    std::vector<collection> collections;          // <collection> mappings

    // Get all column mappings (id + result)
    auto all_mappings() const -> std::vector<result_mapping> {
        std::vector<result_mapping> all;
        all.insert(all.end(), id_mappings.begin(), id_mappings.end());
        all.insert(all.end(), result_mappings.begin(), result_mappings.end());
        return all;
    }

    // Find mapping by property name
    auto find_by_property(std::string_view prop) const -> const result_mapping* {
        for (auto& m : id_mappings) {
            if (m.property == prop) return &m;
        }
        for (auto& m : result_mappings) {
            if (m.property == prop) return &m;
        }
        return nullptr;
    }

    // Find mapping by column name
    auto find_by_column(std::string_view col) const -> const result_mapping* {
        for (auto& m : id_mappings) {
            if (m.column == col) return &m;
        }
        for (auto& m : result_mappings) {
            if (m.column == col) return &m;
        }
        return nullptr;
    }
};

// =============================================================================
// result_map_parser — Parse <resultMap> from XML
// =============================================================================

export class result_map_parser {
public:
    static auto parse(const xml_node& node) -> std::expected<result_map_def, std::string> {
        if (node.tag != "resultMap") {
            return std::unexpected("Expected <resultMap> tag");
        }

        result_map_def def;
        def.id = std::string(node.attr("id"));
        def.type = std::string(node.attr("type"));

        auto auto_mapping_attr = node.attr("autoMapping");
        if (!auto_mapping_attr.empty()) {
            def.auto_mapping = (auto_mapping_attr == "true");
        }

        // Parse children
        for (auto& child : node.children) {
            if (child.is_text) continue;
            if (!child.element) continue;

            auto& tag = child.element->tag;
            if (tag == "id") {
                auto mapping = parse_result_mapping(*child.element);
                mapping.is_id = true;
                def.id_mappings.push_back(std::move(mapping));
            } else if (tag == "result") {
                def.result_mappings.push_back(parse_result_mapping(*child.element));
            } else if (tag == "association") {
                def.associations.push_back(parse_association(*child.element));
            } else if (tag == "collection") {
                def.collections.push_back(parse_collection(*child.element));
            }
        }

        return def;
    }

private:
    static auto parse_result_mapping(const xml_node& node) -> result_mapping {
        result_mapping mapping;
        mapping.property = std::string(node.attr("property"));
        mapping.column = std::string(node.attr("column"));
        mapping.jdbc_type = std::string(node.attr("jdbcType"));
        mapping.type_handler = std::string(node.attr("typeHandler"));
        return mapping;
    }

    static auto parse_association(const xml_node& node) -> association {
        association assoc;
        assoc.property = std::string(node.attr("property"));
        assoc.column = std::string(node.attr("column"));
        assoc.select = std::string(node.attr("select"));
        assoc.result_map = std::string(node.attr("resultMap"));
        assoc.jdbc_type = std::string(node.attr("jdbcType"));
        return assoc;
    }

    static auto parse_collection(const xml_node& node) -> collection {
        collection coll;
        coll.property = std::string(node.attr("property"));
        coll.column = std::string(node.attr("column"));
        coll.select = std::string(node.attr("select"));
        coll.result_map = std::string(node.attr("resultMap"));
        coll.of_type = std::string(node.attr("ofType"));
        return coll;
    }
};

// =============================================================================
// result_map_registry — Global registry for ResultMaps
// =============================================================================

export class result_map_registry {
public:
    /// Register a ResultMap
    void register_result_map(result_map_def def) {
        std::string key = def.id;
        result_maps_[key] = std::move(def);
    }

    /// Find ResultMap by ID
    auto find(std::string_view id) const -> const result_map_def* {
        auto it = result_maps_.find(std::string(id));
        if (it != result_maps_.end()) return &it->second;
        return nullptr;
    }

    /// Parse and register ResultMap from XML node
    auto load_from_xml(const xml_node& node) -> std::expected<void, std::string> {
        auto result = result_map_parser::parse(node);
        if (!result) return std::unexpected(result.error());

        register_result_map(std::move(*result));
        return {};
    }

private:
    std::unordered_map<std::string, result_map_def> result_maps_;
};

// =============================================================================
// result_map_applier — Apply ResultMap to result set
// =============================================================================

export class result_map_applier {
public:
    /// Apply ResultMap to a single row
    /// Returns a map of property -> value
    static auto apply_to_row(const result_map_def& result_map,
                            const row& result_row,
                            const std::vector<std::string>& column_names)
        -> std::unordered_map<std::string, param_value> {

        std::unordered_map<std::string, param_value> properties;

        // Step 1: Apply explicit mappings
        for (auto& mapping : result_map.all_mappings()) {
            // Find column index
            auto it = std::find(column_names.begin(), column_names.end(), mapping.column);
            if (it == column_names.end()) continue;

            std::size_t col_idx = std::distance(column_names.begin(), it);
            if (col_idx >= result_row.size()) continue;

            // Get value
            auto& field = result_row[col_idx];
            param_value value;

            // Convert field to param_value based on type
            if (field.is_null()) {
                value = param_value::null();
            } else if (field.is_int64()) {
                value = param_value::from_int(field.get_int64());
            } else if (field.is_uint64()) {
                value = param_value::from_uint(field.get_uint64());
            } else if (field.is_double()) {
                value = param_value::from_double(field.get_double());
            } else if (field.is_string()) {
                value = param_value::from_string(std::string(field.get_string()));
            } else if (field.is_date()) {
                value.kind = param_value::kind_t::date_kind;
                value.date_val = field.get_date();
            } else if (field.is_datetime()) {
                value.kind = param_value::kind_t::datetime_kind;
                value.datetime_val = field.get_datetime();
            } else if (field.is_time()) {
                value.kind = param_value::kind_t::time_kind;
                value.time_val = field.get_time();
            }

            properties[mapping.property] = value;
        }

        // Step 2: Auto-mapping for unmapped columns
        if (result_map.auto_mapping) {
            for (std::size_t i = 0; i < column_names.size() && i < result_row.size(); ++i) {
                auto& col_name = column_names[i];

                // Skip if already mapped
                if (result_map.find_by_column(col_name)) continue;

                // Convert column name to property name (snake_case -> camelCase)
                std::string prop_name = snake_to_camel(col_name);

                // Get value
                auto& field = result_row[i];
                param_value value;

                if (field.is_null()) {
                    value = param_value::null();
                } else if (field.is_int64()) {
                    value = param_value::from_int(field.get_int64());
                } else if (field.is_uint64()) {
                    value = param_value::from_uint(field.get_uint64());
                } else if (field.is_double()) {
                    value = param_value::from_double(field.get_double());
                } else if (field.is_string()) {
                    value = param_value::from_string(std::string(field.get_string()));
                }

                properties[prop_name] = value;
            }
        }

        return properties;
    }

private:
    // Convert snake_case to camelCase
    static auto snake_to_camel(std::string_view snake) -> std::string {
        std::string camel;
        bool next_upper = false;

        for (char c : snake) {
            if (c == '_') {
                next_upper = true;
            } else if (next_upper) {
                camel.push_back(std::toupper(static_cast<unsigned char>(c)));
                next_upper = false;
            } else {
                camel.push_back(c);
            }
        }

        return camel;
    }
};

// =============================================================================
// Global result_map_registry instance
// =============================================================================

export inline result_map_registry& global_result_map_registry() {
    static result_map_registry instance;
    return instance;
}

} // namespace cnetmod::mysql::orm
