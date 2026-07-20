module;

#if defined(__has_include)
#if __has_include(<cxxabi.h>)
#include <cstdlib>
#include <cxxabi.h>
#define CNETMOD_RECOVER_HAS_DEMANGLE 1
#endif
#endif

module cnetmod.protocol.http.middleware.recover;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.core.log;

namespace cnetmod {
namespace {
auto truncate_one_line(std::string_view value, std::size_t max_bytes)
    -> std::string {
  std::string result;
  result.reserve(std::min(max_bytes, value.size()));
  for (std::size_t i = 0; i < value.size() && result.size() < max_bytes; ++i) {
    auto character = value[i];
    result.push_back(character == '\r' || character == '\n' || character == '\t'
                         ? ' '
                         : character);
  }
  if (value.size() > max_bytes)
    result.append("...");
  return result;
}
auto should_log_body(const recover_options &opts) -> bool {
  if (opts.log_body)
    return true;
  if (!opts.allow_env_override)
    return false;
  if (const auto *value = std::getenv("CNETMOD_RECOVER_LOG_BODY");
      value && std::string_view(value) == "1")
    return true;
  if (const auto *value = std::getenv("RECOVER_LOG_BODY");
      value && std::string_view(value) == "1")
    return true;
  return false;
}
auto exception_type_name(const std::exception &exception) -> std::string {
  const auto *raw = typeid(exception).name();
#if defined(CNETMOD_RECOVER_HAS_DEMANGLE)
  int status = 0;
  if (auto *demangled = abi::__cxa_demangle(raw, nullptr, nullptr, &status);
      demangled) {
    const auto name = status == 0 ? std::string(demangled) : std::string(raw);
    std::free(demangled);
    return name;
  }
#endif
  return raw ? std::string(raw) : std::string{};
}
auto params_to_string(const http::route_params &params) -> std::string {
  if (params.named.empty() && params.wildcard.empty())
    return {};
  std::string result{"{"};
  bool first = true;
  for (const auto &[key, value] : params.named) {
    if (!first)
      result.append(", ");
    first = false;
    result.append(key).append("=").append(value);
  }
  if (!params.wildcard.empty()) {
    if (!first)
      result.append(", ");
    result.append("*=").append(params.wildcard);
  }
  result.push_back('}');
  return result;
}
auto next_error_id() -> std::string {
  static std::atomic_uint64_t sequence{0};
  return std::to_string(sequence.fetch_add(1, std::memory_order_relaxed) + 1);
}
auto recover_request(const recover_options &opts, http::request_context &ctx,
                     http::next_fn next) -> task<void> {
  try {
    co_await next();
  } catch (const std::exception &exception) {
    const auto error_id = next_error_id();
    const auto content_type = ctx.get_header("Content-Type");
    const auto content_length = ctx.get_header("Content-Length");
    const auto params = params_to_string(ctx.params());
    if (should_log_body(opts))
      logger::error("[recover id={}] {} {} uri={} qs={} ct={} clen={} "
                    "body_len={} params={} ex={} what={} body={}",
                    error_id, ctx.method(), ctx.path(), ctx.uri(),
                    ctx.query_string(), content_type, content_length,
                    ctx.body().size(), params, exception_type_name(exception),
                    exception.what(),
                    truncate_one_line(ctx.body(), opts.max_body_bytes));
    else
      logger::error("[recover id={}] {} {} uri={} qs={} ct={} clen={} "
                    "body_len={} params={} ex={} what={}",
                    error_id, ctx.method(), ctx.path(), ctx.uri(),
                    ctx.query_string(), content_type, content_length,
                    ctx.body().size(), params, exception_type_name(exception),
                    exception.what());
    ctx.resp().set_header("X-Error-Id", error_id);
    ctx.json(
        http::status::internal_server_error,
        std::format(R"({{"error":"internal server error","error_id":"{}"}})",
                    error_id));
  } catch (...) {
    const auto error_id = next_error_id();
    logger::error("[recover id={}] {} {} uri={} qs={} ct={} clen={} "
                  "body_len={} params={} ex=<unknown>",
                  error_id, ctx.method(), ctx.path(), ctx.uri(),
                  ctx.query_string(), ctx.get_header("Content-Type"),
                  ctx.get_header("Content-Length"), ctx.body().size(),
                  params_to_string(ctx.params()));
    ctx.resp().set_header("X-Error-Id", error_id);
    ctx.json(
        http::status::internal_server_error,
        std::format(R"({{"error":"internal server error","error_id":"{}"}})",
                    error_id));
  }
}
} // namespace
auto recover(recover_options opts) -> http::middleware_fn {
  return [opts = std::move(opts)](http::request_context &ctx,
                                  http::next_fn next) -> task<void> {
    return recover_request(opts, ctx, std::move(next));
  };
}
} // namespace cnetmod
