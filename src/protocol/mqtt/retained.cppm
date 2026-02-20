/// cnetmod.protocol.mqtt:retained — MQTT 保留消息存储
/// 管理 retained messages 的存储、匹配和删除

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
    std::string  topic;
    std::string  payload;
    qos          qos_value = qos::at_most_once;
    properties   props;    // v5
};

// =============================================================================
// Retained Store — Trie-based 保留消息管理
// =============================================================================

export class retained_store {
public:
    retained_store() = default;

    // 不可复制
    retained_store(const retained_store&) = delete;
    auto operator=(const retained_store&) -> retained_store& = delete;

    // 可移动
    retained_store(retained_store&&) = default;
    auto operator=(retained_store&&) -> retained_store& = default;

    /// 存储保留消息
    /// 如果 payload 为空，则删除该 topic 的保留消息 (MQTT 规范)
    void store(const std::string& topic, retained_message msg) {
        if (msg.payload.empty()) {
            // 空 payload 表示删除保留消息
            remove(topic);
        } else {
            // 同时存入 flat map（供持久化/遍历）和 trie（供匹配）
            messages_.insert_or_assign(topic, msg);
            trie_insert(topic, std::move(msg));
        }
    }

    /// 根据 topic filter 匹配保留消息（支持 +/# 通配符）
    /// 使用 trie 加速匹配
    [[nodiscard]] auto match(std::string_view topic_filter_str) const
        -> std::vector<retained_message>
    {
        std::vector<retained_message> result;
        auto segments = split(topic_filter_str);
        trie_match(&root_, segments, 0, topic_filter_str, result);
        return result;
    }

    /// 删除指定 topic 的保留消息
    auto remove(const std::string& topic) -> bool {
        auto erased = messages_.erase(topic);
        if (erased > 0) {
            trie_erase(topic);
            return true;
        }
        return false;
    }

    /// 获取指定 topic 的保留消息
    [[nodiscard]] auto find(const std::string& topic) const
        -> const retained_message*
    {
        auto it = messages_.find(topic);
        if (it != messages_.end()) return &it->second;
        return nullptr;
    }

    /// 保留消息总数
    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return messages_.size();
    }

    /// 清空所有保留消息
    void clear() {
        messages_.clear();
        root_.children.clear();
        root_.message.reset();
    }

    /// 遍历所有保留消息
    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (auto& [topic, msg] : messages_) {
            fn(msg);
        }
    }

    /// 获取底层 map 引用（供持久化使用）
    [[nodiscard]] auto messages() noexcept
        -> std::map<std::string, retained_message>& { return messages_; }
    [[nodiscard]] auto messages() const noexcept
        -> const std::map<std::string, retained_message>& { return messages_; }

private:
    struct trie_node {
        std::map<std::string, std::unique_ptr<trie_node>> children;
        std::optional<retained_message> message;
    };

    static auto split(std::string_view s) -> std::vector<std::string_view> {
        std::vector<std::string_view> segs;
        std::size_t start = 0;
        while (start <= s.size()) {
            auto pos = s.find('/', start);
            if (pos == std::string_view::npos) {
                segs.push_back(s.substr(start));
                break;
            }
            segs.push_back(s.substr(start, pos - start));
            start = pos + 1;
            if (start == s.size()) segs.push_back(std::string_view{});
        }
        return segs;
    }

    void trie_insert(const std::string& topic, retained_message msg) {
        auto segs = split(topic);
        auto* node = &root_;
        for (auto& seg : segs) {
            auto it = node->children.find(std::string(seg));
            if (it == node->children.end()) {
                auto [ni, _] = node->children.emplace(
                    std::string(seg), std::make_unique<trie_node>());
                node = ni->second.get();
            } else {
                node = it->second.get();
            }
        }
        node->message = std::move(msg);
    }

    void trie_erase(const std::string& topic) {
        auto segs = split(topic);
        auto* node = &root_;
        std::vector<std::pair<trie_node*, std::string>> path;
        for (auto& seg : segs) {
            path.emplace_back(node, std::string(seg));
            auto it = node->children.find(std::string(seg));
            if (it == node->children.end()) return;
            node = it->second.get();
        }
        node->message.reset();
        // 回溯清理空节点
        for (auto rit = path.rbegin(); rit != path.rend(); ++rit) {
            auto [parent, seg] = *rit;
            auto cit = parent->children.find(seg);
            if (cit == parent->children.end()) break;
            auto* child = cit->second.get();
            if (!child->message && child->children.empty()) {
                parent->children.erase(cit);
            } else {
                break;
            }
        }
    }

    /// trie 匹配: filter 中的 +/# 用来遍历 trie（存储的是 topic name）
    void trie_match(const trie_node* node,
                    const std::vector<std::string_view>& filter_segs,
                    std::size_t depth,
                    std::string_view original_filter,
                    std::vector<retained_message>& result) const
    {
        if (!node) return;

        if (depth >= filter_segs.size()) {
            // 完全匹配
            if (node->message) {
                result.push_back(*node->message);
            }
            return;
        }

        auto& seg = filter_segs[depth];

        if (seg == "#") {
            // # 匹配当前节点及所有后代
            collect_all(node, result);
            return;
        }

        if (seg == "+") {
            // + 匹配单层：遍历所有子节点
            for (auto& [child_seg, child] : node->children) {
                // $ 前缀保护
                if (depth == 0 && !child_seg.empty() && child_seg[0] == '$')
                    continue;
                trie_match(child.get(), filter_segs, depth + 1,
                          original_filter, result);
            }
            return;
        }

        // 精确匹配
        auto it = node->children.find(std::string(seg));
        if (it != node->children.end()) {
            trie_match(it->second.get(), filter_segs, depth + 1,
                      original_filter, result);
        }
    }

    /// 收集节点及所有后代的消息
    void collect_all(const trie_node* node,
                     std::vector<retained_message>& result) const
    {
        if (!node) return;
        if (node->message) {
            result.push_back(*node->message);
        }
        for (auto& [seg, child] : node->children) {
            collect_all(child.get(), result);
        }
    }

    std::map<std::string, retained_message> messages_; // flat map for persistence
    trie_node root_;                                    // trie for matching
};

} // namespace cnetmod::mqtt
