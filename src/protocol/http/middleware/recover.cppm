export module cnetmod.protocol.http.middleware.recover;

import std;
import cnetmod.protocol.http;

export namespace cnetmod {
struct recover_options {
  bool log_body = false;
  std::size_t max_body_bytes = 512;
  bool allow_env_override = true;
};
[[nodiscard]] auto recover(recover_options opts = {}) -> http::middleware_fn;
} // namespace cnetmod
