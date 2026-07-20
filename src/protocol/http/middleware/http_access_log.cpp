module cnetmod.protocol.http.middleware.access_log;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.protocol.websocket;
import cnetmod.core.log;

namespace cnetmod {
namespace {
auto to_lower_ascii(std::string_view s) -> std::string {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s)
    out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a')
                                       : static_cast<char>(c));
  return out;
}
auto is_textual_content_type(std::string_view ct) -> bool {
  const auto l = to_lower_ascii(ct);
  return l.starts_with("text/") || l.find("json") != std::string::npos ||
         l.find("xml") != std::string::npos ||
         l.find("x-www-form-urlencoded") != std::string::npos ||
         l.find("javascript") != std::string::npos;
}
auto truncate_one_line(std::string_view s, std::size_t max_bytes)
    -> std::string {
  std::string out;
  out.reserve(std::min(max_bytes, s.size()));
  for (std::size_t i = 0; i < s.size() && out.size() < max_bytes; ++i) {
    auto c = s[i];
    out.push_back(c == '\r' || c == '\n' || c == '\t' ? ' ' : c);
  }
  if (s.size() > max_bytes)
    out.append("...");
  return out;
}
auto is_sensitive_header_key(std::string_view key) -> bool {
  const auto lower = to_lower_ascii(key);
  return lower == "authorization" || lower == "proxy-authorization" ||
         lower == "cookie" || lower == "set-cookie" || lower == "x-api-key" ||
         lower == "api-key" || lower == "x-openai-api-key";
}
void append_headers_block(std::string &out, std::string_view prefix,
                          const http::header_map &headers, bool redact) {
  for (const auto &[key, value] : headers) {
    out.append(prefix);
    out.append(key);
    out.append(": ");
    out.append(redact && is_sensitive_header_key(key) ? "<redacted>" : value);
    out.push_back('\n');
  }
}
void append_body_block(std::string &out, std::string_view prefix,
                       std::string_view body, std::string_view content_type,
                       std::size_t max_bytes) {
  if (body.empty())
    return;
  out.append(prefix);
  if (is_textual_content_type(content_type))
    out.append(truncate_one_line(body, max_bytes));
  else
    out.append(std::format("<non-text body: {} bytes>", body.size()));
  out.push_back('\n');
}
auto build_http_dump(http::request_context &ctx, const access_log_options &opts)
    -> std::string {
  std::string out;
  out.reserve(512);
  out.append("> ");
  out.append(ctx.method());
  out.push_back(' ');
  out.append(ctx.uri());
  out.push_back('\n');
  if (opts.log_request_headers)
    append_headers_block(out, "> ", ctx.headers(),
                         opts.redact_sensitive_headers);
  out.append(">\n");
  if (opts.log_request_body)
    append_body_block(out, "> ", ctx.body(), ctx.get_header("Content-Type"),
                      opts.max_body_bytes);
  const auto &response = ctx.resp();
  out.append("< ");
  out.append(http::version_to_string(response.version()));
  out.push_back(' ');
  out.append(std::to_string(response.status_code()));
  out.push_back(' ');
  out.append(http::status_reason(response.status_code()));
  out.push_back('\n');
  if (opts.log_response_headers)
    append_headers_block(out, "< ", response.headers(),
                         opts.redact_sensitive_headers);
  out.append("<\n");
  if (opts.log_response_body)
    append_body_block(out, "< ", response.body(),
                      response.get_header("Content-Type"), opts.max_body_bytes);
  return out;
}
} // namespace

auto access_log(access_log_options opts, std::source_location loc)
    -> http::middleware_fn {
  return [opts = std::move(opts), loc](http::request_context &ctx,
                                       http::next_fn next) -> task<void> {
    const auto start = std::chrono::steady_clock::now();
    co_await next();
    const auto ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    const auto status = ctx.resp().status_code();
    auto log_lv = opts.lv;
    if (status >= 500)
      log_lv = logger::level::warn;
    else if (status >= 400)
      log_lv = std::max(opts.lv, logger::level::info);
    if (opts.format == access_log_format::brief) {
      logger::detail::write_log(log_lv,
                                std::format("{} {} {} {:.2f}ms", ctx.method(),
                                            ctx.path(), status, ms),
                                loc);
      co_return;
    }
    auto message =
        std::format("{} {} {} {:.2f}ms", ctx.method(), ctx.path(), status, ms);
    if (opts.dump == access_log_dump::always || status >= 400) {
      message.push_back('\n');
      message.append(build_http_dump(ctx, opts));
    }
    logger::detail::write_log(log_lv, std::move(message), loc);
  };
}
auto access_log(logger::level lv, std::source_location loc)
    -> http::middleware_fn {
  return access_log(access_log_options{.lv = lv}, loc);
}
auto ws_access_log(ws::ws_handler_fn handler, logger::level lv,
                   std::source_location loc) -> ws::ws_handler_fn {
  return [handler = std::move(handler), lv,
          loc](ws::ws_context &ctx) -> task<void> {
    const auto path = std::string(ctx.path());
    logger::detail::write_log(lv, std::format("WS+ {}", path), loc);
    const auto start = std::chrono::steady_clock::now();
    try {
      co_await handler(ctx);
    } catch (...) {
      const auto ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start)
                          .count();
      logger::detail::write_log(
          logger::level::error,
          std::format("WS! {} {:.2f}ms (exception)", path, ms), loc);
      throw;
    }
    const auto ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    logger::detail::write_log(lv, std::format("WS- {} {:.2f}ms", path, ms),
                              loc);
  };
}
} // namespace cnetmod
