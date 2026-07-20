/// cnetmod.protocol.mqtt:topic_alias — MQTT v5 Topic Alias management
/// Sender LRU auto-allocates alias, receiver restores topic from alias

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:topic_alias;

import std;
import :types;

namespace cnetmod::mqtt {

// =============================================================================
// Topic Alias Send — Sender LRU alias management
// =============================================================================

/// Sender topic alias manager
/// Maintains topic ↔ alias mapping, uses LRU strategy to evict least recently
/// used alias
export class topic_alias_send {
public:
  explicit topic_alias_send(std::uint16_t max_alias = 0) noexcept;

  /// Set maximum alias count (from CONNACK topic_alias_maximum property)
  void set_max(std::uint16_t max_alias) noexcept;

  /// Whether enabled (max > 0)
  [[nodiscard]] auto enabled() const noexcept -> bool;

  /// Maximum alias count
  [[nodiscard]] auto max() const noexcept -> std::uint16_t;

  /// Find topic by alias
  [[nodiscard]] auto find_by_alias(std::uint16_t alias) const -> std::string;

  /// Find allocated alias by topic
  [[nodiscard]] auto find_by_topic(std::string_view topic) const
      -> std::optional<std::uint16_t>;

  /// Insert or update mapping, and refresh LRU timestamp
  void insert_or_update(std::string_view topic, std::uint16_t alias);

  /// Get LRU (least recently used) alias for reuse by new topic
  /// If there are free aliases, return a free one first
  [[nodiscard]] auto get_lru_alias() const -> std::uint16_t;

  /// Allocate or reuse alias for topic
  /// Returns (alias, is_new) — when is_new=true, need to send both topic and
  /// alias in PUBLISH
  auto allocate(std::string_view topic) -> std::pair<std::uint16_t, bool>;

  /// Clear all mappings
  void clear();

private:
  std::uint16_t max_ = 0;
  std::uint64_t lru_clock_ = 0;
  std::map<std::uint16_t, std::string> alias_to_topic_;
  std::map<std::string, std::uint16_t> topic_to_alias_;
  std::map<std::uint16_t, std::uint64_t> lru_time_;
};

// =============================================================================
// Topic Alias Recv — Receiver alias resolution
// =============================================================================

/// Receiver topic alias manager
/// Restores topic from sender's alias
export class topic_alias_recv {
public:
  explicit topic_alias_recv(std::uint16_t max_alias = 0) noexcept;

  /// Set maximum alias count (our advertised topic_alias_maximum to peer)
  void set_max(std::uint16_t max_alias) noexcept;

  /// Whether enabled
  [[nodiscard]] auto enabled() const noexcept -> bool;

  /// Maximum alias count
  [[nodiscard]] auto max() const noexcept -> std::uint16_t;

  /// Insert or update mapping (when PUBLISH contains both topic and alias)
  void insert_or_update(std::string_view topic, std::uint16_t alias);

  /// Find topic by alias
  [[nodiscard]] auto find(std::uint16_t alias) const -> std::string;

  /// Process received PUBLISH's topic alias
  /// Input: topic (may be empty), alias (from property)
  /// Output: actual topic; returns empty if cannot resolve
  [[nodiscard]] auto resolve(std::string_view topic, std::uint16_t alias)
      -> std::string;

  /// Clear all mappings
  void clear();

private:
  std::uint16_t max_ = 0;
  std::map<std::uint16_t, std::string> aliases_;
};

} // namespace cnetmod::mqtt
