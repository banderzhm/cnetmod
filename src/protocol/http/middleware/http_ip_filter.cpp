module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.http.middleware.ip_filter;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.core.log;

namespace cnetmod {
namespace {
    auto ip_in_list(std::string_view ip, const std::vector<std::string>& patterns)
        -> bool
    {
        return std::ranges::any_of(
            patterns, [ip](const auto& pattern)
            {
                return http::ip_matches(ip, pattern);
            });
    }

    auto filter_request(const ip_filter_options& opts, http::request_context& ctx,
        http::next_fn next) -> task<void>
    {
        const auto client_ip = http::resolve_client_ip(ctx, opts.trusted_proxies);
        const auto denied =
            !opts.allow_list.empty()
            ? !ip_in_list(client_ip, opts.allow_list)
            : (!opts.deny_list.empty() && ip_in_list(client_ip, opts.deny_list));
        if (denied)
        {
            logger::warn("{} {} blocked IP: {}", ctx.method(), ctx.path(), client_ip);
            ctx.json(
                opts.denied_status,
                std::format(R"({{"error":"access denied","ip":"{}"}})", client_ip));
            co_return;
        }
        co_await next();
    }
} // namespace

auto ip_filter(ip_filter_options opts) -> http::middleware_fn
{
    return [opts = std::move(opts)](http::request_context& ctx,
               http::next_fn next) -> task<void>
    {
        return filter_request(opts, ctx, std::move(next));
    };
}
} // namespace cnetmod
