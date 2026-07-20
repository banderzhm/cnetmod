module cnetmod.protocol.http.middleware.cors;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {
namespace {

auto join(const std::vector<std::string> &values, std::string_view separator)
    -> std::string {
  std::string result;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0)
      result += separator;
    result += values[index];
  }
  return result;
}

auto origin_allowed(const std::vector<std::string> &origins,
                    std::string_view origin) -> bool {
  if (origins.empty())
    return false;
  if (origins.front() == "*")
    return true;
  for (const auto &allowed_origin : origins) {
    if (allowed_origin == origin)
      return true;
  }
  return false;
}

auto apply_cors(cors_options options, http::request_context &context,
                http::next_fn next) -> task<void> {
  const auto origin = context.get_header("Origin");
  if (origin.empty() || !origin_allowed(options.allow_origins, origin)) {
    co_await next();
    co_return;
  }

  auto &response = context.resp();
  if (options.allow_origins.size() == 1 &&
      options.allow_origins.front() == "*" && !options.allow_credentials) {
    response.set_header("Access-Control-Allow-Origin", "*");
  } else {
    response.set_header("Access-Control-Allow-Origin", origin);
    response.set_header("Vary", "Origin");
  }

  if (options.allow_credentials) {
    response.set_header("Access-Control-Allow-Credentials", "true");
  }
  if (!options.expose_headers.empty()) {
    response.set_header("Access-Control-Expose-Headers",
                        join(options.expose_headers, ", "));
  }

  if (context.method() == "OPTIONS") {
    response.set_status(http::status::no_content);
    response.set_header("Access-Control-Allow-Methods",
                        join(options.allow_methods, ", "));
    response.set_header("Access-Control-Allow-Headers",
                        join(options.allow_headers, ", "));
    response.set_header("Access-Control-Max-Age",
                        std::to_string(options.max_age));
    co_return;
  }

  co_await next();
}

} // namespace

auto cors(cors_options options) -> http::middleware_fn {
  return [options = std::move(options)](http::request_context &context,
                                        http::next_fn next) -> task<void> {
    return apply_cors(options, context, std::move(next));
  };
}

} // namespace cnetmod
