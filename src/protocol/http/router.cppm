module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http:router;

import std;
import :types;
import :parser;
import :request;
import :response;
import :multipart;
import :sse;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;

namespace cnetmod::http {

// =============================================================================
// route_params — Route Parameters
// =============================================================================

export struct route_params {
    std::unordered_map<std::string, std::string> named;   // :id → value
    std::string wildcard;                                  // *filepath → rest

    [[nodiscard]] auto get(std::string_view key) const noexcept
        -> std::string_view
    {
        auto it = named.find(std::string(key));
        if (it != named.end()) return it->second;
        return {};
    }
};

// =============================================================================
// request_context — Request Context
// =============================================================================

export class request_context {
public:
    /// HTTP/1.1 constructor (from request_parser)
    request_context(io_context& ctx, socket& sock,
                    const request_parser& parser,
                    response& resp, route_params params)
        : ctx_(ctx), sock_(sock), resp_(resp), params_(std::move(params))
        , headers_ptr_(&parser.headers())
        , method_(parser.method())
        , body_(parser.body())
    {
        init_path_query(parser.uri());
    }

    /// Protocol-neutral constructor (for HTTP/2, future HTTP/3)
    request_context(io_context& ctx, socket& sock,
                    std::string_view method, std::string_view uri,
                    const header_map& headers, std::string_view body,
                    response& resp, route_params params,
                    std::shared_ptr<request_body_stream> body_stream = {})
        : ctx_(ctx), sock_(sock), resp_(resp), params_(std::move(params))
        , headers_ptr_(&headers)
        , method_(method)
        , body_storage_(body)
        , body_(body_storage_)
        , body_stream_(std::move(body_stream))
    {
        init_path_query(uri);
    }

    // --- Request Access ---

    [[nodiscard]] auto method() const noexcept -> std::string_view {
        return method_;
    }

    [[nodiscard]] auto method_enum() const noexcept -> std::optional<http_method> {
        return string_to_method(method_);
    }

    [[nodiscard]] auto path() const noexcept -> std::string_view { return path_; }
    [[nodiscard]] auto query_string() const noexcept -> std::string_view { return query_; }
    [[nodiscard]] auto uri() const noexcept -> std::string_view { return uri_; }

    [[nodiscard]] auto headers() const noexcept -> const header_map& {
        return *headers_ptr_;
    }

    [[nodiscard]] auto body() const -> std::string_view {
        drain_available_body_chunks();
        return body_;
    }

    [[nodiscard]] auto has_body_stream() const noexcept -> bool {
        return body_stream_ != nullptr;
    }

    [[nodiscard]] auto receive_body_chunk()
        -> task<std::optional<request_body_chunk>>
    {
        if (!body_stream_) {
            co_return std::nullopt;
        }
        co_return co_await body_stream_->receive();
    }

    [[nodiscard]] auto read_full_body() -> task<std::string_view> {
        if (!body_stream_ || body_stream_drained_) {
            drain_available_body_chunks();
            co_return body_;
        }

        drain_available_body_chunks();
        while (auto chunk = co_await body_stream_->receive()) {
            body_storage_.append(
                reinterpret_cast<const char*>(chunk->data()),
                chunk->size());
        }
        body_ = body_storage_;
        body_stream_drained_ = true;
        co_return body_;
    }

    [[nodiscard]] auto get_header(std::string_view key) const -> std::string_view {
        auto it = headers_ptr_->find(std::string(key));
        if (it != headers_ptr_->end()) return it->second;
        return {};
    }

    // --- Route Parameters ---

    [[nodiscard]] auto param(std::string_view name) const noexcept
        -> std::string_view
    {
        return params_.get(name);
    }

    [[nodiscard]] auto wildcard() const noexcept -> std::string_view {
        return params_.wildcard;
    }

    [[nodiscard]] auto params() const noexcept -> const route_params& {
        return params_;
    }

    // --- Response Shortcuts ---

    void text(int status_code, std::string_view text_body) {
        resp_.set_status(status_code);
        resp_.set_header("Content-Type", "text/plain; charset=utf-8");
        resp_.set_body(std::string(text_body));
    }

    void json(int status_code, std::string_view json_body) {
        resp_.set_status(status_code);
        resp_.set_header("Content-Type", "application/json; charset=utf-8");
        resp_.set_body(std::string(json_body));
    }

    void html(int status_code, std::string_view html_body) {
        resp_.set_status(status_code);
        resp_.set_header("Content-Type", "text/html; charset=utf-8");
        resp_.set_body(std::string(html_body));
    }

    void redirect(std::string_view location, int code = 302) {
        resp_.set_status(code);
        resp_.set_header("Location", location);
    }

    void not_found() {
        text(status::not_found, "404 Not Found");
    }

    // --- SSE Helpers ---

    auto sse_begin(int status_code = status::ok) -> task<bool> {
        if (sse_started_) {
            co_return true;
        }
        resp_.set_status(status_code);
        sse::prepare(resp_, sse::response_options{.status_code = status_code});

        const auto header = resp_.serialize();
        auto wr = co_await async_write_all(
            ctx_, sock_, const_buffer{header.data(), header.size()});
        if (!wr) {
            co_return false;
        }

        sse_started_ = true;
        co_return true;
    }

    auto sse_send(std::string_view data,
                  std::string_view event = {}) -> task<bool>
    {
        if (!(co_await sse_begin())) {
            co_return false;
        }

        auto frame = sse::data(data, event);

        auto wr = co_await async_write_all(
            ctx_, sock_, const_buffer{frame.data(), frame.size()});
        co_return wr.has_value();
    }

    auto sse_json(std::string_view json_payload,
                  std::string_view event = {}) -> task<bool>
    {
        co_return co_await sse_send(json_payload, event);
    }

    auto sse_done() -> task<bool> {
        if (!(co_await sse_begin())) {
            co_return false;
        }
        auto frame = sse::done();
        auto wr = co_await async_write_all(
            ctx_, sock_, const_buffer{frame.data(), frame.size()});
        co_return wr.has_value();
    }

    // --- Form Parsing ---

    /// Lazily parse multipart/form-data or application/x-www-form-urlencoded body
    /// First call performs parsing and caches, subsequent calls return cached pointer
    [[nodiscard]] auto parse_form()
        -> std::expected<const form_data*, std::error_code>
    {
        if (form_cache_) return &*form_cache_;

        auto ct = get_header("Content-Type");
        if (ct.empty()) {
            return std::unexpected(make_error_code(http_errc::invalid_multipart));
        }

        auto r = http::parse_form(ct, body());
        if (!r) return std::unexpected(r.error());

        form_cache_ = std::move(*r);
        return &*form_cache_;
    }

    // --- Low-level Access ---

    [[nodiscard]] auto resp() noexcept -> response& { return resp_; }
    [[nodiscard]] auto io_ctx() noexcept -> io_context& { return ctx_; }
    [[nodiscard]] auto raw_socket() noexcept -> socket& { return sock_; }

private:
    void drain_available_body_chunks() const {
        if (!body_stream_ || body_stream_drained_) {
            return;
        }
        while (auto chunk = body_stream_->try_receive()) {
            body_storage_.append(
                reinterpret_cast<const char*>(chunk->data()),
                chunk->size());
        }
        body_ = body_storage_;
        if (body_stream_->is_closed()) {
            body_stream_drained_ = true;
        }
    }

    void init_path_query(std::string_view uri) {
        uri_ = std::string(uri);
        auto qpos = uri.find('?');
        if (qpos != std::string_view::npos) {
            path_ = std::string(uri.substr(0, qpos));
            query_ = std::string(uri.substr(qpos + 1));
        } else {
            path_ = std::string(uri);
        }
    }

    io_context& ctx_;
    socket& sock_;
    response& resp_;
    route_params params_;
    const header_map* headers_ptr_;   // Non-owning, points to parser's or stream's headers
    std::string_view method_;         // Non-owning, stable for request lifetime
    mutable std::string_view body_;   // Non-owning for HTTP/1, owned view for streamed requests
    mutable std::string body_storage_; // Owned body for streaming/protocol-neutral requests
    std::shared_ptr<request_body_stream> body_stream_;
    std::string uri_;                 // Owned (for uri() accessor)
    std::string path_;                // Owned
    std::string query_;               // Owned
    std::optional<form_data> form_cache_;
    mutable bool body_stream_drained_ = false;
    bool sse_started_ = false;
};

// =============================================================================
// handler / middleware types
// =============================================================================

export using handler_fn = std::function<task<void>(request_context&)>;

/// next_fn: call co_await next() to continue executing subsequent middleware/handler
export using next_fn = std::function<task<void>()>;

/// middleware: fn(ctx, next) — Onion model
export using middleware_fn = std::function<task<void>(request_context&, next_fn)>;

// =============================================================================
// Route Segment Parsing
// =============================================================================

namespace detail {

enum class segment_kind { exact, param, wildcard };

struct segment {
    segment_kind kind;
    std::string  value;   // exact: literal; param: name; wildcard: name
};

/// Parse route pattern into segment sequence
/// e.g. "/api/users/:id/posts/*rest"
///  → [exact("api"), exact("users"), param("id"), exact("posts"), wildcard("rest")]
inline auto parse_pattern(std::string_view pattern) -> std::vector<segment> {
    std::vector<segment> segs;

    // Remove leading /
    if (!pattern.empty() && pattern[0] == '/')
        pattern.remove_prefix(1);

    while (!pattern.empty()) {
        auto slash = pattern.find('/');
        auto part = (slash != std::string_view::npos)
                     ? pattern.substr(0, slash)
                     : pattern;

        if (!part.empty() && part[0] == ':') {
            segs.push_back({segment_kind::param, std::string(part.substr(1))});
        } else if (!part.empty() && part[0] == '*') {
            segs.push_back({segment_kind::wildcard,
                            std::string(part.size() > 1 ? part.substr(1) : "path")});
            break; // wildcard consumes all remaining
        } else {
            segs.push_back({segment_kind::exact, std::string(part)});
        }

        if (slash == std::string_view::npos) break;
        pattern.remove_prefix(slash + 1);
    }

    return segs;
}

/// Split path into segments
inline auto split_path(std::string_view path) -> std::vector<std::string_view> {
    std::vector<std::string_view> parts;
    if (!path.empty() && path[0] == '/')
        path.remove_prefix(1);

    while (!path.empty()) {
        auto slash = path.find('/');
        if (slash != std::string_view::npos) {
            if (slash > 0)
                parts.push_back(path.substr(0, slash));
            path.remove_prefix(slash + 1);
        } else {
            parts.push_back(path);
            break;
        }
    }
    return parts;
}

/// Calculate route priority score (lower is higher priority)
/// exact=0, param=1, wildcard=2, sum by segment
inline auto route_priority(const std::vector<segment>& segs) -> int {
    int score = 0;
    for (auto& s : segs) {
        switch (s.kind) {
            case segment_kind::exact:    score += 0; break;
            case segment_kind::param:    score += 1; break;
            case segment_kind::wildcard: score += 2; break;
        }
    }
    return score;
}

struct route_score {
    int wildcard_count = 0;
    int param_count = 0;
    int literal_count = 0;
    int segment_count = 0;
    int method_cost = 0;
    std::uint64_t order = 0;
};

inline auto route_specificity(const std::vector<segment>& segs,
                              bool any_method,
                              std::uint64_t order) -> route_score
{
    route_score out;
    out.segment_count = static_cast<int>(segs.size());
    out.method_cost = any_method ? 1 : 0;
    out.order = order;
    for (const auto& seg : segs) {
        switch (seg.kind) {
        case segment_kind::exact:
            ++out.literal_count;
            break;
        case segment_kind::param:
            ++out.param_count;
            break;
        case segment_kind::wildcard:
            ++out.wildcard_count;
            break;
        }
    }
    return out;
}

inline auto better_score(const route_score& a, const route_score& b) noexcept -> bool {
    if (a.wildcard_count != b.wildcard_count) return a.wildcard_count < b.wildcard_count;
    if (a.param_count != b.param_count) return a.param_count < b.param_count;
    if (a.literal_count != b.literal_count) return a.literal_count > b.literal_count;
    if (a.segment_count != b.segment_count) return a.segment_count > b.segment_count;
    if (a.method_cost != b.method_cost) return a.method_cost < b.method_cost;
    return a.order < b.order;
}

inline auto is_static_route(const std::vector<segment>& segs) noexcept -> bool {
    return std::ranges::all_of(segs, [](const segment& seg) {
        return seg.kind == segment_kind::exact;
    });
}

inline auto first_literal_segment(const std::vector<segment>& segs)
    -> std::optional<std::string>
{
    if (!segs.empty() && segs.front().kind == segment_kind::exact) {
        return segs.front().value;
    }
    return std::nullopt;
}

inline auto canonical_from_segments(const std::vector<segment>& segs) -> std::string {
    if (segs.empty()) return "/";
    std::string out;
    for (const auto& seg : segs) {
        out.push_back('/');
        out += seg.value;
    }
    return out;
}

inline auto canonical_from_parts(const std::vector<std::string_view>& parts) -> std::string {
    if (parts.empty()) return "/";
    std::string out;
    for (auto part : parts) {
        out.push_back('/');
        out.append(part);
    }
    return out;
}

inline auto method_path_key(std::optional<http_method> method,
                            std::string_view path) -> std::string
{
    std::string key;
    if (method) {
        key = std::string(method_to_string(*method));
    } else {
        key = "*";
    }
    key.push_back(' ');
    key.append(path);
    return key;
}

inline auto method_bucket_key(std::optional<http_method> method,
                              std::string_view first_segment) -> std::string
{
    return method_path_key(method, first_segment);
}

} // namespace detail

// =============================================================================
// match_result — Match Result
// =============================================================================

export struct match_result {
    handler_fn   handler;
    route_params params;
};

// =============================================================================
// router — Route Registration and Matching
// =============================================================================

export class router {
public:
    router() = default;

    // --- Register Routes ---

    auto get(std::string_view pattern, handler_fn fn) -> router& {
        return add(http_method::GET, pattern, std::move(fn));
    }

    auto post(std::string_view pattern, handler_fn fn) -> router& {
        return add(http_method::POST, pattern, std::move(fn));
    }

    auto put(std::string_view pattern, handler_fn fn) -> router& {
        return add(http_method::PUT, pattern, std::move(fn));
    }

    auto del(std::string_view pattern, handler_fn fn) -> router& {
        return add(http_method::DELETE_, pattern, std::move(fn));
    }

    auto patch(std::string_view pattern, handler_fn fn) -> router& {
        return add(http_method::PATCH, pattern, std::move(fn));
    }

    /// Match any method
    auto any(std::string_view pattern, handler_fn fn) -> router& {
        return add_route(std::nullopt, pattern, std::move(fn));
    }

    // --- Matching ---

    [[nodiscard]] auto match(http_method method, std::string_view path) const
        -> std::optional<match_result>
    {
        auto parts = detail::split_path(path);
        auto canonical_path = detail::canonical_from_parts(parts);

        if (auto exact = find_exact(method, canonical_path)) {
            return match_result{exact->handler, {}};
        }

        const route_entry* best = nullptr;
        route_params best_params;

        auto consider = [&](std::size_t idx) {
            const auto& entry = entries_[idx];
            route_params rp;
            if (try_match(entry.segments, parts, rp)) {
                if (!best || detail::better_score(entry.score, best->score)) {
                    best = &entry;
                    best_params = std::move(rp);
                }
            }
        };

        auto consider_bucket = [&](std::optional<http_method> bucket_method,
                                   std::string_view first) {
            auto it = first_literal_index_.find(
                detail::method_bucket_key(bucket_method, first));
            if (it == first_literal_index_.end()) return;
            for (auto idx : it->second) consider(idx);
        };

        if (!parts.empty()) {
            consider_bucket(method, parts.front());
            consider_bucket(std::nullopt, parts.front());
        }
        for (auto idx : generic_indices_) {
            const auto& entry = entries_[idx];
            if (entry.method.has_value() && *entry.method != method) continue;
            consider(idx);
        }

        if (best) {
            return match_result{best->handler, std::move(best_params)};
        }
        return std::nullopt;
    }

    /// String version (method string passed from request_parser)
    [[nodiscard]] auto match(std::string_view method_str, std::string_view path) const
        -> std::optional<match_result>
    {
        auto m = string_to_method(method_str);
        if (!m) return std::nullopt;
        return match(*m, path);
    }

private:
    struct route_entry {
        std::optional<http_method>      method;
        std::vector<detail::segment>    segments;
        handler_fn                      handler;
        detail::route_score             score;
        std::uint64_t                   order = 0;
    };

    auto add(http_method method, std::string_view pattern, handler_fn fn)
        -> router&
    {
        return add_route(method, pattern, std::move(fn));
    }

    auto add_route(std::optional<http_method> method,
                   std::string_view pattern,
                   handler_fn fn) -> router&
    {
        auto segments = detail::parse_pattern(pattern);
        auto order = next_order_++;
        const auto idx = entries_.size();
        const bool is_any = !method.has_value();
        const bool is_static = detail::is_static_route(segments);
        auto first_literal = detail::first_literal_segment(segments);
        auto canonical = detail::canonical_from_segments(segments);

        entries_.push_back({
            method,
            std::move(segments),
            std::move(fn),
            {},
            order,
        });
        auto& entry = entries_.back();
        entry.score = detail::route_specificity(entry.segments, is_any, order);

        if (is_static) {
            exact_index_[detail::method_path_key(method, canonical)].push_back(idx);
        } else if (first_literal) {
            first_literal_index_[detail::method_bucket_key(method, *first_literal)].push_back(idx);
        } else {
            generic_indices_.push_back(idx);
        }

        return *this;
    }

    [[nodiscard]] auto find_exact(http_method method, std::string_view canonical_path) const
        -> const route_entry*
    {
        const route_entry* best = nullptr;
        auto find_in = [&](std::optional<http_method> m) {
            auto it = exact_index_.find(detail::method_path_key(m, canonical_path));
            if (it == exact_index_.end()) return;
            for (auto idx : it->second) {
                const auto& entry = entries_[idx];
                if (!best || detail::better_score(entry.score, best->score)) {
                    best = &entry;
                }
            }
        };
        find_in(method);
        find_in(std::nullopt);
        return best;
    }

    /// Try to match path parts with pattern segments
    static auto try_match(const std::vector<detail::segment>& segs,
                          const std::vector<std::string_view>& parts,
                          route_params& out) -> bool
    {
        std::size_t si = 0;
        std::size_t pi = 0;

        for (; si < segs.size(); ++si) {
            auto& seg = segs[si];

            if (seg.kind == detail::segment_kind::wildcard) {
                // Consume all remaining parts
                std::string rest;
                for (std::size_t j = pi; j < parts.size(); ++j) {
                    if (!rest.empty()) rest += '/';
                    rest += parts[j];
                }
                out.wildcard = std::move(rest);
                out.named[seg.value] = out.wildcard;
                return true;
            }

            if (pi >= parts.size()) return false;

            if (seg.kind == detail::segment_kind::exact) {
                if (parts[pi] != seg.value) return false;
            } else { // param
                out.named[seg.value] = std::string(parts[pi]);
            }
            ++pi;
        }

        // All pattern segments matched, path segments should also be exhausted
        return pi == parts.size();
    }

    std::vector<route_entry> entries_;
    std::unordered_map<std::string, std::vector<std::size_t>> exact_index_;
    std::unordered_map<std::string, std::vector<std::size_t>> first_literal_index_;
    std::vector<std::size_t> generic_indices_;
    std::uint64_t next_order_ = 0;
};

} // namespace cnetmod::http
