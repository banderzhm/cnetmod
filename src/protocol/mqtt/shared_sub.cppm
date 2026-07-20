/// cnetmod.protocol.mqtt:shared_sub — MQTT v5 Shared Subscriptions
/// Parse $share/{group}/{filter} format, round-robin delivery

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:shared_sub;

import std;

namespace cnetmod::mqtt {

// =============================================================================
// Shared Subscription parsing
// =============================================================================

/// $share/{group}/{filter} parse result
export struct share_name_topic_filter {
  std::string
      share_name; // Shared group name (empty means non-shared subscription)
  std::string topic_filter; // Actual topic filter
};

/// Parse shared subscription topic filter
/// Input: "$share/group1/sensor/+/data" → share_name="group1",
/// topic_filter="sensor/+/data" Input: "sensor/+/data"              →
/// share_name="", topic_filter="sensor/+/data" Returns nullopt on format error
export auto parse_shared_subscription(std::string_view whole_filter)
    -> std::optional<share_name_topic_filter>;

/// Check if topic filter is a shared subscription
export auto is_shared_subscription(std::string_view filter) noexcept -> bool;

/// Extract actual topic filter from shared subscription (without $share/group/
/// prefix) Non-shared subscription returned as-is
export auto extract_topic_filter(std::string_view filter) -> std::string;

// =============================================================================
// Shared Target Store — Shared subscription group management and round-robin
// delivery
// =============================================================================

/// Shared subscription member info
export struct shared_member {
  std::string client_id;
  std::string topic_filter; // Actual filter (without $share/.../ prefix)
};

/// Shared subscription group manager
/// Maintains member list by (share_name, topic_filter), round-robin selects
/// delivery target
export class shared_target_store {
public:
  shared_target_store() = default;

  // Non-copyable
  shared_target_store(const shared_target_store &) = delete;
  auto operator=(const shared_target_store &) -> shared_target_store & = delete;

  // Movable
  shared_target_store(shared_target_store &&) = default;
  auto operator=(shared_target_store &&) -> shared_target_store & = default;

  /// Composite key: share_name + "/" + topic_filter
  static auto make_key(std::string_view share_name, std::string_view filter)
      -> std::string;

  /// Add member to shared group
  void add_member(std::string_view share_name, std::string_view filter,
                  const std::string &client_id);

  /// Remove member from shared group
  void remove_member(std::string_view share_name, std::string_view filter,
                     const std::string &client_id);

  /// Remove client membership from all shared groups
  void remove_client(const std::string &client_id);

  /// Round-robin select delivery target
  /// Returns selected client_id, empty means no available members
  [[nodiscard]] auto select_target(std::string_view share_name,
                                   std::string_view filter) -> std::string;

  /// Select delivery target (prefer online)
  /// online_check: callback to check if client_id is online
  template <typename Fn>
  [[nodiscard]] auto select_online_target(std::string_view share_name,
                                          std::string_view filter,
                                          Fn &&online_check) -> std::string {
    auto key = make_key(share_name, filter);
    auto it = groups_.find(key);
    if (it == groups_.end() || it->second.members.empty()) {
      return {};
    }

    auto &group = it->second;
    auto n = group.members.size();

    // Loop from rr_index to find online member
    for (std::size_t attempt = 0; attempt < n; ++attempt) {
      auto idx = (group.rr_index + attempt) % n;
      auto &cid = group.members[idx].client_id;
      if (online_check(cid)) {
        group.rr_index = idx + 1;
        return cid;
      }
    }

    // No online members, select first (offline delivery)
    auto idx = group.rr_index % n;
    group.rr_index++;
    return group.members[idx].client_id;
  }

  /// Find all shared groups matching given topic
  /// Returns: [(share_name, filter, selected_client_id), ...]
  /// topic_match_fn: (filter, topic) -> bool
  template <typename MatchFn, typename OnlineFn>
  auto find_matching_groups(std::string_view topic, MatchFn &&match_fn,
                            OnlineFn &&online_fn)
      -> std::vector<std::pair<std::string, std::string>> // (client_id, filter)
  {
    std::vector<std::pair<std::string, std::string>> result;

    for (auto &[key, group] : groups_) {
      if (group.members.empty())
        continue;

      // Extract filter from key (key = share_name/filter)
      auto &first_member = group.members[0];
      auto &filter = first_member.topic_filter;

      if (!match_fn(filter, topic))
        continue;

      // Select a member for delivery
      auto slash_pos = key.find('/');
      auto share_name = std::string_view(key).substr(0, slash_pos);
      auto cid = select_online_target(share_name, filter, online_fn);
      if (!cid.empty()) {
        result.emplace_back(std::move(cid), filter);
      }
    }

    return result;
  }

  /// Group count
  [[nodiscard]] auto group_count() const noexcept -> std::size_t;

  /// Clear all
  void clear();

private:
  struct group_state {
    std::vector<shared_member> members;
    std::size_t rr_index = 0;
  };

  std::unordered_map<std::string, group_state> groups_;
};

} // namespace cnetmod::mqtt
