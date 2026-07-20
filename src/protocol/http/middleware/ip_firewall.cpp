module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.http.middleware.ip_firewall;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.cache_store;

namespace cnetmod {

ip_firewall::ip_firewall(cache::cache_store &store,
                         ip_firewall_options opts) noexcept
    : store_(store), opts_(std::move(opts)) {}

auto ip_firewall::check_middleware() -> http::middleware_fn {
  return [this](http::request_context &ctx, http::next_fn next) -> task<void> {
    auto ip = http::resolve_client_ip(ctx);
    if (co_await is_banned(ip)) {
      std::println(std::cerr, "[firewall] blocked banned IP: {}", ip);
      ctx.resp().set_header("Connection", "close");
      ctx.json(opts_.banned_status,
               std::format(R"({{"error":"ip banned","ip":"{}"}})", ip));
      co_return;
    }
    co_await next();
  };
}

auto ip_firewall::track_middleware() -> http::middleware_fn {
  return [this](http::request_context &ctx, http::next_fn next) -> task<void> {
    co_await next();
    const auto status = ctx.resp().status_code();
    int weight = 0;
    if (opts_.track_rate_limit && status == 429)
      weight = 1;
    else if (opts_.track_4xx && status >= 400 && status < 500 && status != 429)
      weight = 1;
    else if (opts_.track_5xx && status >= 500)
      weight = 1;
    if (weight <= 0)
      co_return;
    const auto path = ctx.path();
    for (const auto &[prefix, configured_weight] : opts_.path_weights) {
      if (path.starts_with(prefix)) {
        weight = configured_weight;
        break;
      }
    }
    co_await add_violation(http::resolve_client_ip(ctx), weight);
  };
}

auto ip_firewall::report_violation(std::string_view ip, int weight)
    -> task<void> {
  co_await add_violation(std::string(ip), weight);
}

auto ip_firewall::ban(std::string_view ip) -> task<void> {
  co_await ban(ip, opts_.ban_duration);
}

auto ip_firewall::ban(std::string_view ip, std::chrono::seconds duration)
    -> task<void> {
  co_await store_.set(ban_key(std::string(ip)), "1", duration);
  std::println(std::cerr, "[firewall] banned IP: {} for {}s", ip,
               duration.count());
}

auto ip_firewall::unban(std::string_view ip) -> task<void> {
  const auto value = std::string(ip);
  co_await store_.del(ban_key(value));
  co_await store_.del(violation_key(value));
  std::println(std::cerr, "[firewall] unbanned IP: {}", ip);
}

auto ip_firewall::is_banned(std::string_view ip) -> task<bool> {
  co_return co_await store_.exists(ban_key(std::string(ip)));
}

auto ip_firewall::violation_count(std::string_view ip) -> task<int> {
  auto value = co_await store_.get(violation_key(std::string(ip)));
  if (!value)
    co_return 0;
  int count = 0;
  const auto [_, error] =
      std::from_chars(value->data(), value->data() + value->size(), count);
  co_return error == std::errc{} ? count : 0;
}

auto ip_firewall::ban_key(const std::string &ip) const -> std::string {
  return opts_.key_prefix + "ban:" + ip;
}
auto ip_firewall::violation_key(const std::string &ip) const -> std::string {
  return opts_.key_prefix + "v:" + ip;
}

auto ip_firewall::add_violation(const std::string &ip, int weight)
    -> task<void> {
  if (co_await is_banned(ip))
    co_return;
  const auto key = violation_key(ip);
  int count = 0;
  if (auto value = co_await store_.get(key)) {
    const auto [_, error] =
        std::from_chars(value->data(), value->data() + value->size(), count);
    if (error != std::errc{})
      count = 0;
  }
  count += weight;
  co_await store_.set(key, std::to_string(count), opts_.violation_window);
  if (count >= opts_.max_violations) {
    co_await ban(ip, opts_.ban_duration);
    co_await store_.del(key);
  }
}

auto firewall_status_handler(ip_firewall &fw) -> http::handler_fn {
  return [&fw](http::request_context &ctx) -> task<void> {
    const auto ip = ctx.param("ip");
    if (ip.empty()) {
      ctx.json(http::status::bad_request, R"({"error":"missing ip param"})");
      co_return;
    }
    const auto banned = co_await fw.is_banned(ip);
    const auto count = co_await fw.violation_count(ip);
    ctx.json(http::status::ok,
             std::format(R"({{"ip":"{}","banned":{},"violations":{}}})", ip,
                         banned ? "true" : "false", count));
  };
}

auto firewall_ban_handler(ip_firewall &fw) -> http::handler_fn {
  return [&fw](http::request_context &ctx) -> task<void> {
    const auto ip = ctx.param("ip");
    if (ip.empty()) {
      ctx.json(http::status::bad_request, R"({"error":"missing ip param"})");
      co_return;
    }
    co_await fw.ban(ip);
    ctx.json(http::status::ok,
             std::format(R"({{"ip":"{}","banned":true}})", ip));
  };
}

auto firewall_unban_handler(ip_firewall &fw) -> http::handler_fn {
  return [&fw](http::request_context &ctx) -> task<void> {
    const auto ip = ctx.param("ip");
    if (ip.empty()) {
      ctx.json(http::status::bad_request, R"({"error":"missing ip param"})");
      co_return;
    }
    co_await fw.unban(ip);
    ctx.json(http::status::ok,
             std::format(R"({{"ip":"{}","banned":false}})", ip));
  };
}

} // namespace cnetmod
