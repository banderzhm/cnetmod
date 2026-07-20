module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.mqtt;

import std;
import :retained;

namespace cnetmod::mqtt {

void retained_store::store(const std::string &topic, retained_message message) {
  if (message.payload.empty()) {
    (void)remove(topic);
    return;
  }
  messages_.insert_or_assign(topic, message);
  trie_insert(topic, std::move(message));
}

auto retained_store::match(std::string_view filter) const
    -> std::vector<retained_message> {
  std::vector<retained_message> result;
  const auto segments = split(filter);
  trie_match(&root_, segments, 0, filter, result);
  return result;
}

auto retained_store::remove(const std::string &topic) -> bool {
  if (messages_.erase(topic) == 0)
    return false;
  trie_erase(topic);
  return true;
}

auto retained_store::find(const std::string &topic) const
    -> const retained_message * {
  const auto iterator = messages_.find(topic);
  return iterator == messages_.end() ? nullptr : &iterator->second;
}

auto retained_store::size() const noexcept -> std::size_t {
  return messages_.size();
}

void retained_store::clear() {
  messages_.clear();
  root_.children.clear();
  root_.message.reset();
}

auto retained_store::messages() noexcept
    -> std::map<std::string, retained_message> & {
  return messages_;
}

auto retained_store::messages() const noexcept
    -> const std::map<std::string, retained_message> & {
  return messages_;
}

auto retained_store::split(std::string_view value)
    -> std::vector<std::string_view> {
  std::vector<std::string_view> segments;
  std::size_t start = 0;
  while (start <= value.size()) {
    const auto separator = value.find('/', start);
    if (separator == std::string_view::npos) {
      segments.push_back(value.substr(start));
      break;
    }
    segments.push_back(value.substr(start, separator - start));
    start = separator + 1;
    if (start == value.size())
      segments.emplace_back();
  }
  return segments;
}

void retained_store::trie_insert(const std::string &topic,
                                 retained_message message) {
  auto *node = &root_;
  for (const auto segment : split(topic)) {
    const auto key = std::string(segment);
    auto [iterator, inserted] = node->children.emplace(key, nullptr);
    if (inserted)
      iterator->second = std::make_unique<trie_node>();
    node = iterator->second.get();
  }
  node->message = std::move(message);
}

void retained_store::trie_erase(const std::string &topic) {
  auto *node = &root_;
  std::vector<std::pair<trie_node *, std::string>> path;
  for (const auto segment : split(topic)) {
    const auto key = std::string(segment);
    path.emplace_back(node, key);
    const auto iterator = node->children.find(key);
    if (iterator == node->children.end())
      return;
    node = iterator->second.get();
  }
  node->message.reset();
  for (auto iterator = path.rbegin(); iterator != path.rend(); ++iterator) {
    auto &[parent, key] = *iterator;
    const auto child_iterator = parent->children.find(key);
    if (child_iterator == parent->children.end())
      break;
    const auto *child = child_iterator->second.get();
    if (child->message || !child->children.empty())
      break;
    parent->children.erase(child_iterator);
  }
}

void retained_store::trie_match(const trie_node *node,
                                const std::vector<std::string_view> &segments,
                                std::size_t depth, std::string_view,
                                std::vector<retained_message> &result) const {
  if (!node)
    return;
  if (depth == segments.size()) {
    if (node->message)
      result.push_back(*node->message);
    return;
  }

  const auto segment = segments[depth];
  if (segment == "#") {
    collect_all(node, result);
    return;
  }
  if (segment == "+") {
    for (const auto &[child_segment, child] : node->children) {
      if (depth == 0 && !child_segment.empty() && child_segment.front() == '$')
        continue;
      trie_match(child.get(), segments, depth + 1, {}, result);
    }
    return;
  }
  const auto iterator = node->children.find(std::string(segment));
  if (iterator != node->children.end())
    trie_match(iterator->second.get(), segments, depth + 1, {}, result);
}

void retained_store::collect_all(const trie_node *node,
                                 std::vector<retained_message> &result) const {
  if (!node)
    return;
  if (node->message)
    result.push_back(*node->message);
  for (const auto &[_, child] : node->children)
    collect_all(child.get(), result);
}

} // namespace cnetmod::mqtt
