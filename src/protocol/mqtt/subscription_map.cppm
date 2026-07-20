/// cnetmod.protocol.mqtt:subscription_map — Trie-based subscription matching
/// Efficient topic filter → subscriber mapping, supports +/# wildcard matching

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:subscription_map;

import std;
import :types;

namespace cnetmod::mqtt {

// =============================================================================
// Subscription entry (match result)
// =============================================================================

export struct subscription_entry_ref {
  std::string client_id;
  subscribe_entry entry;
};

// =============================================================================
// Subscription Map — Trie implementation
// =============================================================================

/// Trie-based subscription mapping
/// Splits topic filter by '/' and stores in trie, traverses trie during
/// matching
export class subscription_map {
public:
  subscription_map() = default;

  // Non-copyable
  subscription_map(const subscription_map &) = delete;
  auto operator=(const subscription_map &) -> subscription_map & = delete;

  // Movable
  subscription_map(subscription_map &&) = default;
  auto operator=(subscription_map &&) -> subscription_map & = default;

  /// Insert subscription
  void insert(const std::string &topic_filter, const std::string &client_id,
              const subscribe_entry &entry);

  /// Remove subscription
  /// Returns whether successfully removed
  auto erase(const std::string &topic_filter, const std::string &client_id)
      -> bool;

  /// Remove all subscriptions for a client
  void erase_client(const std::string &client_id);

  /// Match topic name, return all matching subscribers
  [[nodiscard]] auto match(std::string_view topic) const
      -> std::vector<subscription_entry_ref>;

  /// Total subscription count
  [[nodiscard]] auto size() const noexcept -> std::size_t;

  /// Whether empty
  [[nodiscard]] auto empty() const noexcept -> bool;

  /// Clear all
  void clear();

private:
  struct trie_node {
    std::unordered_map<std::string, std::unique_ptr<trie_node>> children;
    std::unordered_map<std::string, subscribe_entry>
        subscribers; // client_id -> entry
  };

  /// Split topic path
  static auto split(std::string_view topic) -> std::vector<std::string_view>;

  /// Recursive matching
  void match_recursive(const trie_node *node,
                       const std::vector<std::string_view> &segments,
                       std::size_t depth, std::string_view original_topic,
                       std::vector<subscription_entry_ref> &result) const;

  /// Recursively remove all subscriptions for a client
  void erase_client_recursive(trie_node *node, const std::string &client_id);

  trie_node root_;
  std::size_t total_subscriptions_ = 0;
};

} // namespace cnetmod::mqtt
