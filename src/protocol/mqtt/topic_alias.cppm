/// cnetmod.protocol.mqtt:topic_alias — MQTT v5 Topic Alias 管理
/// 发送端 LRU 自动分配 alias，接收端根据 alias 还原 topic

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:topic_alias;

import std;
import :types;

namespace cnetmod::mqtt {

// =============================================================================
// Topic Alias Send — 发送端 LRU alias 管理
// =============================================================================

/// 发送端 topic alias 管理器
/// 维护 topic ↔ alias 映射，使用 LRU 策略淘汰最久未用的 alias
export class topic_alias_send {
public:
    explicit topic_alias_send(std::uint16_t max_alias = 0) noexcept
        : max_(max_alias) {}

    /// 设置最大 alias 数量（从 CONNACK topic_alias_maximum 属性获取）
    void set_max(std::uint16_t max_alias) noexcept {
        max_ = max_alias;
        clear();
    }

    /// 是否启用（max > 0）
    [[nodiscard]] auto enabled() const noexcept -> bool { return max_ > 0; }

    /// 最大 alias 数
    [[nodiscard]] auto max() const noexcept -> std::uint16_t { return max_; }

    /// 通过 alias 查找 topic
    [[nodiscard]] auto find_by_alias(std::uint16_t alias) const -> std::string {
        auto it = alias_to_topic_.find(alias);
        if (it != alias_to_topic_.end()) return it->second;
        return {};
    }

    /// 通过 topic 查找已分配的 alias
    [[nodiscard]] auto find_by_topic(std::string_view topic) const
        -> std::optional<std::uint16_t>
    {
        auto it = topic_to_alias_.find(std::string(topic));
        if (it != topic_to_alias_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /// 插入或更新映射，并刷新 LRU 时间戳
    void insert_or_update(std::string_view topic, std::uint16_t alias) {
        if (alias == 0 || alias > max_) return;

        auto alias_it = alias_to_topic_.find(alias);
        if (alias_it != alias_to_topic_.end()) {
            // alias 已存在，先清除旧 topic → alias 映射
            topic_to_alias_.erase(alias_it->second);
            alias_it->second = std::string(topic);
        } else {
            alias_to_topic_[alias] = std::string(topic);
        }
        topic_to_alias_[std::string(topic)] = alias;
        lru_time_[alias] = ++lru_clock_;
    }

    /// 获取 LRU（最久未使用）alias，用于新 topic 复用
    /// 如果还有空闲 alias，优先返回空闲的
    [[nodiscard]] auto get_lru_alias() const -> std::uint16_t {
        if (max_ == 0) return 0;

        // 查找未使用的 alias
        for (std::uint16_t a = 1; a <= max_; ++a) {
            if (alias_to_topic_.find(a) == alias_to_topic_.end()) {
                return a;
            }
        }

        // 所有 alias 都在用，返回 LRU 最久的
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

    /// 为 topic 分配或复用 alias
    /// 返回 (alias, is_new) — is_new=true 时需要在 PUBLISH 中同时发送 topic 和 alias
    auto allocate(std::string_view topic) -> std::pair<std::uint16_t, bool> {
        if (!enabled()) return {0, false};

        // 已有映射，直接复用
        auto existing = find_by_topic(topic);
        if (existing) {
            lru_time_[*existing] = ++lru_clock_;
            return {*existing, false};
        }

        // 分配新 alias
        auto alias = get_lru_alias();
        insert_or_update(topic, alias);
        return {alias, true};
    }

    /// 清空所有映射
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
// Topic Alias Recv — 接收端 alias 解析
// =============================================================================

/// 接收端 topic alias 管理器
/// 根据发送方的 alias 还原 topic
export class topic_alias_recv {
public:
    explicit topic_alias_recv(std::uint16_t max_alias = 0) noexcept
        : max_(max_alias) {}

    /// 设置最大 alias 数量（我方通告给对端的 topic_alias_maximum）
    void set_max(std::uint16_t max_alias) noexcept {
        max_ = max_alias;
        clear();
    }

    /// 是否启用
    [[nodiscard]] auto enabled() const noexcept -> bool { return max_ > 0; }

    /// 最大 alias 数
    [[nodiscard]] auto max() const noexcept -> std::uint16_t { return max_; }

    /// 插入或更新映射（收到 PUBLISH 中同时包含 topic 和 alias 时）
    void insert_or_update(std::string_view topic, std::uint16_t alias) {
        if (alias == 0 || alias > max_) return;
        aliases_[alias] = std::string(topic);
    }

    /// 根据 alias 查找 topic
    [[nodiscard]] auto find(std::uint16_t alias) const -> std::string {
        auto it = aliases_.find(alias);
        if (it != aliases_.end()) return it->second;
        return {};
    }

    /// 处理收到的 PUBLISH 的 topic alias
    /// 输入: topic (可能为空), alias (从 property 取)
    /// 输出: 实际 topic；如果无法解析返回空
    [[nodiscard]] auto resolve(std::string_view topic, std::uint16_t alias)
        -> std::string
    {
        if (alias == 0) {
            // 未使用 alias，直接返回 topic
            return std::string(topic);
        }

        if (!topic.empty()) {
            // topic 和 alias 都有：建立映射，返回 topic
            insert_or_update(topic, alias);
            return std::string(topic);
        }

        // topic 为空，alias 非零：从映射查找
        return find(alias);
    }

    /// 清空所有映射
    void clear() {
        aliases_.clear();
    }

private:
    std::uint16_t max_ = 0;
    std::map<std::uint16_t, std::string> aliases_;
};

} // namespace cnetmod::mqtt
