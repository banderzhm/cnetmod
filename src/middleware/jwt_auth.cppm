/**
 * @file jwt_auth.cppm
 * @brief JWT / Bearer Token 认证中间件
 *
 * 从 Authorization 头提取 Bearer token，调用用户提供的验证函数。
 * 验证逻辑可插拔：支持 jwt-cpp、自定义 HMAC、API Key 等任意方案。
 *
 * 使用示例:
 *   import cnetmod.middleware.jwt_auth;
 *
 *   // 简单 API Key 验证
 *   svr.use(jwt_auth({
 *       .verify = [](std::string_view token) { return token == "my-secret"; },
 *       .skip_paths = {"/", "/login"},
 *   }));
 *
 *   // 使用 jwt-cpp 验证 HS256 (在应用层 #include <jwt-cpp/jwt.h>)
 *   svr.use(jwt_auth({
 *       .verify = [](std::string_view token) {
 *           try {
 *               auto decoded = jwt::decode(std::string(token));
 *               auto verifier = jwt::verify()
 *                   .allow_algorithm(jwt::algorithm::hs256{"secret"})
 *                   .with_issuer("myapp");
 *               verifier.verify(decoded);
 *               return true;
 *           } catch (...) { return false; }
 *       },
 *       .skip_paths = {"/", "/login", "/register"},
 *   }));
 */
export module cnetmod.middleware.jwt_auth;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

// =============================================================================
// generate_secure_token — CSPRNG 安全令牌（hex 编码）
// =============================================================================

/// 生成密码学安全的随机令牌（hex 编码）
/// MSVC 底层使用 BCryptGenRandom，GCC/Clang 使用 /dev/urandom
export inline auto generate_secure_token(std::size_t bytes = 32) -> std::string {
    static thread_local std::random_device rd;
    static constexpr char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i) {
        auto byte = static_cast<std::uint8_t>(rd() & 0xFF);
        token.push_back(hex[(byte >> 4) & 0x0F]);
        token.push_back(hex[byte & 0x0F]);
    }
    return token;
}

// =============================================================================
// jwt_auth_options — JWT 认证配置
// =============================================================================

export struct jwt_auth_options {
    /// Token 验证函数: 返回 true 表示合法
    std::function<bool(std::string_view token)> verify;

    /// 跳过认证的路径 (精确匹配或前缀匹配 path + "/")
    std::vector<std::string> skip_paths;

    /// 读取 token 的请求头 (默认 Authorization)
    std::string header_name = "Authorization";

    /// Token 前缀 (默认 "Bearer ")，设为空则直接取整个 header 值
    std::string token_prefix = "Bearer ";
};

// =============================================================================
// jwt_auth — Bearer Token 认证中间件
// =============================================================================
//
// 流程:
//   1. 检查 skip_paths → 命中则直接放行
//   2. 读 Authorization 头 → 为空则 401
//   3. 去除 "Bearer " 前缀 → 格式不对则 401
//   4. 调用 verify(token) → false 则 401
//   5. 通过 → 调用 next()

export inline auto jwt_auth(jwt_auth_options opts) -> http::middleware_fn
{
    return [opts = std::move(opts)]
           (http::request_context& ctx, http::next_fn next) -> task<void>
    {
        // 跳过指定路径
        auto path = ctx.path();
        for (auto& skip : opts.skip_paths) {
            if (path == skip
                || (!skip.empty() && skip != "/"
                    && path.starts_with(skip)
                    && (path.size() == skip.size()
                        || path[skip.size()] == '/')))
            {
                co_await next();
                co_return;
            }
        }

        // 提取 token
        auto auth = ctx.get_header(opts.header_name);
        if (auth.empty()) {
            ctx.json(http::status::unauthorized,
                R"({"error":"missing authorization header"})");
            co_return;
        }

        std::string_view token = auth;
        if (!opts.token_prefix.empty()) {
            if (!auth.starts_with(opts.token_prefix)) {
                ctx.json(http::status::unauthorized,
                    R"({"error":"invalid authorization format"})");
                co_return;
            }
            token = auth.substr(opts.token_prefix.size());
        }

        // 验证
        if (!opts.verify || !opts.verify(token)) {
            ctx.json(http::status::unauthorized,
                R"({"error":"invalid or expired token"})");
            co_return;
        }

        co_await next();
    };
}

} // namespace cnetmod
