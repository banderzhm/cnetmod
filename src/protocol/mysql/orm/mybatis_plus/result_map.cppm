export module cnetmod.protocol.mysql:orm_result_map;

import std;
import :types;
import :orm_xml_parser;

namespace cnetmod::mysql::orm {

// =============================================================================
// result_mapping — Single column-to-property mapping
// =============================================================================

export struct result_mapping {
  std::string property;     // Property name in model
  std::string column;       // Column name in result set
  std::string jdbc_type;    // JDBC type (optional)
  std::string type_handler; // Custom type handler (optional)
  bool is_id = false;       // Is this the ID field?
};

// =============================================================================
// association — One-to-one or many-to-one relationship
// =============================================================================

export struct association {
  std::string property;   // Property name in model
  std::string column;     // Column name for join
  std::string select;     // Statement ID for nested select
  std::string result_map; // ResultMap ID for nested result
  std::string jdbc_type;
};

// =============================================================================
// collection — One-to-many relationship
// =============================================================================

export struct collection {
  std::string property;   // Property name in model (collection)
  std::string column;     // Column name for join
  std::string select;     // Statement ID for nested select
  std::string result_map; // ResultMap ID for nested result
  std::string of_type;    // Element type of collection
};

// =============================================================================
// result_map_def — Complete ResultMap definition
// =============================================================================

export struct result_map_def {
  std::string id;           // ResultMap ID
  std::string type;         // Target type (class name)
  bool auto_mapping = true; // Enable auto-mapping for unmapped columns

  std::vector<result_mapping> id_mappings;     // <id> mappings
  std::vector<result_mapping> result_mappings; // <result> mappings
  std::vector<association> associations;       // <association> mappings
  std::vector<collection> collections;         // <collection> mappings

  // Get all column mappings (id + result)
  auto all_mappings() const -> std::vector<result_mapping>;

  // Find mapping by property name
  auto find_by_property(std::string_view prop) const -> const result_mapping *;

  // Find mapping by column name
  auto find_by_column(std::string_view col) const -> const result_mapping *;
};

// =============================================================================
// result_map_parser — Parse <resultMap> from XML
// =============================================================================

export class result_map_parser {
public:
  static auto parse(const xml_node &node)
      -> std::expected<result_map_def, std::string>;

private:
  static auto parse_result_mapping(const xml_node &node) -> result_mapping;
  static auto parse_association(const xml_node &node) -> association;
  static auto parse_collection(const xml_node &node) -> collection;
};

// =============================================================================
// result_map_registry — Global registry for ResultMaps
// =============================================================================

export class result_map_registry {
public:
  /// Register a ResultMap
  void register_result_map(result_map_def def);

  /// Find ResultMap by ID
  auto find(std::string_view id) const -> const result_map_def *;

  /// Parse and register ResultMap from XML node
  auto load_from_xml(const xml_node &node) -> std::expected<void, std::string>;

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
  static auto apply_to_row(const result_map_def &result_map,
                           const row &result_row,
                           const std::vector<std::string> &column_names)
      -> std::unordered_map<std::string, param_value>;

private:
  // Convert snake_case to camelCase
  static auto snake_to_camel(std::string_view snake) -> std::string;
};

// =============================================================================
// Global result_map_registry instance
// =============================================================================

export auto global_result_map_registry() -> result_map_registry &;

} // namespace cnetmod::mysql::orm
