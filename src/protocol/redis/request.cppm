module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.redis:request;

import std;
import :types;

namespace cnetmod::redis {

// =============================================================================
// RESP3 序列化基础函数
// =============================================================================

namespace detail {

/// 追加 CRLF 分隔符
inline void add_separator(std::string& payload) {
    payload.append("\r\n");
}

/// 追加聚合 header: *N\r\n, >N\r\n, %N\r\n, ...
inline void add_header(std::string& payload, resp3_type t, std::size_t size) {
    payload += to_code(t);
    std::format_to(std::back_inserter(payload), "{}", size);
    add_separator(payload);
}

/// 追加 bulk string: $N\r\nDATA\r\n
inline void add_bulk(std::string& payload, std::string_view data) {
    payload += '$';
    std::format_to(std::back_inserter(payload), "{}", data.size());
    add_separator(payload);
    payload.append(data);
    add_separator(payload);
}

/// 整数 → 字符串后追加 bulk
template <class T>
    requires std::is_integral_v<T>
inline void add_bulk_int(std::string& payload, T n) {
    auto s = std::to_string(n);
    add_bulk(payload, s);
}

/// 浮点 → 字符串后追加 bulk
template <class T>
    requires std::is_floating_point_v<T>
inline void add_bulk_float(std::string& payload, T n) {
    auto s = std::to_string(n);
    add_bulk(payload, s);
}

/// 泛型 serialize_one: 根据类型选择序列化方式
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

/// 计算单个值贡献的 bulk 数
template <class T>
constexpr auto bulk_count() -> std::size_t {
    return 1;
}

} // namespace detail

// =============================================================================
// request 类
// =============================================================================

/// Redis 请求 (pipeline)
/// 由一个或多个 Redis 命令组成，使用 RESP3 array of bulk strings 编码
///
/// 用法:
///   request req;
///   req.push("SET", "key", "value");
///   req.push("GET", "key");
///   req.push("HSET", "hash", "f1", 100, "f2", 200);
export class request {
public:
    request() = default;

    /// 追加一条命令 (variadic)
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

    /// 追加一条命令: cmd key [range elements...]
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

    /// 追加一条命令: cmd [range elements...] (无 key)
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

    /// 追加一条命令: cmd key [range container]
    template <class Range>
    void push_range(std::string_view cmd, std::string_view key,
                    const Range& range)
    {
        using std::begin;
        using std::end;
        push_range(cmd, key, begin(range), end(range));
    }

    /// 追加一条命令: cmd [range container] (无 key)
    template <class Range>
    void push_range(std::string_view cmd, const Range& range)
    {
        using std::begin;
        using std::end;
        push_range(cmd, begin(range), end(range));
    }

    /// 追加 pair 范围: cmd key [k1 v1 k2 v2 ...]
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

    /// 获取编码后的 payload
    [[nodiscard]] auto payload() const noexcept -> std::string_view {
        return payload_;
    }

    /// 命令数量
    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return commands_;
    }

    /// 是否为空
    [[nodiscard]] auto empty() const noexcept -> bool {
        return commands_ == 0;
    }

    /// 清空 (保留内存)
    void clear() {
        payload_.clear();
        commands_ = 0;
    }

    /// 预分配 payload 内存
    void reserve(std::size_t n) {
        payload_.reserve(n);
    }

private:
    std::string payload_;
    std::size_t commands_ = 0;
};

} // namespace cnetmod::redis
