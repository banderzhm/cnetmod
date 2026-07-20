/// cnetmod.protocol.mqtt:retained — MQTT Retained Message Storage
/// Manages storage, matching, and deletion of retained messages

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:retained;

import std;
import :types;
import :topic_filter;

namespace cnetmod::mqtt {

// =============================================================================
// Retained Message
// =============================================================================

export struct retained_message {
  std::string topic;
  std::string payload;
  qos qos_value = qos::at_most_once;
  properties props; // v5
};

// =============================================================================
// Retained Store — Trie-based Retained Message Management
// =============================================================================

export class retained_store {
public:
  retained_store() = default;

  // Non-copyable
  retained_store(const retained_store &) = delete;
  auto operator=(const retained_store &) -> retained_store & = delete;

  // Movable
  retained_store(retained_store &&) = default;
  auto operator=(retained_store &&) -> retained_store & = default;

  /// Store retained message
  /// If payload is empty, delete the retained message for that topic (MQTT
  /// spec)
  void store(const std::string &topic, retained_message msg);

  /// Match retained messages by topic filter (supports +/# wildcards)
  /// Uses trie to accelerate matching
  [[nodiscard]] auto match(std::string_view topic_filter_str) const
      -> std::vector<retained_message>;

  /// Delete retained message for specified topic
  auto remove(const std::string &topic) -> bool;

  /// Get retained message for specified topic
  [[nodiscard]] auto find(const std::string &topic) const
      -> const retained_message *;

  /// Total retained message count
  [[nodiscard]] auto size() const noexcept -> std::size_t;

  /// Clear all retained messages
  void clear();

  /// Iterate over all retained messages
  template <typename Fn> void for_each(Fn &&fn) const {
    for (auto &[topic, msg] : messages_) {
      fn(msg);
    }
  }

  /// Get underlying map reference (for persistence use)
  [[nodiscard]] auto messages() noexcept
      -> std::map<std::string, retained_message> &;
  [[nodiscard]] auto messages() const noexcept
      -> const std::map<std::string, retained_message> &;

private:
  struct trie_node {
    std::map<std::string, std::unique_ptr<trie_node>> children;
    std::optional<retained_message> message;
  };

  static auto split(std::string_view s) -> std::vector<std::string_view>;

  void trie_insert(const std::string &topic, retained_message msg);

  void trie_erase(const std::string &topic);

  /// Trie matching: +/# in filter used to traverse trie (which stores topic
  /// names)
  void trie_match(const trie_node *node,
                  const std::vector<std::string_view> &filter_segs,
                  std::size_t depth, std::string_view original_filter,
                  std::vector<retained_message> &result) const;

  /// Collect messages from node and all descendants
  void collect_all(const trie_node *node,
                   std::vector<retained_message> &result) const;

  std::map<std::string, retained_message> messages_; // flat map for persistence
  trie_node root_;                                   // trie for matching
};

} // namespace cnetmod::mqtt
