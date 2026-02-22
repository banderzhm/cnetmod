module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http:router;

import std;
import :types;
import :parser;
import :request;
import :response;
import :multipart;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.task;

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
    request_context(io_context& ctx, socket& sock,
                    const request_parser& parser,
                    response& resp, route_params params)
        : ctx_(ctx), sock_(sock), parser_(parser)
        , resp_(resp), params_(std::move(params))
    {
        // Split path and query_string
        auto uri = parser_.uri();
        auto qpos = uri.find('?');
        if (qpos != std::string_view::npos) {
            path_ = std::string(uri.substr(0, qpos));
            query_ = std::string(uri.substr(qpos + 1));
        } else {
            path_ = std::string(uri);
        }
    }

    // --- Request Access ---

    [[nodiscard]] auto method() const noexcept -> std::string_view {
        return parser_.method();
    }

    [[nodiscard]] auto method_enum() const noexcept -> std::optional<http_method> {
        return parser_.method_enum();
    }

    [[nodiscard]] auto path() const noexcept -> std::string_view { return path_; }
    [[nodiscard]] auto query_string() const noexcept -> std::string_view { return query_; }
    [[nodiscard]] auto uri() const noexcept -> std::string_view { return parser_.uri(); }

    [[nodiscard]] auto headers() const noexcept -> const header_map& {
        return parser_.headers();
    }

    [[nodiscard]] auto body() const noexcept -> std::string_view {
        return parser_.body();
    }

    [[nodiscard]] auto get_header(std::string_view key) const -> std::string_view {
        return parser_.get_header(key);
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
    io_context& ctx_;
    socket& sock_;
    const request_parser& parser_;
    response& resp_;
    route_params params_;
    std::string path_;
    std::string query_;
    std::optional<form_data> form_cache_;
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
        entries_.push_back({
            std::nullopt, // any method
            detail::parse_pattern(pattern),
            std::move(fn),
        });
        return *this;
    }

    // --- Matching ---

    [[nodiscard]] auto match(http_method method, std::string_view path) const
        -> std::optional<match_result>
    {
        auto parts = detail::split_path(path);

        const route_entry* best = nullptr;
        route_params best_params;
        int best_priority = std::numeric_limits<int>::max();

        for (auto& entry : entries_) {
            // Check method
            if (entry.method.has_value() && *entry.method != method)
                continue;

            route_params rp;
            if (try_match(entry.segments, parts, rp)) {
                auto prio = detail::route_priority(entry.segments);
                if (!best || prio < best_priority) {
                    best = &entry;
                    best_params = std::move(rp);
                    best_priority = prio;
                }
            }
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
    };

    auto add(http_method method, std::string_view pattern, handler_fn fn)
        -> router&
    {
        entries_.push_back({
            method,
            detail::parse_pattern(pattern),
            std::move(fn),
        });
        return *this;
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
};

} // namespace cnetmod::http
