/// cnetmod.protocol.mqtt:subscription_map — Trie-based 订阅匹配
/// 高效的 topic filter → subscriber 映射，支持 +/# 通配符匹配

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:subscription_map;

import std;
import :types;

namespace cnetmod::mqtt {

// =============================================================================
// 订阅条目（匹配结果）
// =============================================================================

export struct subscription_entry_ref {
    std::string      client_id;
    subscribe_entry  entry;
};

// =============================================================================
// Subscription Map — Trie 实现
// =============================================================================

/// Trie-based 订阅映射
/// 将 topic filter 按 '/' 分割后存入 trie，匹配时沿 trie 遍历
export class subscription_map {
public:
    subscription_map() = default;

    // 不可复制
    subscription_map(const subscription_map&) = delete;
    auto operator=(const subscription_map&) -> subscription_map& = delete;

    // 可移动
    subscription_map(subscription_map&&) = default;
    auto operator=(subscription_map&&) -> subscription_map& = default;

    /// 插入订阅
    void insert(const std::string& topic_filter, const std::string& client_id,
                const subscribe_entry& entry)
    {
        auto segments = split(topic_filter);
        auto* node = &root_;

        for (auto& seg : segments) {
            auto it = node->children.find(std::string(seg));
            if (it == node->children.end()) {
                auto [new_it, _] = node->children.emplace(
                    std::string(seg), std::make_unique<trie_node>());
                node = new_it->second.get();
            } else {
                node = it->second.get();
            }
        }

        // 在叶节点存储订阅者
        node->subscribers[client_id] = entry;
        ++total_subscriptions_;
    }

    /// 移除订阅
    /// 返回是否成功移除
    auto erase(const std::string& topic_filter, const std::string& client_id) -> bool {
        auto segments = split(topic_filter);
        auto* node = &root_;

        // 先找到目标节点
        std::vector<std::pair<trie_node*, std::string>> path;
        for (auto& seg : segments) {
            path.emplace_back(node, std::string(seg));
            auto it = node->children.find(std::string(seg));
            if (it == node->children.end()) return false;
            node = it->second.get();
        }

        // 移除订阅者
        auto erased = node->subscribers.erase(client_id);
        if (erased == 0) return false;
        --total_subscriptions_;

        // 回溯清理空节点
        for (auto rit = path.rbegin(); rit != path.rend(); ++rit) {
            auto [parent, seg] = *rit;
            auto child_it = parent->children.find(seg);
            if (child_it == parent->children.end()) break;
            auto* child = child_it->second.get();
            if (child->subscribers.empty() && child->children.empty()) {
                parent->children.erase(child_it);
            } else {
                break; // 非空，不再继续清理
            }
        }

        return true;
    }

    /// 移除某个 client 的所有订阅
    void erase_client(const std::string& client_id) {
        erase_client_recursive(&root_, client_id);
    }

    /// 匹配 topic name，返回所有匹配的订阅者
    [[nodiscard]] auto match(std::string_view topic) const
        -> std::vector<subscription_entry_ref>
    {
        std::vector<subscription_entry_ref> result;
        auto segments = split(topic);
        match_recursive(&root_, segments, 0, topic, result);
        return result;
    }

    /// 总订阅数
    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return total_subscriptions_;
    }

    /// 是否为空
    [[nodiscard]] auto empty() const noexcept -> bool {
        return total_subscriptions_ == 0;
    }

    /// 清空
    void clear() {
        root_.children.clear();
        root_.subscribers.clear();
        total_subscriptions_ = 0;
    }

private:
    struct trie_node {
        std::map<std::string, std::unique_ptr<trie_node>> children;
        std::map<std::string, subscribe_entry> subscribers; // client_id → entry
    };

    /// 分割 topic 路径
    static auto split(std::string_view topic) -> std::vector<std::string_view> {
        std::vector<std::string_view> segments;
        std::size_t start = 0;
        while (start <= topic.size()) {
            auto pos = topic.find('/', start);
            if (pos == std::string_view::npos) {
                segments.push_back(topic.substr(start));
                break;
            }
            segments.push_back(topic.substr(start, pos - start));
            start = pos + 1;
            if (start == topic.size()) {
                segments.push_back(std::string_view{});
            }
        }
        return segments;
    }

    /// 递归匹配
    void match_recursive(const trie_node* node,
                         const std::vector<std::string_view>& segments,
                         std::size_t depth,
                         std::string_view original_topic,
                         std::vector<subscription_entry_ref>& result) const
    {
        if (!node) return;

        // 检查 # 通配符子节点
        auto hash_it = node->children.find("#");
        if (hash_it != node->children.end()) {
            auto* hash_node = hash_it->second.get();
            // # 匹配剩余所有层级
            // 但 $ 前缀 topic 不匹配以 # 或 + 开头的 filter
            bool dollar_block = (depth == 0 && !original_topic.empty() &&
                                 original_topic[0] == '$');
            if (!dollar_block) {
                for (auto& [cid, entry] : hash_node->subscribers) {
                    result.push_back({cid, entry});
                }
            }
        }

        if (depth >= segments.size()) {
            // 完全匹配：收集此节点的订阅者
            for (auto& [cid, entry] : node->subscribers) {
                result.push_back({cid, entry});
            }
            return;
        }

        auto& seg = segments[depth];

        // 检查 + 通配符子节点
        auto plus_it = node->children.find("+");
        if (plus_it != node->children.end()) {
            bool dollar_block = (depth == 0 && !original_topic.empty() &&
                                 original_topic[0] == '$');
            if (!dollar_block) {
                match_recursive(plus_it->second.get(), segments, depth + 1,
                               original_topic, result);
            }
        }

        // 精确匹配子节点
        auto exact_it = node->children.find(std::string(seg));
        if (exact_it != node->children.end()) {
            match_recursive(exact_it->second.get(), segments, depth + 1,
                           original_topic, result);
        }
    }

    /// 递归移除 client 的所有订阅
    void erase_client_recursive(trie_node* node, const std::string& client_id) {
        auto erased = node->subscribers.erase(client_id);
        if (erased > 0) --total_subscriptions_;

        for (auto it = node->children.begin(); it != node->children.end(); ) {
            erase_client_recursive(it->second.get(), client_id);
            if (it->second->subscribers.empty() && it->second->children.empty()) {
                it = node->children.erase(it);
            } else {
                ++it;
            }
        }
    }

    trie_node root_;
    std::size_t total_subscriptions_ = 0;
};

} // namespace cnetmod::mqtt
