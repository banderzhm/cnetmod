export module cnetmod.protocol.http.middleware.compress;

import std;
import cnetmod.protocol.http;

export namespace cnetmod {
struct compress_options {
  std::size_t min_size = 1024;
  int level = 6;
};
[[nodiscard]] auto compress(compress_options opts = {}) -> http::middleware_fn;
} // namespace cnetmod
