/**
 * @file access_log.cppm
 * @brief HTTP / WebSocket Aspect Tool — API Latency Calculation and Access Logging
 *
 * Provides two aspects:
 *
 * 1. access_log() → http::middleware_fn
 *    HTTP request latency logging middleware (onion model), records method, path, status, latency.
 *
 * 2. ws_access_log(handler) → ws::ws_handler_fn
 *    WebSocket connection latency logging wrapper, records path, connection duration.
 *
 * Usage example:
 *   import cnetmod.protocol.http.middleware.access_log;
 *
 *   // HTTP
 *   http::server svr(ctx);
 *   svr.use(access_log());                         // Global middleware
 *   svr.use(access_log(logger::level::debug));      // Specify log level
 *
 *   // WebSocket
 *   ws::server ws_svr(ctx);
 *   ws_svr.on("/ws/chat", ws_access_log([](ws::ws_context& ctx) -> task<void> {
 *       // ...
 *   }));
 */
export module cnetmod.protocol.http.middleware.access_log;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.protocol.websocket;
import cnetmod.core.log;

namespace cnetmod {

// =============================================================================
// access_log_options — Detailed HTTP dump mode
// =============================================================================

export enum class access_log_format : std::uint8_t {
    brief,   // method path status latency
    http,    // curl-style HTTP dump (request + response)
};

export enum class access_log_dump : std::uint8_t {
    error_only,  // dump only if status >= 400
    always,      // dump for every request
};

export struct access_log_options {
    logger::level    lv     = logger::level::info;
    access_log_format format = access_log_format::brief;
    access_log_dump  dump   = access_log_dump::error_only;

    bool log_request_headers  = true;
    bool log_request_body     = true;
    bool log_response_headers = true;
    bool log_response_body    = true;

    // Body can be large or contain secrets; we always truncate.
    std::size_t max_body_bytes = 2048;

    // Hide obvious secrets by default.
    bool redact_sensitive_headers = true;
};

namespace detail {

inline auto to_lower_ascii(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c >= 'A' && c <= 'Z') out.push_back(static_cast<char>(c - 'A' + 'a'));
        else out.push_back(static_cast<char>(c));
    }
    return out;
}

inline auto is_textual_content_type(std::string_view ct) -> bool {
    auto l = to_lower_ascii(ct);
    if (l.starts_with("text/")) return true;
    if (l.find("json") != std::string::npos) return true;
    if (l.find("xml") != std::string::npos) return true;
    if (l.find("x-www-form-urlencoded") != std::string::npos) return true;
    if (l.find("javascript") != std::string::npos) return true;
    return false;
}

inline auto truncate_one_line(std::string_view s, std::size_t max_bytes) -> std::string {
    std::string out;
    out.reserve(std::min(max_bytes, s.size()));
    for (std::size_t i = 0; i < s.size() && out.size() < max_bytes; ++i) {
        char c = s[i];
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        out.push_back(c);
    }
    if (s.size() > max_bytes) out.append("...");
    return out;
}

inline auto is_sensitive_header_key(std::string_view k) -> bool {
    auto l = to_lower_ascii(k);
    return l == "authorization" ||
           l == "proxy-authorization" ||
           l == "cookie" ||
           l == "set-cookie" ||
           l == "x-api-key" ||
           l == "api-key" ||
           l == "x-openai-api-key";
}

inline void append_headers_block(std::string& out,
                                 std::string_view prefix,
                                 const http::header_map& headers,
                                 bool redact)
{
    for (const auto& [k, v] : headers) {
        out.append(prefix);
        out.append(k);
        out.append(": ");
        if (redact && is_sensitive_header_key(k)) {
            out.append("<redacted>");
        } else {
            out.append(v);
        }
        out.push_back('\n');
    }
}

inline void append_body_block(std::string& out,
                              std::string_view prefix,
                              std::string_view body,
                              std::string_view content_type,
                              std::size_t max_bytes)
{
    if (body.empty()) return;

    if (!is_textual_content_type(content_type)) {
        out.append(prefix);
        out.append(std::format("<non-text body: {} bytes>", body.size()));
        out.push_back('\n');
        return;
    }

    out.append(prefix);
    out.append(truncate_one_line(body, max_bytes));
    out.push_back('\n');
}

inline auto should_dump(access_log_dump dump, int status) -> bool {
    if (dump == access_log_dump::always) return true;
    return status >= 400;
}

inline auto build_http_dump(http::request_context& ctx,
                            const access_log_options& opts) -> std::string
{
    std::string out;
    out.reserve(512);

    // Request line
    out.append("> ");
    out.append(std::string(ctx.method()));
    out.push_back(' ');
    out.append(ctx.uri());
    out.push_back('\n');

    if (opts.log_request_headers) {
        append_headers_block(out, "> ", ctx.headers(), opts.redact_sensitive_headers);
    }
    out.append(">\n");

    if (opts.log_request_body) {
        auto ct = ctx.get_header("Content-Type");
        append_body_block(out, "> ", ctx.body(), ct, opts.max_body_bytes);
    }

    // Response
    const auto& resp = ctx.resp();
    out.append("< ");
    out.append(std::string(http::version_to_string(resp.version())));
    out.push_back(' ');
    out.append(std::to_string(resp.status_code()));
    out.push_back(' ');
    out.append(std::string(http::status_reason(resp.status_code())));
    out.push_back('\n');

    if (opts.log_response_headers) {
        append_headers_block(out, "< ", resp.headers(), opts.redact_sensitive_headers);
    }
    out.append("<\n");

    if (opts.log_response_body) {
        auto ct = resp.get_header("Content-Type");
        append_body_block(out, "< ", resp.body(), ct, opts.max_body_bytes);
    }

    return out;
}

} // namespace detail

// =============================================================================
// access_log — HTTP API Latency Logging Middleware
// =============================================================================
//
// Onion model: Timing before and after handler
//
// Log format:
//   [INFO] GET /api/users 200 3.25ms
//   [WARN] POST /api/login 500 120.40ms   (5xx auto-upgraded to warn)
//

/// Create HTTP access log middleware (supports full HTTP dump mode)
export inline auto access_log(access_log_options opts,
    std::source_location loc = std::source_location::current())
    -> http::middleware_fn
{
    return [opts = std::move(opts), loc](http::request_context& ctx, http::next_fn next) -> task<void> {
        auto start = std::chrono::steady_clock::now();

        co_await next();

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration<double, std::milli>(elapsed).count();

        auto status = ctx.resp().status_code();
        auto method = ctx.method();
        auto path   = ctx.path();

        // 5xx → warn, 4xx → keep original level, others → lv
        auto log_lv = opts.lv;
        if (status >= 500)      log_lv = logger::level::warn;
        else if (status >= 400) log_lv = std::max(opts.lv, logger::level::info);

        if (opts.format == access_log_format::brief) {
            logger::detail::write_log(log_lv,
                std::format("{} {} {} {:.2f}ms", method, path, status, ms), loc);
            co_return;
        }

        // Full HTTP dump mode (curl-style). Still keeps a summary line.
        std::string msg = std::format("{} {} {} {:.2f}ms", method, path, status, ms);
        if (detail::should_dump(opts.dump, status)) {
            msg.push_back('\n');
            msg.append(detail::build_http_dump(ctx, opts));
        }

        logger::detail::write_log(log_lv, std::move(msg), loc);
    };
}

/// Create HTTP access log middleware
/// @param lv  Log level for normal requests (5xx auto-upgraded to warn/error)
/// @param loc Auto-capture call site location (line where svr.use(access_log()) is called)
export inline auto access_log(logger::level lv = logger::level::info,
    std::source_location loc = std::source_location::current())
    -> http::middleware_fn
{
    access_log_options opts;
    opts.lv = lv;
    opts.format = access_log_format::brief;
    return access_log(std::move(opts), loc);
}

// =============================================================================
// ws_access_log — WebSocket Connection Latency Logging Wrapper
// =============================================================================
//
// Wraps a ws_handler_fn, prints logs on connection establish/disconnect:
//   [INFO] WS+ /ws/chat            — Connection established
//   [INFO] WS- /ws/chat 45230.12ms — Connection closed + duration
//

/// Wrap WebSocket handler, add connection lifecycle logging
/// @param handler Original handler
/// @param lv  Log level
/// @param loc Auto-capture call site location
export inline auto ws_access_log(ws::ws_handler_fn handler,
                                 logger::level lv = logger::level::info,
                                 std::source_location loc = std::source_location::current())
    -> ws::ws_handler_fn
{
    return [handler = std::move(handler), lv, loc]
           (ws::ws_context& ctx) -> task<void>
    {
        auto path = std::string(ctx.path());

        logger::detail::write_log(lv, std::format("WS+ {}", path), loc);

        auto start = std::chrono::steady_clock::now();

        try {
            co_await handler(ctx);
        } catch (...) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            auto ms = std::chrono::duration<double, std::milli>(elapsed).count();
            logger::detail::write_log(logger::level::error,
                std::format("WS! {} {:.2f}ms (exception)", path, ms), loc);
            throw;
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration<double, std::milli>(elapsed).count();
        logger::detail::write_log(lv,
            std::format("WS- {} {:.2f}ms", path, ms), loc);
    };
}

} // namespace cnetmod
