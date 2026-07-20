module cnetmod.protocol.http.middleware.request_id;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {
namespace {

auto generate_request_id() -> std::string {
  static thread_local std::random_device random_device;
  static constexpr char hex[] = "0123456789abcdef";
  std::string identifier;
  identifier.reserve(32);
  for (int index = 0; index < 16; ++index) {
    const auto byte = static_cast<std::uint8_t>(random_device() & 0xffU);
    identifier.push_back(hex[(byte >> 4U) & 0x0fU]);
    identifier.push_back(hex[byte & 0x0fU]);
  }
  return identifier;
}

auto assign_request_id(std::string header_name, http::request_context &context,
                       http::next_fn next) -> task<void> {
  const auto existing = context.get_header(header_name);
  const auto identifier =
      existing.empty() ? generate_request_id() : std::string(existing);
  context.resp().set_header(header_name, identifier);
  co_await next();
}

} // namespace

auto request_id(std::string_view header_name) -> http::middleware_fn {
  return [header_name = std::string(header_name)](
             http::request_context &context, http::next_fn next) -> task<void> {
    return assign_request_id(header_name, context, std::move(next));
  };
}

} // namespace cnetmod
