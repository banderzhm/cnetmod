/**
 * @file compress.cppm
 * @brief gzip 响应压缩中间件 — 自动压缩文本类响应
 *
 * 检查 Accept-Encoding，对文本类 MIME 且 body 超过阈值的响应进行 gzip 压缩。
 * 需要 zlib 支持（CNETMOD_HAS_ZLIB），未启用时退化为 pass-through。
 *
 * 使用示例:
 *   import cnetmod.middleware.compress;
 *
 *   svr.use(compress());                             // 默认: 1KB 阈值
 *   svr.use(compress({.min_size = 256}));             // 自定义阈值
 *   svr.use(compress({.level = 6}));                  // 压缩等级 1-9
 */
module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_ZLIB
#include <zlib.h>
#endif

export module cnetmod.middleware.compress;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

// =============================================================================
// compress_options — 压缩配置
// =============================================================================

export struct compress_options {
    std::size_t min_size = 1024;   ///< 响应 body 小于此值不压缩 (bytes)
    int level = 6;                 ///< gzip 压缩等级 (1=fastest, 9=best, 6=default)
};

// =============================================================================
// 内部工具
// =============================================================================

namespace detail {

/// 判断 Content-Type 是否为可压缩的文本类型
inline auto is_compressible(std::string_view content_type) -> bool {
    // text/* 全部可压缩
    if (content_type.starts_with("text/")) return true;
    // JSON / XML / JavaScript / SVG
    if (content_type.find("json") != std::string_view::npos) return true;
    if (content_type.find("xml") != std::string_view::npos) return true;
    if (content_type.find("javascript") != std::string_view::npos) return true;
    if (content_type.find("svg") != std::string_view::npos) return true;
    return false;
}

/// 检查 Accept-Encoding 是否包含 gzip
inline auto accepts_gzip(std::string_view accept_encoding) -> bool {
    // 简单匹配: 包含 "gzip" 即可
    return accept_encoding.find("gzip") != std::string_view::npos;
}

#ifdef CNETMOD_HAS_ZLIB

/// gzip 压缩 (zlib)
inline auto gzip_compress(std::string_view input, int level)
    -> std::optional<std::string>
{
    // gzip 压缩上限估算
    auto bound = ::compressBound(static_cast<uLong>(input.size()));
    // gzip 头尾额外开销
    std::vector<Bytef> out(bound + 32);

    z_stream strm{};
    // windowBits = 15 + 16 启用 gzip 格式
    if (deflateInit2(&strm, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return std::nullopt;

    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    strm.avail_in = static_cast<uInt>(input.size());
    strm.next_out = out.data();
    strm.avail_out = static_cast<uInt>(out.size());

    auto ret = deflate(&strm, Z_FINISH);
    deflateEnd(&strm);

    if (ret != Z_STREAM_END)
        return std::nullopt;

    auto compressed_size = out.size() - strm.avail_out;
    return std::string(reinterpret_cast<const char*>(out.data()), compressed_size);
}

#endif // CNETMOD_HAS_ZLIB

} // namespace detail

// =============================================================================
// compress — gzip 响应压缩中间件
// =============================================================================
//
// 流程:
//   1. 执行 next() 获得 handler 响应
//   2. 检查: Accept-Encoding 含 gzip? Content-Type 可压缩? body > min_size?
//   3. 全部满足 → gzip 压缩 body, 设置 Content-Encoding: gzip
//   4. 任一不满足 → 直接放行

export inline auto compress(compress_options opts = {}) -> http::middleware_fn
{
    return [opts](http::request_context& ctx, http::next_fn next) -> task<void> {
        co_await next();

#ifdef CNETMOD_HAS_ZLIB
        // 检查客户端是否接受 gzip
        auto ae = ctx.get_header("Accept-Encoding");
        if (!detail::accepts_gzip(ae))
            co_return;

        // 检查响应是否已经压缩
        auto ce = ctx.resp().get_header("Content-Encoding");
        if (!ce.empty())
            co_return;

        // 检查 Content-Type 是否可压缩
        auto ct = ctx.resp().get_header("Content-Type");
        if (!detail::is_compressible(ct))
            co_return;

        // 检查 body 大小
        auto body = ctx.resp().body();
        if (body.size() < opts.min_size)
            co_return;

        // 执行压缩
        auto compressed = detail::gzip_compress(body, opts.level);
        if (!compressed || compressed->size() >= body.size())
            co_return;  // 压缩无效果

        // 替换 body
        ctx.resp().set_body(std::move(*compressed));
        ctx.resp().set_header("Content-Encoding", "gzip");
        ctx.resp().set_header("Content-Length",
            std::to_string(ctx.resp().body().size()));
        // Vary: Accept-Encoding — 通知缓存代理
        ctx.resp().set_header("Vary", "Accept-Encoding");
#else
        // zlib 未启用，pass-through
        (void)opts;
#endif
    };
}

} // namespace cnetmod
