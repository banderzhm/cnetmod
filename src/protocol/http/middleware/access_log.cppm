export module cnetmod.protocol.http.middleware.access_log;

import std;
import cnetmod.protocol.http;
import cnetmod.protocol.websocket;
import cnetmod.core.log;

export namespace cnetmod {

enum class access_log_format : std::uint8_t { brief, http };
enum class access_log_dump : std::uint8_t { error_only, always };

struct access_log_options {
  logger::level lv = logger::level::info;
  access_log_format format = access_log_format::brief;
  access_log_dump dump = access_log_dump::error_only;
  bool log_request_headers = true;
  bool log_request_body = true;
  bool log_response_headers = true;
  bool log_response_body = true;
  std::size_t max_body_bytes = 2048;
  bool redact_sensitive_headers = true;
};

[[nodiscard]] auto
access_log(access_log_options opts,
           std::source_location loc = std::source_location::current())
    -> http::middleware_fn;
[[nodiscard]] auto
access_log(logger::level lv = logger::level::info,
           std::source_location loc = std::source_location::current())
    -> http::middleware_fn;
[[nodiscard]] auto
ws_access_log(ws::ws_handler_fn handler, logger::level lv = logger::level::info,
              std::source_location loc = std::source_location::current())
    -> ws::ws_handler_fn;
} // namespace cnetmod
