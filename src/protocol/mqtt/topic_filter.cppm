/// cnetmod.protocol.mqtt:topic_filter — MQTT Topic 过滤器验证与匹配
/// 支持 +/# 通配符，遵循 MQTT v3.1.1 / v5.0 规范

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:topic_filter;

import std;

namespace cnetmod::mqtt {

// =============================================================================
// Topic Filter 验证
// =============================================================================

/// 验证 topic filter 是否合法
/// 规则来自 https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html#_Toc3901247
export constexpr auto validate_topic_filter(std::string_view filter) noexcept -> bool {
    // 至少 1 字节, 最多 65535 字节
    if (filter.empty() || filter.size() > 65535) return false;

    for (std::size_t i = 0; i < filter.size(); ++i) {
        auto ch = filter[i];
        // 不允许 null 字符
        if (ch == '\0') return false;

        if (ch == '+') {
            // + 必须独占一个层级：前面必须是 / 或位于开头
            if (i > 0 && filter[i - 1] != '/') return false;
            // 后面必须是 / 或位于末尾
            if (i + 1 < filter.size() && filter[i + 1] != '/') return false;
        }

        if (ch == '#') {
            // # 必须是最后一个字符
            if (i != filter.size() - 1) return false;
            // 前面必须是 / 或位于开头
            if (i > 0 && filter[i - 1] != '/') return false;
        }
    }
    return true;
}

/// 验证 topic name 是否合法（不允许通配符 + #）
export constexpr auto validate_topic_name(std::string_view name) noexcept -> bool {
    if (name.empty() || name.size() > 65535) return false;
    for (auto ch : name) {
        if (ch == '\0' || ch == '+' || ch == '#') return false;
    }
    return true;
}

// =============================================================================
// Topic 匹配
// =============================================================================

namespace detail {

/// 分割 topic 路径为层级段
inline auto split_topic(std::string_view topic) -> std::vector<std::string_view> {
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
        // 末尾 / 产生一个空段
        if (start == topic.size()) {
            segments.push_back(std::string_view{});
        }
    }
    return segments;
}

} // namespace detail

/// 检查 topic_name 是否匹配 topic_filter（含 +/# 通配符）
/// 前提：filter 和 name 已通过各自的 validate 验证
export inline auto topic_matches(std::string_view filter, std::string_view name) noexcept -> bool {
    // $ 前缀特殊处理：
    // 通配符 # 或 + 开头的 filter 不匹配 $ 开头的 topic
    if (!name.empty() && name[0] == '$') {
        if (!filter.empty() && (filter[0] == '#' || filter[0] == '+')) {
            return false;
        }
    }

    auto filter_segs = detail::split_topic(filter);
    auto name_segs   = detail::split_topic(name);

    std::size_t fi = 0, ni = 0;

    while (fi < filter_segs.size()) {
        auto& fs = filter_segs[fi];

        if (fs == "#") {
            // # 匹配剩余所有层级 (包括 0 个)
            return true;
        }

        if (ni >= name_segs.size()) {
            // name 段用完了，但 filter 还有非 # 段
            return false;
        }

        if (fs == "+") {
            // + 匹配当前层级任意内容
            ++fi;
            ++ni;
            continue;
        }

        // 精确匹配
        if (fs != name_segs[ni]) {
            return false;
        }

        ++fi;
        ++ni;
    }

    // filter 段用完时，name 段也必须用完
    return ni == name_segs.size();
}

// =============================================================================
// 辅助：检查 filter 是否包含通配符
// =============================================================================

export constexpr auto has_wildcards(std::string_view filter) noexcept -> bool {
    for (auto ch : filter) {
        if (ch == '+' || ch == '#') return true;
    }
    return false;
}

} // namespace cnetmod::mqtt
