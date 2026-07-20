export module cnetmod.protocol.http.middleware.upload;

import std;
import cnetmod.protocol.http;

export namespace cnetmod {
struct upload_config {
  std::size_t max_file_size = 10 * 1024 * 1024;
  std::size_t max_total_size = 0;
  std::size_t max_files = 0;
  std::size_t max_fields = 0;
  std::vector<std::string> allowed_types;
  std::vector<std::string> allowed_exts;
};
[[nodiscard]] auto upload(upload_config cfg = {}) -> http::middleware_fn;
} // namespace cnetmod
