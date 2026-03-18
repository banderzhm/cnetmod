module;

#if defined(__has_include)
    #if __has_include(<cxxabi.h>)
        #include <cxxabi.h>
        #include <cstdlib>
        #define CNETMOD_RECOVER_HAS_DEMANGLE 1
    #endif
#endif

/**
 * @file recover.cppm
 * @brief Exception Recovery Middleware — Catch unhandled handler exceptions, return 500 and log
 *
 * Prevents handler exceptions from causing connection drops without response.
 * Should be placed at outermost layer of middleware chain to catch all exceptions.
 *
 * Usage example:
 *   import cnetmod.protocol.http.middleware.recover;
 *
 *   svr.use(recover());  // Outermost layer
 *   svr.use(access_log());
 *   svr.use(cors());
 *   // ...
 */
export module cnetmod.protocol.http.middleware.recover;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.core.log;

namespace cnetmod {

// =============================================================================
// recover_options — Explicit knobs (avoid hidden env vars)
// =============================================================================

export struct recover_options {
    // If true, logs a one-line preview of request body (truncated).
    // Recommended for local debugging only.
    bool        log_body = false;

    // Max bytes logged for body preview (one line, with CR/LF/TAB collapsed).
    std::size_t max_body_bytes = 512;

    // Back-compat: allow env vars to turn body logging on without code changes.
    // If you prefer fully explicit behavior, set this to false.
    bool        allow_env_override = true;
};

namespace detail {

inline auto truncate_one_line_for_recover(std::string_view s, std::size_t max_bytes) -> std::string {
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

inline auto should_log_body(std::string_view ct, const recover_options& opts) -> bool {
    if (opts.log_body) return true;
    if (!opts.allow_env_override) return false;

    // Back-compat env flags. Still useful for quick triage, but can be disabled via opts.
    if (const char* v = std::getenv("CNETMOD_RECOVER_LOG_BODY"); v && std::string_view(v) == "1") {
        return true;
    }
    if (const char* v = std::getenv("RECOVER_LOG_BODY"); v && std::string_view(v) == "1") {
        return true;
    }

    (void)ct;
    return false;
}

inline auto exception_type_name(const std::exception& e) -> std::string {
    // typeid name is implementation-defined but still useful.
    const char* raw = typeid(e).name();
#if defined(CNETMOD_RECOVER_HAS_DEMANGLE)
    int status = 0;
    if (char* dem = abi::__cxa_demangle(raw, nullptr, nullptr, &status); dem) {
        std::string s = (status == 0) ? std::string(dem) : std::string(raw);
        std::free(dem);
        return s;
    }
#endif
    return raw ? std::string(raw) : std::string{};
}

inline auto params_to_string(const http::route_params& p) -> std::string {
    if (p.named.empty() && p.wildcard.empty()) return {};
    std::string out;
    out.reserve(256);
    out.append("{");
    bool first = true;
    for (const auto& [k, v] : p.named) {
        if (!first) out.append(", ");
        first = false;
        out.append(k);
        out.append("=");
        out.append(v);
    }
    if (!p.wildcard.empty()) {
        if (!first) out.append(", ");
        out.append("*=");
        out.append(p.wildcard);
    }
    out.append("}");
    return out;
}

inline auto next_error_id() -> std::string {
    static std::atomic_uint64_t seq{0};
    auto n = seq.fetch_add(1, std::memory_order_relaxed) + 1;
    // Compact monotonic ID; good enough for correlating logs with responses.
    return std::to_string(n);
}

} // namespace detail

// =============================================================================
// recover — Exception Recovery Middleware
// =============================================================================
//
// Behavior:
//   try { co_await next(); }
//   catch (std::exception) → 500 + error log
//   catch (...)            → 500 + error log

export inline auto recover(recover_options opts = {}) -> http::middleware_fn
{
    return [opts = std::move(opts)](http::request_context& ctx, http::next_fn next) -> task<void> {
        try {
            co_await next();
        } catch (const std::exception& e) {
            const auto error_id = detail::next_error_id();
            const auto ex_type  = detail::exception_type_name(e);

            const auto ct   = ctx.get_header("Content-Type");
            const auto clen = ctx.get_header("Content-Length");
            const auto body_len = ctx.body().size();
            const auto params = detail::params_to_string(ctx.params());

            if (detail::should_log_body(ct, opts)) {
                logger::error("[recover id={}] {} {} uri={} qs={} ct={} clen={} body_len={} params={} ex={} what={} body={}",
                    error_id,
                    ctx.method(), ctx.path(),
                    ctx.uri(), ctx.query_string(),
                    ct, clen, body_len, params,
                    ex_type, e.what(),
                    detail::truncate_one_line_for_recover(ctx.body(), opts.max_body_bytes));
            } else {
                logger::error("[recover id={}] {} {} uri={} qs={} ct={} clen={} body_len={} params={} ex={} what={}",
                    error_id,
                    ctx.method(), ctx.path(),
                    ctx.uri(), ctx.query_string(),
                    ct, clen, body_len, params,
                    ex_type, e.what());
            }

            ctx.resp().set_header("X-Error-Id", error_id);
            ctx.json(http::status::internal_server_error,
                std::format(R"({{"error":"internal server error","error_id":"{}"}})", error_id));
        } catch (...) {
            const auto error_id = detail::next_error_id();
            const auto ct   = ctx.get_header("Content-Type");
            const auto clen = ctx.get_header("Content-Length");
            const auto body_len = ctx.body().size();
            const auto params = detail::params_to_string(ctx.params());

            logger::error("[recover id={}] {} {} uri={} qs={} ct={} clen={} body_len={} params={} ex=<unknown>",
                error_id,
                ctx.method(), ctx.path(),
                ctx.uri(), ctx.query_string(),
                ct, clen, body_len, params);
            ctx.resp().set_header("X-Error-Id", error_id);
            ctx.json(http::status::internal_server_error,
                std::format(R"({{"error":"internal server error","error_id":"{}"}})", error_id));
        }
    };
}

} // namespace cnetmod