export module cnetmod.protocol.mysql:orm_xml_parser;

import std;

namespace cnetmod::mysql::orm {

// =============================================================================
// Forward declaration for pointer member in xml_content
// =============================================================================
export struct xml_node;

export struct xml_content {
  bool is_text = true;
  std::string text;
  std::unique_ptr<xml_node> element;

  xml_content();
  ~xml_content();
  xml_content(xml_content &&) noexcept;
  xml_content &operator=(xml_content &&) noexcept;
  xml_content(const xml_content &) = delete;
  xml_content &operator=(const xml_content &) = delete;

  static auto make_text(std::string t) -> xml_content;
  static auto make_element(std::unique_ptr<xml_node> e) -> xml_content;
};

// =============================================================================
// xml_node — Parsed XML element
// =============================================================================

export struct xml_node {
  std::string tag;
  std::unordered_map<std::string, std::string> attrs;
  std::vector<xml_content> children;

  auto attr(std::string_view name) const -> std::string_view;
  auto has_attr(std::string_view name) const -> bool;
};

export auto parse_xml(std::string_view xml_content)
    -> std::expected<std::unique_ptr<xml_node>, std::string>;

export auto parse_xml_file(const std::filesystem::path &path)
    -> std::expected<std::unique_ptr<xml_node>, std::string>;

} // namespace cnetmod::mysql::orm
