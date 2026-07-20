export module cnetmod.protocol.http.middleware.ip_filter;

import std;
import cnetmod.protocol.http;

export namespace cnetmod {
struct ip_filter_options {
  std::vector<std::string> allow_list;
  std::vector<std::string> deny_list;
  std::vector<std::string> trusted_proxies;
  int denied_status = http::status::forbidden;
};
[[nodiscard]] auto ip_filter(ip_filter_options opts = {})
    -> http::middleware_fn;
} // namespace cnetmod
