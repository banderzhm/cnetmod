module;

#include <cnetmod/config.hpp>
#ifdef CNETMOD_HAS_ZLIB
#include <zlib.h>
#endif

module cnetmod.protocol.http.middleware.compress;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {
namespace {
auto is_compressible(std::string_view content_type) -> bool {
  return content_type.starts_with("text/") ||
         content_type.find("json") != std::string_view::npos ||
         content_type.find("xml") != std::string_view::npos ||
         content_type.find("javascript") != std::string_view::npos ||
         content_type.find("svg") != std::string_view::npos;
}
auto accepts_gzip(std::string_view accept_encoding) -> bool {
  return accept_encoding.find("gzip") != std::string_view::npos;
}
#ifdef CNETMOD_HAS_ZLIB
auto gzip_compress(std::string_view input, int level)
    -> std::optional<std::string> {
  const auto bound = ::compressBound(static_cast<uLong>(input.size()));
  std::vector<Bytef> output(bound + 32);
  z_stream stream{};
  if (deflateInit2(&stream, level, Z_DEFLATED, 15 + 16, 8,
                   Z_DEFAULT_STRATEGY) != Z_OK)
    return std::nullopt;
  stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.data()));
  stream.avail_in = static_cast<uInt>(input.size());
  stream.next_out = output.data();
  stream.avail_out = static_cast<uInt>(output.size());
  const auto result = deflate(&stream, Z_FINISH);
  deflateEnd(&stream);
  if (result != Z_STREAM_END)
    return std::nullopt;
  return std::string(reinterpret_cast<const char *>(output.data()),
                     output.size() - stream.avail_out);
}
#endif
auto compress_response(compress_options opts, http::request_context &ctx,
                       http::next_fn next) -> task<void> {
  co_await next();
#ifdef CNETMOD_HAS_ZLIB
  if (!accepts_gzip(ctx.get_header("Accept-Encoding")) ||
      !ctx.resp().get_header("Content-Encoding").empty())
    co_return;
  const auto content_type = ctx.resp().get_header("Content-Type");
  if (!is_compressible(content_type))
    co_return;
  const auto body = ctx.resp().body();
  if (body.size() < opts.min_size)
    co_return;
  const auto compressed = gzip_compress(body, opts.level);
  if (!compressed || compressed->size() >= body.size())
    co_return;
  ctx.resp().set_body(*compressed);
  ctx.resp().set_header("Content-Encoding", "gzip");
  ctx.resp().set_header("Content-Length",
                        std::to_string(ctx.resp().body().size()));
  ctx.resp().set_header("Vary", "Accept-Encoding");
#else
  (void)opts;
#endif
}
} // namespace
auto compress(compress_options opts) -> http::middleware_fn {
  return [opts](http::request_context &ctx, http::next_fn next) -> task<void> {
    return compress_response(opts, ctx, std::move(next));
  };
}
} // namespace cnetmod
