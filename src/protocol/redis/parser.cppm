module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.redis:parser;

import std;
import :types;

namespace cnetmod::redis {

// =============================================================================
// RESP3 增量解析器
// =============================================================================

/// 增量 RESP3 解析器
/// 支持所有 17 种 RESP3 数据类型，处理嵌套聚合结构
/// 参考 Boost.Redis parser 设计
///
/// 使用方式:
///   resp3_parser parser;
///   while (!parser.done()) {
///       auto node = parser.consume(data, ec);
///       if (ec) break;
///       if (!node) break;  // 需要更多数据
///       // 处理 *node
///   }
export class resp3_parser {
public:
    static constexpr std::size_t max_embedded_depth = 5;
    static constexpr std::string_view sep = "\r\n";

    resp3_parser() { reset(); }

    /// 从数据中消费并产出一个节点
    /// 返回 nullopt 表示需要更多数据
    [[nodiscard]] auto consume(std::string_view data, std::error_code& ec)
        -> std::optional<resp3_node>
    {
        while (consumed_ < data.size()) {
            auto remaining = data.substr(consumed_);

            // 是否在等待 bulk body
            if (bulk_expected()) {
                auto result = consume_bulk_body(remaining, ec);
                if (ec) return std::nullopt;
                if (!result) return std::nullopt; // need more data
                auto node = make_node(bulk_type_, std::move(*result));
                commit_elem();
                return node;
            }

            // 找行结束 (CRLF)
            auto crlf = remaining.find(sep);
            if (crlf == std::string_view::npos) return std::nullopt;

            auto line = remaining.substr(0, crlf);
            consumed_ += crlf + 2; // 消耗 line + CRLF

            if (line.empty()) {
                ec = make_error_code(redis_errc::invalid_data_type);
                return std::nullopt;
            }

            auto t = to_type(line[0]);
            auto body = line.substr(1);

            if (t == resp3_type::invalid) {
                ec = make_error_code(redis_errc::invalid_data_type);
                return std::nullopt;
            }

            // 需要读取 bulk body 的类型
            if (is_bulk_type(t)) {
                auto len = parse_length(body, ec);
                if (ec) return std::nullopt;
                if (len < 0) {
                    // RESP2 兼容: $-1 → null
                    auto node = make_node(resp3_type::null, "");
                    commit_elem();
                    return node;
                }
                bulk_length_ = static_cast<std::size_t>(len);
                bulk_type_ = t;
                // 尝试立即读取 body
                remaining = data.substr(consumed_);
                auto result = consume_bulk_body(remaining, ec);
                if (ec) return std::nullopt;
                if (!result) return std::nullopt;
                auto node = make_node(bulk_type_, std::move(*result));
                bulk_type_ = resp3_type::invalid;
                commit_elem();
                return node;
            }

            // 聚合类型
            if (is_aggregate(t)) {
                auto count = parse_length(body, ec);
                if (ec) return std::nullopt;
                if (count < 0) {
                    // RESP2 兼容: *-1 → null
                    auto node = make_node(resp3_type::null, "");
                    commit_elem();
                    return node;
                }
                auto agg_size = static_cast<std::size_t>(count);
                auto node = resp3_node{
                    t, agg_size, depth_, {}
                };

                if (agg_size == 0) {
                    commit_elem();
                    return node;
                }

                // 推入聚合栈
                if (depth_ >= max_embedded_depth) {
                    ec = make_error_code(redis_errc::exceeds_max_nested_depth);
                    return std::nullopt;
                }
                ++depth_;
                sizes_[depth_] = agg_size * element_multiplicity(t);
                return node;
            }

            // 简单类型 (内联值)
            auto node = consume_simple(t, body, ec);
            if (ec) return std::nullopt;
            commit_elem();
            return node;
        }

        return std::nullopt; // need more data
    }

    /// 当前消息是否已完整解析
    [[nodiscard]] auto done() const noexcept -> bool {
        return depth_ == 0 && sizes_[0] == 0;
    }

    /// 已消费的字节数
    [[nodiscard]] auto consumed() const noexcept -> std::size_t {
        return consumed_;
    }

    /// 是否正在解析中 (已消费部分数据)
    [[nodiscard]] auto is_parsing() const noexcept -> bool {
        return consumed_ > 0 || depth_ > 0;
    }

    /// 重置解析器状态
    void reset() {
        depth_ = 0;
        sizes_.fill(1); // sizes_[0] = 1 (顶层哨兵)
        bulk_length_ = 0;
        bulk_type_ = resp3_type::invalid;
        consumed_ = 0;
    }

private:
    // --- 状态 ---
    std::size_t depth_ = 0;
    std::array<std::size_t, max_embedded_depth + 1> sizes_{};
    std::size_t bulk_length_ = 0;
    resp3_type  bulk_type_ = resp3_type::invalid;
    std::size_t consumed_ = 0;

    // --- 辅助 ---

    [[nodiscard]] auto bulk_expected() const noexcept -> bool {
        return bulk_type_ != resp3_type::invalid;
    }

    static auto is_bulk_type(resp3_type t) noexcept -> bool {
        return t == resp3_type::blob_string ||
               t == resp3_type::blob_error ||
               t == resp3_type::verbatim_string ||
               t == resp3_type::streamed_string_part;
    }

    auto parse_length(std::string_view s, std::error_code& ec) -> std::int64_t {
        std::int64_t v = 0;
        auto [ptr, errc] = std::from_chars(s.data(), s.data() + s.size(), v);
        if (errc != std::errc{}) {
            ec = make_error_code(redis_errc::not_a_number);
            return -1;
        }
        return v;
    }

    auto consume_bulk_body(std::string_view remaining, std::error_code& /*ec*/)
        -> std::optional<std::string>
    {
        // 需要 bulk_length_ + 2 (trailing CRLF) 字节
        auto needed = bulk_length_ + 2;
        if (remaining.size() < needed) return std::nullopt;

        auto body = std::string(remaining.substr(0, bulk_length_));
        consumed_ += needed;
        bulk_type_ = resp3_type::invalid;
        return body;
    }

    auto consume_simple(resp3_type t, std::string_view body, std::error_code& ec)
        -> resp3_node
    {
        switch (t) {
        case resp3_type::simple_string:
        case resp3_type::simple_error:
        case resp3_type::number:
        case resp3_type::doublean:
        case resp3_type::big_number:
            return make_node(t, std::string(body));

        case resp3_type::boolean:
            if (body != "t" && body != "f") {
                ec = make_error_code(redis_errc::unexpected_bool_value);
                return {};
            }
            return make_node(t, std::string(body));

        case resp3_type::null:
            return make_node(t, "");

        default:
            ec = make_error_code(redis_errc::invalid_data_type);
            return {};
        }
    }

    auto make_node(resp3_type t, std::string value) -> resp3_node {
        return resp3_node{t, 0, depth_, std::move(value)};
    }

    void commit_elem() noexcept {
        // 逐层向上递减聚合计数
        while (depth_ > 0) {
            --sizes_[depth_];
            if (sizes_[depth_] > 0) return;
            --depth_;
        }
        // 顶层
        if (sizes_[0] > 0) --sizes_[0];
    }
};

// =============================================================================
// 便捷函数: 从完整数据解析所有节点
// =============================================================================

/// 从完整的 RESP3 数据中解析一个响应 (所有节点)
export auto parse_response(std::string_view data, std::size_t expected_responses = 1)
    -> std::expected<std::vector<resp3_node>, std::error_code>
{
    std::vector<resp3_node> nodes;
    std::error_code ec;

    for (std::size_t r = 0; r < expected_responses; ++r) {
        resp3_parser parser;
        while (!parser.done()) {
            auto node = parser.consume(data, ec);
            if (ec) return std::unexpected(ec);
            if (!node) return std::unexpected(make_error_code(redis_errc::empty_field));
            nodes.push_back(std::move(*node));
        }
        data.remove_prefix(parser.consumed());
    }

    return nodes;
}

} // namespace cnetmod::redis
