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
    std::string share_name;     // Shared group name (empty means non-shared subscription)
    std::string topic_filter;   // Actual topic filter
};

/// Parse shared subscription topic filter
/// Input: "$share/group1/sensor/+/data" → share_name="group1", topic_filter="sensor/+/data"
/// Input: "sensor/+/data"              → share_name="", topic_filter="sensor/+/data"
/// Returns nullopt on format error
export inline auto parse_shared_subscription(std::string_view whole_filter)
    -> std::optional<share_name_topic_filter>
{
    constexpr std::string_view prefix = "$share/";

    if (!whole_filter.starts_with(prefix)) {
        // Non-shared subscription
        return share_name_topic_filter{{}, std::string(whole_filter)};
    }

    // Remove $share/
    auto rest = whole_filter.substr(prefix.size());

    // Find '/' separating share_name and topic_filter
    auto slash_pos = rest.find('/');
    if (slash_pos == std::string_view::npos) {
        return std::nullopt; // Format error: no / after $share/group
    }

    auto share_name = rest.substr(0, slash_pos);
    auto topic_filter = rest.substr(slash_pos + 1);

    // Both share_name and topic_filter cannot be empty
    if (share_name.empty() || topic_filter.empty()) {
        return std::nullopt;
    }

    return share_name_topic_filter{
        std::string(share_name),
        std::string(topic_filter)
    };
}

/// Check if topic filter is a shared subscription
export inline auto is_shared_subscription(std::string_view filter) noexcept -> bool {
    return filter.starts_with("$share/");
}

/// Extract actual topic filter from shared subscription (without $share/group/ prefix)
/// Non-shared subscription returned as-is
export inline auto extract_topic_filter(std::string_view filter) -> std::string {
    auto parsed = parse_shared_subscription(filter);
    if (parsed) return std::move(parsed->topic_filter);
    return std::string(filter);
}

// =============================================================================
// Shared Target Store — Shared subscription group management and round-robin delivery
// =============================================================================

/// Shared subscription member info
export struct shared_member {
    std::string client_id;
    std::string topic_filter;  // Actual filter (without $share/.../ prefix)
};

/// Shared subscription group manager
/// Maintains member list by (share_name, topic_filter), round-robin selects delivery target
export class shared_target_store {
public:
    shared_target_store() = default;

    // Non-copyable
    shared_target_store(const shared_target_store&) = delete;
    auto operator=(const shared_target_store&) -> shared_target_store& = delete;

    // Movable
    shared_target_store(shared_target_store&&) = default;
    auto operator=(shared_target_store&&) -> shared_target_store& = default;

    /// Composite key: share_name + "/" + topic_filter
    static auto make_key(std::string_view share_name, std::string_view filter) -> std::string {
        std::string key;
        key.reserve(share_name.size() + 1 + filter.size());
        key.append(share_name);
        key.push_back('/');
        key.append(filter);
        return key;
    }

    /// Add member to shared group
    void add_member(std::string_view share_name, std::string_view filter,
                    const std::string& client_id)
    {
        auto key = make_key(share_name, filter);
        auto& group = groups_[key];

        // Avoid duplicates
        for (auto& m : group.members) {
            if (m.client_id == client_id) return;
        }

        group.members.push_back(shared_member{client_id, std::string(filter)});
    }

    /// Remove member from shared group
    void remove_member(std::string_view share_name, std::string_view filter,
                       const std::string& client_id)
    {
        auto key = make_key(share_name, filter);
        auto it = groups_.find(key);
        if (it == groups_.end()) return;

        auto& group = it->second;
        std::erase_if(group.members, [&](const shared_member& m) {
            return m.client_id == client_id;
        });

        // Remove empty group
        if (group.members.empty()) {
            groups_.erase(it);
        }
    }

    /// Remove client membership from all shared groups
    void remove_client(const std::string& client_id) {
        for (auto it = groups_.begin(); it != groups_.end(); ) {
            auto& group = it->second;
            std::erase_if(group.members, [&](const shared_member& m) {
                return m.client_id == client_id;
            });
            if (group.members.empty()) {
                it = groups_.erase(it);
            } else {
                ++it;
            }
        }
    }

    /// Round-robin select delivery target
    /// Returns selected client_id, empty means no available members
    [[nodiscard]] auto select_target(std::string_view share_name,
                                      std::string_view filter)
        -> std::string
    {
        auto key = make_key(share_name, filter);
        auto it = groups_.find(key);
        if (it == groups_.end() || it->second.members.empty()) {
            return {};
        }

        auto& group = it->second;
        auto idx = group.rr_index % group.members.size();
        group.rr_index++;
        return group.members[idx].client_id;
    }

    /// Select delivery target (prefer online)
    /// online_check: callback to check if client_id is online
    template <typename Fn>
    [[nodiscard]] auto select_online_target(std::string_view share_name,
                                             std::string_view filter,
                                             Fn&& online_check) -> std::string
    {
        auto key = make_key(share_name, filter);
        auto it = groups_.find(key);
        if (it == groups_.end() || it->second.members.empty()) {
            return {};
        }

        auto& group = it->second;
        auto n = group.members.size();

        // Loop from rr_index to find online member
        for (std::size_t attempt = 0; attempt < n; ++attempt) {
            auto idx = (group.rr_index + attempt) % n;
            auto& cid = group.members[idx].client_id;
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
    auto find_matching_groups(std::string_view topic,
                               MatchFn&& match_fn,
                               OnlineFn&& online_fn)
        -> std::vector<std::pair<std::string, std::string>> // (client_id, filter)
    {
        std::vector<std::pair<std::string, std::string>> result;

        for (auto& [key, group] : groups_) {
            if (group.members.empty()) continue;

            // Extract filter from key (key = share_name/filter)
            auto& first_member = group.members[0];
            auto& filter = first_member.topic_filter;

            if (!match_fn(filter, topic)) continue;

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
    [[nodiscard]] auto group_count() const noexcept -> std::size_t {
        return groups_.size();
    }

    /// Clear all
    void clear() { groups_.clear(); }

private:
    struct group_state {
        std::vector<shared_member> members;
        std::size_t rr_index = 0;
    };

    std::map<std::string, group_state> groups_;
};

} // namespace cnetmod::mqtt
