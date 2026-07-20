module;

#include <pugixml.hpp>

module cnetmod.protocol.mysql;

import std;
import :orm_xml_parser;

namespace cnetmod::mysql::orm {

xml_content::xml_content() = default;
xml_content::~xml_content() = default;
xml_content::xml_content(xml_content &&) noexcept = default;
auto xml_content::operator=(xml_content &&) noexcept -> xml_content & = default;

auto xml_content::make_text(std::string text) -> xml_content {
  xml_content content;
  content.is_text = true;
  content.text = std::move(text);
  return content;
}

auto xml_content::make_element(std::unique_ptr<xml_node> element)
    -> xml_content {
  xml_content content;
  content.is_text = false;
  content.element = std::move(element);
  return content;
}

auto xml_node::attr(std::string_view name) const -> std::string_view {
  const auto it = attrs.find(std::string(name));
  return it != attrs.end() ? std::string_view(it->second) : std::string_view{};
}

auto xml_node::has_attr(std::string_view name) const -> bool {
  return attrs.contains(std::string(name));
}

namespace detail {

auto convert_node(const pugi::xml_node &pnode) -> std::unique_ptr<xml_node> {
  auto node = std::make_unique<xml_node>();
  node->tag = pnode.name();

  for (const auto attr : pnode.attributes()) {
    node->attrs[attr.name()] = attr.value();
  }

  for (const auto child : pnode.children()) {
    if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
      std::string text = child.value();
      if (text.find_first_not_of(" \t\n\r") == std::string::npos)
        continue;
      node->children.push_back(xml_content::make_text(std::move(text)));
    } else if (child.type() == pugi::node_element) {
      node->children.push_back(xml_content::make_element(convert_node(child)));
    }
  }

  return node;
}

auto root_element(const pugi::xml_document &document) -> pugi::xml_node {
  auto root = document.first_child();
  while (root && root.type() != pugi::node_element)
    root = root.next_sibling();
  return root;
}

} // namespace detail

auto parse_xml(std::string_view content)
    -> std::expected<std::unique_ptr<xml_node>, std::string> {
  pugi::xml_document document;
  const auto result = document.load_buffer(content.data(), content.size());
  if (!result) {
    return std::unexpected(std::format("XML parse error: {} at offset {}",
                                       result.description(), result.offset));
  }

  const auto root = detail::root_element(document);
  if (!root)
    return std::unexpected("No root element found");
  return detail::convert_node(root);
}

auto parse_xml_file(const std::filesystem::path &path)
    -> std::expected<std::unique_ptr<xml_node>, std::string> {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return std::unexpected(
        std::format("Failed to open file: {}", path.string()));

  std::string content{std::istreambuf_iterator<char>{file},
                      std::istreambuf_iterator<char>{}};
  if (content.empty())
    return std::unexpected(std::format("File is empty: {}", path.string()));

  pugi::xml_document document;
  const auto result = document.load_buffer(content.data(), content.size());
  if (!result) {
    return std::unexpected(
        std::format("Failed to parse XML file {}: {} (offset: {}, status: {})",
                    path.string(), result.description(), result.offset,
                    static_cast<int>(result.status)));
  }

  const auto root = detail::root_element(document);
  if (!root)
    return std::unexpected("No root element found");
  return detail::convert_node(root);
}

} // namespace cnetmod::mysql::orm
