/**
 * @file ip_filter.cppm
 * @brief IP 黑白名单中间件 — 基于客户端 IP 的访问控制
 *
 * 支持白名单模式（仅允许列表内 IP）和黑名单模式（拒绝列表内 IP）。
 * 自动解析 X-Forwarded-For / X-Real-IP 获取真实客户端 IP。
 *
 * 使用示例:
 *   import cnetmod.middleware.ip_filter;
 *
 *   // 白名单: 仅允许内网
 *   svr.use(ip_filter({.allow_list = {"127.0.0.1", "10.0.0.0/8"}}));
 *
 *   // 黑名单: 封禁特定 IP
 *   svr.use(ip_filter({.deny_list = {"1.2.3.4", "5.6.7.8"}}));
 */
export module cnetmod.middleware.ip_filter;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.core.log;

namespace cnetmod {

// =============================================================================
// ip_filter_options — IP 过滤配置
// =============================================================================

export struct ip_filter_options {
    /// 白名单: 非空时仅允许列表内 IP（优先于黑名单）
    std::vector<std::string> allow_list;

    /// 黑名单: 拒绝列表内 IP（仅在白名单为空时生效）
    std::vector<std::string> deny_list;

    /// 被拒绝时的 HTTP 状态码
    int denied_status = http::status::forbidden;
};

// =============================================================================
// 内部: IP 解析与匹配
// =============================================================================

namespace detail {

/// 从请求头解析客户端真实 IP
/// 优先级: X-Forwarded-For (最左) > X-Real-IP > "unknown"
inline auto resolve_ip(const http::request_context& ctx) -> std::string {
    if (auto xff = ctx.get_header("X-Forwarded-For"); !xff.empty()) {
        auto comma = xff.find(',');
        auto ip = (comma != std::string_view::npos)
                  ? xff.substr(0, comma) : xff;
        while (!ip.empty() && ip.front() == ' ') ip.remove_prefix(1);
        while (!ip.empty() && ip.back() == ' ')  ip.remove_suffix(1);
        if (!ip.empty()) return std::string(ip);
    }
    if (auto xri = ctx.get_header("X-Real-IP"); !xri.empty())
        return std::string(xri);
    return "unknown";
}

/// 简单 CIDR 匹配（仅支持 IPv4）
/// 格式: "10.0.0.0/8" 或精确 IP "192.168.1.1"
inline auto ip_matches(std::string_view client_ip,
                       std::string_view pattern) -> bool
{
    // 精确匹配
    if (client_ip == pattern)
        return true;

    // CIDR 匹配
    auto slash = pattern.find('/');
    if (slash == std::string_view::npos)
        return false;

    auto net_str = pattern.substr(0, slash);
    auto bits_str = pattern.substr(slash + 1);
    int bits = 0;
    std::from_chars(bits_str.data(), bits_str.data() + bits_str.size(), bits);
    if (bits < 0 || bits > 32) return false;

    // 解析 IPv4 为 uint32
    auto parse_ipv4 = [](std::string_view s) -> std::optional<std::uint32_t> {
        std::uint32_t result = 0;
        int octet_count = 0;
        std::size_t pos = 0;
        while (pos <= s.size() && octet_count < 4) {
            auto dot = s.find('.', pos);
            if (dot == std::string_view::npos) dot = s.size();
            int val = 0;
            auto [ptr, ec] = std::from_chars(
                s.data() + pos, s.data() + dot, val);
            if (ec != std::errc{} || val < 0 || val > 255) return std::nullopt;
            result = (result << 8) | static_cast<std::uint32_t>(val);
            ++octet_count;
            pos = dot + 1;
        }
        return (octet_count == 4) ? std::optional{result} : std::nullopt;
    };

    auto client = parse_ipv4(client_ip);
    auto network = parse_ipv4(net_str);
    if (!client || !network) return false;

    if (bits == 0) return true;
    std::uint32_t mask = ~std::uint32_t{0} << (32 - bits);
    return (*client & mask) == (*network & mask);
}

/// 检查 IP 是否在列表中（支持 CIDR）
inline auto ip_in_list(std::string_view ip,
                       const std::vector<std::string>& list) -> bool
{
    for (auto& pattern : list)
        if (ip_matches(ip, pattern)) return true;
    return false;
}

} // namespace detail

// =============================================================================
// ip_filter — IP 过滤中间件
// =============================================================================
//
// 行为:
//   白名单非空时: IP 在列表内 → 放行, 否则 → denied_status
//   白名单为空 + 黑名单非空时: IP 在列表内 → denied_status, 否则 → 放行
//   两个都为空: 直接放行

export inline auto ip_filter(ip_filter_options opts = {}) -> http::middleware_fn
{
    return [opts = std::move(opts)]
           (http::request_context& ctx, http::next_fn next) -> task<void>
    {
        auto client_ip = detail::resolve_ip(ctx);

        bool denied = false;

        if (!opts.allow_list.empty()) {
            // 白名单模式: 不在列表内则拒绝
            if (!detail::ip_in_list(client_ip, opts.allow_list))
                denied = true;
        } else if (!opts.deny_list.empty()) {
            // 黑名单模式: 在列表内则拒绝
            if (detail::ip_in_list(client_ip, opts.deny_list))
                denied = true;
        }

        if (denied) {
            logger::warn("{} {} blocked IP: {}",
                         ctx.method(), ctx.path(), client_ip);
            ctx.json(opts.denied_status, std::format(
                R"({{"error":"access denied","ip":"{}"}})", client_ip));
            co_return;
        }

        co_await next();
    };
}

} // namespace cnetmod
