module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.redis:parser;

import std;
import :types;

namespace cnetmod::redis {

// =============================================================================
// RESP3 Incremental Parser
// =============================================================================

/// Incremental RESP3 parser
/// Supports all 17 RESP3 data types, handles nested aggregate structures
/// Reference: Boost.Redis parser design
///
/// Usage:
///   resp3_parser parser;
///   while (!parser.done()) {
///       auto node = parser.consume(data, ec);
///       if (ec) break;
///       if (!node) break;  // Need more data
///       // Process *node
///   }
export class resp3_parser {
public:
    static constexpr std::size_t max_embedded_depth = 5;
    static constexpr std::string_view sep = "\r\n";

    resp3_parser() { reset(); }

    /// Consume data and produce a node
    /// Returns nullopt to indicate need for more data
    [[nodiscard]] auto consume(std::string_view data, std::error_code& ec)
        -> std::optional<resp3_node>
    {
        while (consumed_ < data.size()) {
            auto remaining = data.substr(consumed_);

            // Waiting for bulk body?
            if (bulk_expected()) {
                auto result = consume_bulk_body(remaining, ec);
                if (ec) return std::nullopt;
                if (!result) return std::nullopt; // need more data
                auto node = make_node(bulk_type_, std::move(*result));
                commit_elem();
                return node;
            }

            // Find line ending (CRLF)
            auto crlf = remaining.find(sep);
            if (crlf == std::string_view::npos) return std::nullopt;

            auto line = remaining.substr(0, crlf);
            consumed_ += crlf + 2; // Consume line + CRLF

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

            // Types that need to read bulk body
            if (is_bulk_type(t)) {
                auto len = parse_length(body, ec);
                if (ec) return std::nullopt;
                if (len < 0) {
                    // RESP2 compatibility: $-1 → null
                    auto node = make_node(resp3_type::null, "");
                    commit_elem();
                    return node;
                }
                bulk_length_ = static_cast<std::size_t>(len);
                bulk_type_ = t;
                // Try to read body immediately
                remaining = data.substr(consumed_);
                auto result = consume_bulk_body(remaining, ec);
                if (ec) return std::nullopt;
                if (!result) return std::nullopt;
                auto node = make_node(bulk_type_, std::move(*result));
                bulk_type_ = resp3_type::invalid;
                commit_elem();
                return node;
            }

            // Aggregate types
            if (is_aggregate(t)) {
                auto count = parse_length(body, ec);
                if (ec) return std::nullopt;
                if (count < 0) {
                    // RESP2 compatibility: *-1 → null
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

                // Push to aggregate stack
                if (depth_ >= max_embedded_depth) {
                    ec = make_error_code(redis_errc::exceeds_max_nested_depth);
                    return std::nullopt;
                }
                ++depth_;
                sizes_[depth_] = agg_size * element_multiplicity(t);
                return node;
            }

            // Simple types (inline values)
            auto node = consume_simple(t, body, ec);
            if (ec) return std::nullopt;
            commit_elem();
            return node;
        }

        return std::nullopt; // need more data
    }

    /// Check if current message is fully parsed
    [[nodiscard]] auto done() const noexcept -> bool {
        return depth_ == 0 && sizes_[0] == 0;
    }

    /// Number of bytes consumed
    [[nodiscard]] auto consumed() const noexcept -> std::size_t {
        return consumed_;
    }

    /// Check if parsing is in progress (has consumed some data)
    [[nodiscard]] auto is_parsing() const noexcept -> bool {
        return consumed_ > 0 || depth_ > 0;
    }

    /// Reset parser state
    void reset() {
        depth_ = 0;
        sizes_.fill(1); // sizes_[0] = 1 (top-level sentinel)
        bulk_length_ = 0;
        bulk_type_ = resp3_type::invalid;
        consumed_ = 0;
    }

private:
    // --- State ---
    std::size_t depth_ = 0;
    std::array<std::size_t, max_embedded_depth + 1> sizes_{};
    std::size_t bulk_length_ = 0;
    resp3_type  bulk_type_ = resp3_type::invalid;
    std::size_t consumed_ = 0;

    // --- Helpers ---

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
        // Need bulk_length_ + 2 (trailing CRLF) bytes
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
        // Decrement aggregate count upward through levels
        while (depth_ > 0) {
            --sizes_[depth_];
            if (sizes_[depth_] > 0) return;
            --depth_;
        }
        // Top level
        if (sizes_[0] > 0) --sizes_[0];
    }
};

// =============================================================================
// Convenience Function: Parse All Nodes from Complete Data
// =============================================================================

/// Parse one response (all nodes) from complete RESP3 data
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
