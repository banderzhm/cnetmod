/// cnetmod.protocol.mqtt:shared_sub — MQTT v5 Shared Subscriptions
/// 解析 $share/{group}/{filter} 格式，round-robin 投递

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:shared_sub;

import std;

namespace cnetmod::mqtt {

// =============================================================================
// Shared Subscription 解析
// =============================================================================

/// $share/{group}/{filter} 解析结果
export struct share_name_topic_filter {
    std::string share_name;     // 共享组名（空表示非共享订阅）
    std::string topic_filter;   // 实际 topic filter
};

/// 解析共享订阅的 topic filter
/// 输入: "$share/group1/sensor/+/data" → share_name="group1", topic_filter="sensor/+/data"
/// 输入: "sensor/+/data"              → share_name="", topic_filter="sensor/+/data"
/// 格式错误返回 nullopt
export inline auto parse_shared_subscription(std::string_view whole_filter)
    -> std::optional<share_name_topic_filter>
{
    constexpr std::string_view prefix = "$share/";

    if (!whole_filter.starts_with(prefix)) {
        // 非共享订阅
        return share_name_topic_filter{{}, std::string(whole_filter)};
    }

    // 移除 $share/
    auto rest = whole_filter.substr(prefix.size());

    // 查找分隔 share_name 和 topic_filter 的 '/'
    auto slash_pos = rest.find('/');
    if (slash_pos == std::string_view::npos) {
        return std::nullopt; // 格式错误：$share/group 后没有 /
    }

    auto share_name = rest.substr(0, slash_pos);
    auto topic_filter = rest.substr(slash_pos + 1);

    // share_name 和 topic_filter 都不能为空
    if (share_name.empty() || topic_filter.empty()) {
        return std::nullopt;
    }

    return share_name_topic_filter{
        std::string(share_name),
        std::string(topic_filter)
    };
}

/// 判断 topic filter 是否为共享订阅
export inline auto is_shared_subscription(std::string_view filter) noexcept -> bool {
    return filter.starts_with("$share/");
}

/// 从共享订阅提取实际 topic filter（不含 $share/group/ 前缀）
/// 非共享订阅原样返回
export inline auto extract_topic_filter(std::string_view filter) -> std::string {
    auto parsed = parse_shared_subscription(filter);
    if (parsed) return std::move(parsed->topic_filter);
    return std::string(filter);
}

// =============================================================================
// Shared Target Store — 共享订阅组管理与 round-robin 投递
// =============================================================================

/// 共享订阅成员信息
export struct shared_member {
    std::string client_id;
    std::string topic_filter;  // 实际 filter（不含 $share/.../ 前缀）
};

/// 共享订阅组管理器
/// 按 (share_name, topic_filter) 维护成员列表，round-robin 选择投递目标
export class shared_target_store {
public:
    shared_target_store() = default;

    // 不可复制
    shared_target_store(const shared_target_store&) = delete;
    auto operator=(const shared_target_store&) -> shared_target_store& = delete;

    // 可移动
    shared_target_store(shared_target_store&&) = default;
    auto operator=(shared_target_store&&) -> shared_target_store& = default;

    /// 组合键: share_name + "/" + topic_filter
    static auto make_key(std::string_view share_name, std::string_view filter) -> std::string {
        std::string key;
        key.reserve(share_name.size() + 1 + filter.size());
        key.append(share_name);
        key.push_back('/');
        key.append(filter);
        return key;
    }

    /// 添加成员到共享组
    void add_member(std::string_view share_name, std::string_view filter,
                    const std::string& client_id)
    {
        auto key = make_key(share_name, filter);
        auto& group = groups_[key];

        // 避免重复
        for (auto& m : group.members) {
            if (m.client_id == client_id) return;
        }

        group.members.push_back(shared_member{client_id, std::string(filter)});
    }

    /// 从共享组移除成员
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

        // 空组移除
        if (group.members.empty()) {
            groups_.erase(it);
        }
    }

    /// 移除客户端在所有共享组中的成员身份
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

    /// round-robin 选择投递目标
    /// 返回选中的 client_id，空表示无可用成员
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

    /// 选择投递目标（优先在线的）
    /// online_check: 判断 client_id 是否在线的回调
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

        // 从 rr_index 开始循环查找在线成员
        for (std::size_t attempt = 0; attempt < n; ++attempt) {
            auto idx = (group.rr_index + attempt) % n;
            auto& cid = group.members[idx].client_id;
            if (online_check(cid)) {
                group.rr_index = idx + 1;
                return cid;
            }
        }

        // 无在线成员，选第一个（离线投递）
        auto idx = group.rr_index % n;
        group.rr_index++;
        return group.members[idx].client_id;
    }

    /// 查找匹配给定 topic 的所有共享组
    /// 返回: [(share_name, filter, selected_client_id), ...]
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

            // 从 key 提取 filter（key = share_name/filter）
            auto& first_member = group.members[0];
            auto& filter = first_member.topic_filter;

            if (!match_fn(filter, topic)) continue;

            // 选择一个成员投递
            auto slash_pos = key.find('/');
            auto share_name = std::string_view(key).substr(0, slash_pos);
            auto cid = select_online_target(share_name, filter, online_fn);
            if (!cid.empty()) {
                result.emplace_back(std::move(cid), filter);
            }
        }

        return result;
    }

    /// 组数
    [[nodiscard]] auto group_count() const noexcept -> std::size_t {
        return groups_.size();
    }

    /// 清空
    void clear() { groups_.clear(); }

private:
    struct group_state {
        std::vector<shared_member> members;
        std::size_t rr_index = 0;
    };

    std::map<std::string, group_state> groups_;
};

} // namespace cnetmod::mqtt
