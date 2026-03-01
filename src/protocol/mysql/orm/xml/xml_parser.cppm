module;

#include <pugixml.hpp>

export module cnetmod.protocol.mysql:orm_xml_parser;

import std;

namespace cnetmod::mysql::orm {

// =============================================================================
// xml_node — Parsed XML element (forward declared for xml_content)
// =============================================================================

export struct xml_node {
    std::string tag;
    std::unordered_map<std::string, std::string> attrs;
    std::vector<struct xml_content> children;

    auto attr(std::string_view name) const -> std::string_view {
        auto it = attrs.find(std::string(name));
        if (it != attrs.end()) return it->second;
        return {};
    }

    auto has_attr(std::string_view name) const -> bool {
        return attrs.contains(std::string(name));
    }
};

// =============================================================================
// xml_content — Text or element child node
// =============================================================================

export struct xml_content {
    bool        is_text = true;
    std::string text;
    std::unique_ptr<xml_node> element;

    static auto make_text(std::string t) -> xml_content {
        xml_content c;
        c.is_text = true;
        c.text = std::move(t);
        return c;
    }

    static auto make_element(std::unique_ptr<xml_node> e) -> xml_content {
        xml_content c;
        c.is_text = false;
        c.element = std::move(e);
        return c;
    }
};

// =============================================================================
// pugixml adapter — Convert pugixml nodes to our xml_node structure
// =============================================================================

namespace detail {

auto convert_node(const pugi::xml_node& pnode) -> std::unique_ptr<xml_node> {
    auto node = std::make_unique<xml_node>();
    node->tag = pnode.name();

    // Copy attributes
    for (auto attr : pnode.attributes()) {
        node->attrs[attr.name()] = attr.value();
    }

    // Process children
    for (auto child : pnode.children()) {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
            // Text node
            std::string text = child.value();
            // Only trim if the text is purely whitespace, otherwise keep it as-is
            // This preserves spaces in SQL statements like "SELECT ... FROM"
            auto first = text.find_first_not_of(" \t\n\r");
            if (first == std::string::npos) {
                // Pure whitespace, skip it
                continue;
            }
            // Keep the text as-is (don't trim) to preserve SQL spacing
            node->children.push_back(xml_content::make_text(text));
        } else if (child.type() == pugi::node_element) {
            // Element node
            node->children.push_back(xml_content::make_element(convert_node(child)));
        }
        // Ignore comments and other node types
    }

    return node;
}

} // namespace detail

// =============================================================================
// Public API
// =============================================================================

export inline auto parse_xml(std::string_view xml_content)
    -> std::expected<std::unique_ptr<xml_node>, std::string> {
    pugi::xml_document doc;
    auto result = doc.load_buffer(xml_content.data(), xml_content.size());

    if (!result) {
        return std::unexpected(std::format("XML parse error: {} at offset {}",
            result.description(), result.offset));
    }

    // Find the root element (skip XML declaration)
    auto root = doc.first_child();
    while (root && root.type() != pugi::node_element) {
        root = root.next_sibling();
    }

    if (!root) {
        return std::unexpected("No root element found");
    }

    return detail::convert_node(root);
}

export inline auto parse_xml_file(const std::filesystem::path& path)
    -> std::expected<std::unique_ptr<xml_node>, std::string> {
    // Read file content first (more reliable than pugixml's load_file on some platforms)
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(std::format("Failed to open file: {}", path.string()));
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    if (content.empty()) {
        return std::unexpected(std::format("File is empty: {}", path.string()));
    }

    // Parse the content
    pugi::xml_document doc;
    auto result = doc.load_buffer(content.data(), content.size());

    if (!result) {
        return std::unexpected(std::format("Failed to parse XML file {}: {} (offset: {}, status: {})",
            path.string(), result.description(), result.offset, static_cast<int>(result.status)));
    }

    // Find the root element (skip XML declaration)
    auto root = doc.first_child();
    while (root && root.type() != pugi::node_element) {
        root = root.next_sibling();
    }

    if (!root) {
        return std::unexpected("No root element found");
    }

    return detail::convert_node(root);
}

} // namespace cnetmod::mysql::orm

