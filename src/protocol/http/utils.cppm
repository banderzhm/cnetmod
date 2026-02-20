/**
 * @file utils.cppm
 * @brief HTTP 通用工具函数
 *
 * 提供在多个中间件与 handler 中共用的 HTTP 辅助函数，
 * 避免各处重复实现相同逻辑。
 *
 * 函数列表:
 *   resolve_client_ip  — 从反向代理头解析客户端真实 IP
 *   parse_query_param  — 从 query string 提取单个参数值
 *   parse_query_params — 将 query string 完整解析为 key-value map
 *
 * 使用示例:
 *   import cnetmod.protocol.http;
 *
 *   // 解析真实 IP（支持 X-Forwarded-For / X-Real-IP）
 *   auto ip = http::resolve_client_ip(ctx);
 *
 *   // 提取 query 参数
 *   auto type = http::parse_query_param(ctx.query_string(), "type");
 *
 *   // 解析全部参数
 *   auto params = http::parse_query_params(ctx.query_string());
 */
export module cnetmod.protocol.http:utils;

import std;
import :router;   // request_context

namespace cnetmod::http {

// =============================================================================
// resolve_client_ip — 解析客户端真实 IP
// =============================================================================
//
// 优先级（从高到低）：
//   1. X-Forwarded-For  最左侧 IP（原始客户端），去除首尾空格
//   2. X-Real-IP
//   3. fallback（默认 "unknown"）
//
// 适用场景：Nginx / Cloudflare 等反向代理前置时获取真实来源 IP。

export inline auto resolve_client_ip(
    const request_context& ctx,
    std::string_view fallback = "unknown") -> std::string
{
    // X-Forwarded-For: client, proxy1, proxy2
    if (auto xff = ctx.get_header("X-Forwarded-For"); !xff.empty()) {
        auto comma = xff.find(',');
        auto ip = (comma != std::string_view::npos) ? xff.substr(0, comma) : xff;
        while (!ip.empty() && ip.front() == ' ') ip.remove_prefix(1);
        while (!ip.empty() && ip.back()  == ' ') ip.remove_suffix(1);
        if (!ip.empty()) return std::string(ip);
    }
    if (auto xri = ctx.get_header("X-Real-IP"); !xri.empty())
        return std::string(xri);
    return std::string(fallback);
}

// =============================================================================
// parse_query_param — 从 query string 提取单个参数值
// =============================================================================
//
// 正确处理边界：避免 "mykey=v" 被 "key=" 误匹配。
//
// 示例:
//   parse_query_param("type=send_email&params=to%40x.com", "params")
//   → "to%40x.com"
//
// 注意: 不做 URL 解码，调用方按需处理 %xx 转义。

export inline auto parse_query_param(
    std::string_view query,
    std::string_view key) -> std::string
{
    std::size_t pos = 0;
    while (pos < query.size()) {
        auto amp = query.find('&', pos);
        auto seg = (amp != std::string_view::npos)
                   ? query.substr(pos, amp - pos)
                   : query.substr(pos);

        auto eq = seg.find('=');
        if (eq != std::string_view::npos && seg.substr(0, eq) == key)
            return std::string(seg.substr(eq + 1));

        if (amp == std::string_view::npos) break;
        pos = amp + 1;
    }
    return {};
}

// =============================================================================
// parse_query_params — 将 query string 完整解析为 key-value map
// =============================================================================
//
// 示例:
//   parse_query_params("a=1&b=hello&c=")
//   → {{"a","1"}, {"b","hello"}, {"c",""}}
//
// 注意: 重复 key 时，后者覆盖前者。不做 URL 解码。

export inline auto parse_query_params(std::string_view query)
    -> std::unordered_map<std::string, std::string>
{
    std::unordered_map<std::string, std::string> result;
    std::size_t pos = 0;
    while (pos < query.size()) {
        auto amp = query.find('&', pos);
        auto seg = (amp != std::string_view::npos)
                   ? query.substr(pos, amp - pos)
                   : query.substr(pos);

        auto eq = seg.find('=');
        if (eq != std::string_view::npos)
            result.insert_or_assign(std::string(seg.substr(0, eq)),
                                    std::string(seg.substr(eq + 1)));

        if (amp == std::string_view::npos) break;
        pos = amp + 1;
    }
    return result;
}

} // namespace cnetmod::http
