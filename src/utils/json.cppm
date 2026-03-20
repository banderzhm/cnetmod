export module cnetmod.utils:json;

import std;

export namespace cnetmod::json_utils {

template <typename JsonT>
[[nodiscard]] inline auto parse_object(std::string_view body) -> JsonT {
    if (body.empty()) {
        return JsonT::object();
    }

    auto parsed = JsonT::parse(std::string(body), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return JsonT::object();
    }
    return parsed;
}

template <typename JsonT>
[[nodiscard]] inline auto to_int(const JsonT& j, const char* key, int def = 0) -> int {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_integer()) {
        return def;
    }
    return it->template get<int>();
}

template <typename JsonT>
[[nodiscard]] inline auto to_bool(const JsonT& j, const char* key, bool def = false) -> bool {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_boolean()) {
        return def;
    }
    return it->template get<bool>();
}

template <typename JsonT>
[[nodiscard]] inline auto to_string(const JsonT& j, const char* key, std::string def = {}) -> std::string {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string()) {
        return def;
    }
    return it->template get<std::string>();
}

template <typename JsonT>
[[nodiscard]] inline auto to_uint16_port(const JsonT& j, const char* key, std::uint16_t def) -> std::uint16_t {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_unsigned()) {
        return def;
    }
    const auto value = it->template get<unsigned>();
    if (value == 0U || value > 65535U) {
        return def;
    }
    return static_cast<std::uint16_t>(value);
}

template <typename JsonT>
[[nodiscard]] inline auto to_positive_unsigned(const JsonT& j, const char* key, unsigned def) -> unsigned {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_unsigned()) {
        return def;
    }
    const auto value = it->template get<unsigned>();
    return value == 0U ? def : value;
}

} // namespace cnetmod::json_utils
