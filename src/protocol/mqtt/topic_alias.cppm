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
/// Maintains topic ↔ alias mapping, uses LRU strategy to evict least recently used alias
export class topic_alias_send {
public:
    explicit topic_alias_send(std::uint16_t max_alias = 0) noexcept
        : max_(max_alias) {}

    /// Set maximum alias count (from CONNACK topic_alias_maximum property)
    void set_max(std::uint16_t max_alias) noexcept {
        max_ = max_alias;
        clear();
    }

    /// Whether enabled (max > 0)
    [[nodiscard]] auto enabled() const noexcept -> bool { return max_ > 0; }

    /// Maximum alias count
    [[nodiscard]] auto max() const noexcept -> std::uint16_t { return max_; }

    /// Find topic by alias
    [[nodiscard]] auto find_by_alias(std::uint16_t alias) const -> std::string {
        auto it = alias_to_topic_.find(alias);
        if (it != alias_to_topic_.end()) return it->second;
        return {};
    }

    /// Find allocated alias by topic
    [[nodiscard]] auto find_by_topic(std::string_view topic) const
        -> std::optional<std::uint16_t>
    {
        auto it = topic_to_alias_.find(std::string(topic));
        if (it != topic_to_alias_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /// Insert or update mapping, and refresh LRU timestamp
    void insert_or_update(std::string_view topic, std::uint16_t alias) {
        if (alias == 0 || alias > max_) return;

        auto alias_it = alias_to_topic_.find(alias);
        if (alias_it != alias_to_topic_.end()) {
            // alias already exists, clear old topic → alias mapping first
            topic_to_alias_.erase(alias_it->second);
            alias_it->second = std::string(topic);
        } else {
            alias_to_topic_[alias] = std::string(topic);
        }
        topic_to_alias_[std::string(topic)] = alias;
        lru_time_[alias] = ++lru_clock_;
    }

    /// Get LRU (least recently used) alias for reuse by new topic
    /// If there are free aliases, return a free one first
    [[nodiscard]] auto get_lru_alias() const -> std::uint16_t {
        if (max_ == 0) return 0;

        // Find unused alias
        for (std::uint16_t a = 1; a <= max_; ++a) {
            if (alias_to_topic_.find(a) == alias_to_topic_.end()) {
                return a;
            }
        }

        // All aliases in use, return the oldest LRU
        std::uint16_t lru_alias = 1;
        std::uint64_t min_time = std::numeric_limits<std::uint64_t>::max();
        for (auto& [alias, time] : lru_time_) {
            if (time < min_time) {
                min_time = time;
                lru_alias = alias;
            }
        }
        return lru_alias;
    }

    /// Allocate or reuse alias for topic
    /// Returns (alias, is_new) — when is_new=true, need to send both topic and alias in PUBLISH
    auto allocate(std::string_view topic) -> std::pair<std::uint16_t, bool> {
        if (!enabled()) return {std::uint16_t{0}, false};

        // Already has mapping, reuse directly
        auto existing = find_by_topic(topic);
        if (existing) {
            lru_time_[*existing] = ++lru_clock_;
            return {*existing, false};
        }

        // Allocate new alias
        auto alias = get_lru_alias();
        insert_or_update(topic, alias);
        return {alias, true};
    }

    /// Clear all mappings
    void clear() {
        alias_to_topic_.clear();
        topic_to_alias_.clear();
        lru_time_.clear();
        lru_clock_ = 0;
    }

private:
    std::uint16_t max_ = 0;
    std::uint64_t lru_clock_ = 0;
    std::map<std::uint16_t, std::string>  alias_to_topic_;
    std::map<std::string, std::uint16_t>  topic_to_alias_;
    std::map<std::uint16_t, std::uint64_t> lru_time_;
};

// =============================================================================
// Topic Alias Recv — Receiver alias resolution
// =============================================================================

/// Receiver topic alias manager
/// Restores topic from sender's alias
export class topic_alias_recv {
public:
    explicit topic_alias_recv(std::uint16_t max_alias = 0) noexcept
        : max_(max_alias) {}

    /// Set maximum alias count (our advertised topic_alias_maximum to peer)
    void set_max(std::uint16_t max_alias) noexcept {
        max_ = max_alias;
        clear();
    }

    /// Whether enabled
    [[nodiscard]] auto enabled() const noexcept -> bool { return max_ > 0; }

    /// Maximum alias count
    [[nodiscard]] auto max() const noexcept -> std::uint16_t { return max_; }

    /// Insert or update mapping (when PUBLISH contains both topic and alias)
    void insert_or_update(std::string_view topic, std::uint16_t alias) {
        if (alias == 0 || alias > max_) return;
        aliases_[alias] = std::string(topic);
    }

    /// Find topic by alias
    [[nodiscard]] auto find(std::uint16_t alias) const -> std::string {
        auto it = aliases_.find(alias);
        if (it != aliases_.end()) return it->second;
        return {};
    }

    /// Process received PUBLISH's topic alias
    /// Input: topic (may be empty), alias (from property)
    /// Output: actual topic; returns empty if cannot resolve
    [[nodiscard]] auto resolve(std::string_view topic, std::uint16_t alias)
        -> std::string
    {
        if (alias == 0) {
            // Not using alias, return topic directly
            return std::string(topic);
        }

        if (!topic.empty()) {
            // Both topic and alias present: establish mapping, return topic
            insert_or_update(topic, alias);
            return std::string(topic);
        }

        // topic empty, alias non-zero: find from mapping
        return find(alias);
    }

    /// Clear all mappings
    void clear() {
        aliases_.clear();
    }

private:
    std::uint16_t max_ = 0;
    std::map<std::uint16_t, std::string> aliases_;
};

} // namespace cnetmod::mqtt
