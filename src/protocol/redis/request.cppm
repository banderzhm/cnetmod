module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.redis:request;

import std;
import :types;

namespace cnetmod::redis {

// =============================================================================
// RESP3 Serialization Base Functions
// =============================================================================

namespace detail {

/// Append CRLF separator
inline void add_separator(std::string& payload) {
    payload.append("\r\n");
}

/// Append aggregate header: *N\r\n, >N\r\n, %N\r\n, ...
inline void add_header(std::string& payload, resp3_type t, std::size_t size) {
    payload += to_code(t);
    std::format_to(std::back_inserter(payload), "{}", size);
    add_separator(payload);
}

/// Append bulk string: $N\r\nDATA\r\n
inline void add_bulk(std::string& payload, std::string_view data) {
    payload += '$';
    std::format_to(std::back_inserter(payload), "{}", data.size());
    add_separator(payload);
    payload.append(data);
    add_separator(payload);
}

/// Integer → string then append bulk
template <class T>
    requires std::is_integral_v<T>
inline void add_bulk_int(std::string& payload, T n) {
    auto s = std::to_string(n);
    add_bulk(payload, s);
}

/// Float → string then append bulk
template <class T>
    requires std::is_floating_point_v<T>
inline void add_bulk_float(std::string& payload, T n) {
    auto s = std::to_string(n);
    add_bulk(payload, s);
}

/// Generic serialize_one: choose serialization method based on type
inline void serialize_one(std::string& payload, std::string_view sv) {
    add_bulk(payload, sv);
}

inline void serialize_one(std::string& payload, const std::string& s) {
    add_bulk(payload, s);
}

inline void serialize_one(std::string& payload, const char* s) {
    add_bulk(payload, std::string_view(s));
}

template <class T>
    requires std::is_integral_v<T> && (!std::is_same_v<T, bool>) && (!std::is_same_v<T, char>)
void serialize_one(std::string& payload, T n) {
    add_bulk_int(payload, n);
}

template <class T>
    requires std::is_floating_point_v<T>
void serialize_one(std::string& payload, T n) {
    add_bulk_float(payload, n);
}

/// Calculate bulk count contributed by a single value
template <class T>
constexpr auto bulk_count() -> std::size_t {
    return 1;
}

} // namespace detail

// =============================================================================
// request class
// =============================================================================

/// Redis request (pipeline)
/// Composed of one or more Redis commands, encoded using RESP3 array of bulk strings
///
/// Usage:
///   request req;
///   req.push("SET", "key", "value");
///   req.push("GET", "key");
///   req.push("HSET", "hash", "f1", 100, "f2", 200);
export class request {
public:
    request() = default;

    /// Append a command (variadic)
    template <class... Ts>
    void push(std::string_view cmd, Ts const&... args) {
        constexpr auto pack_size = sizeof...(Ts);
        detail::add_header(payload_, resp3_type::array, 1 + pack_size);
        detail::add_bulk(payload_, cmd);
        if constexpr (pack_size > 0) {
            (detail::serialize_one(payload_, args), ...);
        }
        ++commands_;
    }

    /// Append a command: cmd key [range elements...]
    template <class ForwardIterator>
    void push_range(std::string_view cmd, std::string_view key,
                    ForwardIterator begin, ForwardIterator end)
    {
        if (begin == end) return;
        auto distance = static_cast<std::size_t>(std::distance(begin, end));
        detail::add_header(payload_, resp3_type::array, 2 + distance);
        detail::add_bulk(payload_, cmd);
        detail::add_bulk(payload_, key);
        for (; begin != end; ++begin)
            detail::serialize_one(payload_, *begin);
        ++commands_;
    }

    /// Append a command: cmd [range elements...] (no key)
    template <class ForwardIterator>
    void push_range(std::string_view cmd,
                    ForwardIterator begin, ForwardIterator end)
    {
        if (begin == end) return;
        auto distance = static_cast<std::size_t>(std::distance(begin, end));
        detail::add_header(payload_, resp3_type::array, 1 + distance);
        detail::add_bulk(payload_, cmd);
        for (; begin != end; ++begin)
            detail::serialize_one(payload_, *begin);
        ++commands_;
    }

    /// Append a command: cmd key [range container]
    template <class Range>
    void push_range(std::string_view cmd, std::string_view key,
                    const Range& range)
    {
        using std::begin;
        using std::end;
        push_range(cmd, key, begin(range), end(range));
    }

    /// Append a command: cmd [range container] (no key)
    template <class Range>
    void push_range(std::string_view cmd, const Range& range)
    {
        using std::begin;
        using std::end;
        push_range(cmd, begin(range), end(range));
    }

    /// Append pair range: cmd key [k1 v1 k2 v2 ...]
    template <class ForwardIterator>
    void push_range_pairs(std::string_view cmd, std::string_view key,
                          ForwardIterator begin, ForwardIterator end)
    {
        if (begin == end) return;
        auto distance = static_cast<std::size_t>(std::distance(begin, end));
        detail::add_header(payload_, resp3_type::array, 2 + distance * 2);
        detail::add_bulk(payload_, cmd);
        detail::add_bulk(payload_, key);
        for (; begin != end; ++begin) {
            detail::serialize_one(payload_, begin->first);
            detail::serialize_one(payload_, begin->second);
        }
        ++commands_;
    }

    /// Get encoded payload
    [[nodiscard]] auto payload() const noexcept -> std::string_view {
        return payload_;
    }

    /// Command count
    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return commands_;
    }

    /// Check if empty
    [[nodiscard]] auto empty() const noexcept -> bool {
        return commands_ == 0;
    }

    /// Clear (retain memory)
    void clear() {
        payload_.clear();
        commands_ = 0;
    }

    /// Pre-allocate payload memory
    void reserve(std::size_t n) {
        payload_.reserve(n);
    }

private:
    std::string payload_;
    std::size_t commands_ = 0;
};

} // namespace cnetmod::redis
