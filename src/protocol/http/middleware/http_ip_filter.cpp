module;

#include <cnetmod/config.hpp>
#include <cstring>
#ifdef CNETMOD_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WS2tcpip.h>
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

module cnetmod.protocol.http.middleware.ip_filter;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.core.log;

namespace cnetmod {
namespace {
auto trim(std::string_view value) noexcept -> std::string_view {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())))
    value.remove_prefix(1);
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())))
    value.remove_suffix(1);
  return value;
}
auto strip_ipv6_brackets(std::string_view value) noexcept -> std::string_view {
  value = trim(value);
  return value.size() >= 2 && value.front() == '[' && value.back() == ']'
             ? value.substr(1, value.size() - 2)
             : value;
}
template <std::size_t Size>
auto parse_address(std::string_view value, int family)
    -> std::optional<std::array<std::uint8_t, Size>> {
  value = strip_ipv6_brackets(value);
  std::array<std::uint8_t, Size> address{};
  const auto text = std::string(value);
  return ::inet_pton(family, text.c_str(), address.data()) == 1
             ? std::optional{address}
             : std::nullopt;
}
auto parse_prefix(std::string_view value, int max_bits) -> std::optional<int> {
  value = trim(value);
  int bits{};
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), bits);
  return !value.empty() && error == std::errc{} &&
                 end == value.data() + value.size() && bits >= 0 &&
                 bits <= max_bits
             ? std::optional{bits}
             : std::nullopt;
}
template <std::size_t Size>
auto prefix_matches(const std::array<std::uint8_t, Size> &client,
                    const std::array<std::uint8_t, Size> &network,
                    int bits) noexcept -> bool {
  const auto full_bytes = static_cast<std::size_t>(bits / 8);
  for (std::size_t index = 0; index < full_bytes; ++index)
    if (client[index] != network[index])
      return false;
  const auto remainder = bits % 8;
  if (remainder == 0)
    return true;
  const auto mask = static_cast<std::uint8_t>(0xffu << (8 - remainder));
  return (client[full_bytes] & mask) == (network[full_bytes] & mask);
}
auto ip_matches(std::string_view client, std::string_view pattern) -> bool {
  client = strip_ipv6_brackets(client);
  pattern = strip_ipv6_brackets(pattern);
  if (client == pattern)
    return true;
  const auto slash = pattern.find('/');
  if (slash == std::string_view::npos) {
    const auto client_v4 = parse_address<4>(client, AF_INET);
    const auto pattern_v4 = parse_address<4>(pattern, AF_INET);
    if (client_v4 && pattern_v4)
      return *client_v4 == *pattern_v4;
    const auto client_v6 = parse_address<16>(client, AF_INET6);
    const auto pattern_v6 = parse_address<16>(pattern, AF_INET6);
    return client_v6 && pattern_v6 && *client_v6 == *pattern_v6;
  }
  const auto network = strip_ipv6_brackets(pattern.substr(0, slash));
  const auto prefix = pattern.substr(slash + 1);
  if (const auto client_v4 = parse_address<4>(client, AF_INET)) {
    const auto network_v4 = parse_address<4>(network, AF_INET);
    const auto bits = parse_prefix(prefix, 32);
    return network_v4 && bits && prefix_matches(*client_v4, *network_v4, *bits);
  }
  if (const auto client_v6 = parse_address<16>(client, AF_INET6)) {
    const auto network_v6 = parse_address<16>(network, AF_INET6);
    const auto bits = parse_prefix(prefix, 128);
    return network_v6 && bits && prefix_matches(*client_v6, *network_v6, *bits);
  }
  return false;
}
auto ip_in_list(std::string_view ip, const std::vector<std::string> &patterns)
    -> bool {
  return std::ranges::any_of(
      patterns, [ip](const auto &pattern) { return ip_matches(ip, pattern); });
}
auto resolve_client_ip(http::request_context &ctx,
                       const ip_filter_options &opts) -> std::string {
  if (opts.trusted_proxies.empty())
    return http::resolve_client_ip(ctx);
  const auto endpoint = ctx.raw_socket().remote_endpoint();
  const auto peer_ip =
      endpoint ? endpoint->address().to_string() : std::string{"unknown"};
  return ip_in_list(peer_ip, opts.trusted_proxies)
             ? http::resolve_client_ip(ctx, peer_ip)
             : peer_ip;
}
auto filter_request(const ip_filter_options &opts, http::request_context &ctx,
                    http::next_fn next) -> task<void> {
  const auto client_ip = resolve_client_ip(ctx, opts);
  const auto denied =
      !opts.allow_list.empty()
          ? !ip_in_list(client_ip, opts.allow_list)
          : (!opts.deny_list.empty() && ip_in_list(client_ip, opts.deny_list));
  if (denied) {
    logger::warn("{} {} blocked IP: {}", ctx.method(), ctx.path(), client_ip);
    ctx.json(
        opts.denied_status,
        std::format(R"({{"error":"access denied","ip":"{}"}})", client_ip));
    co_return;
  }
  co_await next();
}
} // namespace
auto ip_filter(ip_filter_options opts) -> http::middleware_fn {
  return [opts = std::move(opts)](http::request_context &ctx,
                                  http::next_fn next) -> task<void> {
    return filter_request(opts, ctx, std::move(next));
  };
}
} // namespace cnetmod
