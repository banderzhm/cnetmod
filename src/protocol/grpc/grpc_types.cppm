module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.types;

import std;
import cnetmod.protocol.http;

namespace cnetmod::grpc {

export enum class status_code : int {
  ok = 0,
  cancelled = 1,
  unknown = 2,
  invalid_argument = 3,
  deadline_exceeded = 4,
  not_found = 5,
  already_exists = 6,
  permission_denied = 7,
  resource_exhausted = 8,
  failed_precondition = 9,
  aborted = 10,
  out_of_range = 11,
  unimplemented = 12,
  internal = 13,
  unavailable = 14,
  data_loss = 15,
  unauthenticated = 16
};
export using byte_buffer = std::vector<std::byte>;
export using metadata = std::multimap<std::string, std::string>;
export inline constexpr std::size_t default_max_message_bytes =
    4u * 1024u * 1024u;
export inline constexpr std::size_t default_max_metadata_bytes = 8u * 1024u;
export enum class compression_algorithm { identity, gzip };

export struct status {
  status_code code = status_code::ok;
  std::string message;
  metadata trailers;
  [[nodiscard]] auto ok() const noexcept -> bool;
};
export struct message_frame {
  bool compressed = false;
  byte_buffer payload;
};
export struct call_options {
  metadata headers;
  std::chrono::milliseconds timeout{0};
  compression_algorithm compression = compression_algorithm::identity;
};
export struct call_context {
  std::string service;
  std::string method;
  std::string path;
  metadata headers;
  std::chrono::milliseconds timeout{0};
  std::chrono::steady_clock::time_point started =
      std::chrono::steady_clock::now();
  [[nodiscard]] auto deadline_exceeded() const noexcept -> bool;
};
export struct unary_request {
  std::string service;
  std::string method;
  metadata headers;
  byte_buffer payload;
  std::chrono::milliseconds timeout{0};
  compression_algorithm compression = compression_algorithm::identity;
};
export struct unary_response {
  status st;
  metadata headers;
  byte_buffer payload;
};
export struct streaming_request {
  std::string service;
  std::string method;
  metadata headers;
  std::vector<byte_buffer> messages;
  std::chrono::milliseconds timeout{0};
  compression_algorithm compression = compression_algorithm::identity;
};
export struct streaming_response {
  status st;
  metadata headers;
  std::vector<byte_buffer> messages;
};
export enum class call_kind {
  unary,
  client_streaming,
  server_streaming,
  bidi_streaming
};

export auto service_path(std::string_view service, std::string_view method)
    -> std::string;
export auto make_status(status_code code, std::string message = {},
                        metadata trailers = {}) -> status;
export auto compression_name(compression_algorithm algorithm)
    -> std::string_view;
export auto compression_from_header(std::string_view text)
    -> std::optional<compression_algorithm>;
export auto accepts_compression(std::string_view header,
                                compression_algorithm algorithm) -> bool;
export auto parse_service_path(std::string_view path)
    -> std::optional<std::pair<std::string, std::string>>;
export auto metadata_from_headers(const http::header_map &headers) -> metadata;
export auto header_value(const http::header_map &headers, std::string_view name)
    -> std::string_view;
export auto metadata_value(const metadata &md, std::string_view name)
    -> std::string_view;
export auto metadata_wire_size(const metadata &md) noexcept -> std::size_t;
export void append_metadata_headers(http::request &req, const metadata &md);
export void append_metadata_headers(http::response &resp, const metadata &md);
export void append_metadata_trailers(http::response &resp, const metadata &md);
export auto status_from_headers(const http::header_map &headers) -> status;
export auto status_from_response(const http::response &resp) -> status;
export auto timeout_from_headers(const http::header_map &headers)
    -> std::chrono::milliseconds;
export auto format_timeout(std::chrono::milliseconds timeout) -> std::string;
export void add_binary_metadata(metadata &md, std::string key,
                                std::span<const std::byte> value);
export auto get_binary_metadata(const metadata &md, std::string_view key)
    -> std::vector<byte_buffer>;

} // namespace cnetmod::grpc
