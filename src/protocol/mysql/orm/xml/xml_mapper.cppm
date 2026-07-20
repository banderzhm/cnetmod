export module cnetmod.protocol.mysql:orm_xml_mapper;

import std;
import :orm_xml_parser;
import :orm_dynamic_sql;

namespace cnetmod::mysql::orm {

// =============================================================================
// mapper_def — Parsed mapper file
// =============================================================================

export struct mapper_def {
  std::string namespace_id;
  std::unordered_map<std::string, xml_node> statements;
  std::unordered_map<std::string, xml_node>
      fragment_nodes;     // Own the fragment nodes
  fragment_map fragments; // Pointers to fragment_nodes
};

// =============================================================================
// mapper_registry — Global mapper registry
// =============================================================================

export class mapper_registry {
public:
  /// Load a mapper XML file
  auto load_file(const std::filesystem::path &path)
      -> std::expected<void, std::string>;

  /// Load mapper XML from string
  auto load_xml(std::string_view xml_content)
      -> std::expected<void, std::string>;

  /// Load all .xml files from a directory
  auto load_directory(const std::filesystem::path &dir)
      -> std::expected<void, std::string>;

  /// Look up a statement by fully-qualified ID ("namespace.statementId") or
  /// plain ID
  auto find_statement(std::string_view id) const -> const xml_node *;

  /// Get fragments for a namespace
  auto get_fragments(std::string_view namespace_id) const
      -> const fragment_map *;

  /// Get the statement type (select/insert/update/delete)
  auto statement_type(std::string_view id) const -> std::string_view;

  /// Get namespace from statement ID
  auto get_namespace(std::string_view id) const -> std::string_view;

private:
  std::unordered_map<std::string, mapper_def> mappers_;
  std::unordered_map<std::string, std::pair<std::string, std::string>>
      id_index_;

  auto load_mapper_node(std::unique_ptr<xml_node> root)
      -> std::expected<void, std::string>;
};

} // namespace cnetmod::mysql::orm
