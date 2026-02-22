/// cnetmod.protocol.mqtt:topic_filter — MQTT Topic filter validation and matching
/// Supports +/# wildcards, follows MQTT v3.1.1 / v5.0 spec

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:topic_filter;

import std;

namespace cnetmod::mqtt {

// =============================================================================
// Topic Filter validation
// =============================================================================

/// Validate if topic filter is legal
/// Rules from https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html#_Toc3901247
export constexpr auto validate_topic_filter(std::string_view filter) noexcept -> bool {
    // At least 1 byte, at most 65535 bytes
    if (filter.empty() || filter.size() > 65535) return false;

    for (std::size_t i = 0; i < filter.size(); ++i) {
        auto ch = filter[i];
        // Null character not allowed
        if (ch == '\0') return false;

        if (ch == '+') {
            // + must occupy a level alone: must be preceded by / or at the beginning
            if (i > 0 && filter[i - 1] != '/') return false;
            // Must be followed by / or at the end
            if (i + 1 < filter.size() && filter[i + 1] != '/') return false;
        }

        if (ch == '#') {
            // # must be the last character
            if (i != filter.size() - 1) return false;
            // Must be preceded by / or at the beginning
            if (i > 0 && filter[i - 1] != '/') return false;
        }
    }
    return true;
}

/// Validate if topic name is legal (wildcards + # not allowed)
export constexpr auto validate_topic_name(std::string_view name) noexcept -> bool {
    if (name.empty() || name.size() > 65535) return false;
    for (auto ch : name) {
        if (ch == '\0' || ch == '+' || ch == '#') return false;
    }
    return true;
}

// =============================================================================
// Topic matching
// =============================================================================

namespace detail {

/// Split topic path into level segments
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
        // Trailing / produces an empty segment
        if (start == topic.size()) {
            segments.push_back(std::string_view{});
        }
    }
    return segments;
}

} // namespace detail

/// Check if topic_name matches topic_filter (with +/# wildcards)
/// Prerequisite: filter and name have passed their respective validate checks
export inline auto topic_matches(std::string_view filter, std::string_view name) noexcept -> bool {
    // $ prefix special handling:
    // Wildcard # or + at the beginning of filter does not match $ prefix topic
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
            // # matches all remaining levels (including 0)
            return true;
        }

        if (ni >= name_segs.size()) {
            // name segments exhausted, but filter still has non-# segments
            return false;
        }

        if (fs == "+") {
            // + matches any content at current level
            ++fi;
            ++ni;
            continue;
        }

        // Exact match
        if (fs != name_segs[ni]) {
            return false;
        }

        ++fi;
        ++ni;
    }

    // When filter segments are exhausted, name segments must also be exhausted
    return ni == name_segs.size();
}

// =============================================================================
// Helper: check if filter contains wildcards
// =============================================================================

export constexpr auto has_wildcards(std::string_view filter) noexcept -> bool {
    for (auto ch : filter) {
        if (ch == '+' || ch == '#') return true;
    }
    return false;
}

} // namespace cnetmod::mqtt
