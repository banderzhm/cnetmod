module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.mqtt;

import std;
import :subscription_map;

namespace cnetmod::mqtt {

void subscription_map::insert(const std::string &filter,
                              const std::string &client_id,
                              const subscribe_entry &entry) {
  auto *node = &root_;
  for (const auto segment : split(filter)) {
    const auto key = std::string(segment);
    auto [iterator, inserted] = node->children.try_emplace(key);
    if (inserted)
      iterator->second = std::make_unique<trie_node>();
    node = iterator->second.get();
  }
  const auto [_, inserted] =
      node->subscribers.insert_or_assign(client_id, entry);
  if (inserted)
    ++total_subscriptions_;
}

auto subscription_map::erase(const std::string &filter,
                             const std::string &client_id) -> bool {
  auto *node = &root_;
  std::vector<std::pair<trie_node *, std::string>> path;
  for (const auto segment : split(filter)) {
    const auto key = std::string(segment);
    path.emplace_back(node, key);
    const auto iterator = node->children.find(key);
    if (iterator == node->children.end())
      return false;
    node = iterator->second.get();
  }
  if (node->subscribers.erase(client_id) == 0)
    return false;
  --total_subscriptions_;

  for (auto iterator = path.rbegin(); iterator != path.rend(); ++iterator) {
    auto &[parent, key] = *iterator;
    const auto child_iterator = parent->children.find(key);
    if (child_iterator == parent->children.end())
      break;
    const auto *child = child_iterator->second.get();
    if (!child->subscribers.empty() || !child->children.empty())
      break;
    parent->children.erase(child_iterator);
  }
  return true;
}

void subscription_map::erase_client(const std::string &client_id) {
  erase_client_recursive(&root_, client_id);
}

auto subscription_map::match(std::string_view topic) const
    -> std::vector<subscription_entry_ref> {
  std::vector<subscription_entry_ref> result;
  const auto segments = split(topic);
  match_recursive(&root_, segments, 0, topic, result);
  return result;
}

auto subscription_map::size() const noexcept -> std::size_t {
  return total_subscriptions_;
}

auto subscription_map::empty() const noexcept -> bool {
  return total_subscriptions_ == 0;
}

void subscription_map::clear() {
  root_.children.clear();
  root_.subscribers.clear();
  total_subscriptions_ = 0;
}

auto subscription_map::split(std::string_view topic)
    -> std::vector<std::string_view> {
  std::vector<std::string_view> segments;
  std::size_t start = 0;
  while (start <= topic.size()) {
    const auto separator = topic.find('/', start);
    if (separator == std::string_view::npos) {
      segments.push_back(topic.substr(start));
      break;
    }
    segments.push_back(topic.substr(start, separator - start));
    start = separator + 1;
    if (start == topic.size())
      segments.emplace_back();
  }
  return segments;
}

void subscription_map::match_recursive(
    const trie_node *node, const std::vector<std::string_view> &segments,
    std::size_t depth, std::string_view original_topic,
    std::vector<subscription_entry_ref> &result) const {
  if (!node)
    return;
  const bool dollar_block =
      depth == 0 && !original_topic.empty() && original_topic.front() == '$';

  const auto hash = node->children.find("#");
  if (hash != node->children.end() && !dollar_block) {
    for (const auto &[client_id, entry] : hash->second->subscribers)
      result.push_back({client_id, entry});
  }
  if (depth == segments.size()) {
    for (const auto &[client_id, entry] : node->subscribers)
      result.push_back({client_id, entry});
    return;
  }

  const auto plus = node->children.find("+");
  if (plus != node->children.end() && !dollar_block)
    match_recursive(plus->second.get(), segments, depth + 1, original_topic,
                    result);

  const auto exact = node->children.find(std::string(segments[depth]));
  if (exact != node->children.end())
    match_recursive(exact->second.get(), segments, depth + 1, original_topic,
                    result);
}

void subscription_map::erase_client_recursive(trie_node *node,
                                              const std::string &client_id) {
  if (node->subscribers.erase(client_id) != 0)
    --total_subscriptions_;
  for (auto iterator = node->children.begin();
       iterator != node->children.end();) {
    erase_client_recursive(iterator->second.get(), client_id);
    if (iterator->second->subscribers.empty() &&
        iterator->second->children.empty())
      iterator = node->children.erase(iterator);
    else
      ++iterator;
  }
}

} // namespace cnetmod::mqtt
