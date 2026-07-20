module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http:response;
// Protocol-qualified filename prevents duplicate source basenames.

import std;
import :semantics;
import :cookie;

namespace cnetmod::http {

// =============================================================================
// response — HTTP Response Builder
// =============================================================================

export class response {
public:
  response();

  explicit response(int status_code,
                    http_version version = http_version::http_1_1);

  // --- Settings ---

  auto set_status(int code) noexcept -> response &;
  auto set_status_message(std::string_view msg) -> response &;
  auto set_version(http_version v) noexcept -> response &;

  auto set_header(std::string_view key, std::string_view value) -> response &;

  auto append_header(std::string_view key, std::string_view value)
      -> response &;

  auto set_trailer(std::string_view key, std::string_view value) -> response &;

  auto append_trailer(std::string_view key, std::string_view value)
      -> response &;

  auto remove_header(std::string_view key) -> response &;

  auto set_body(std::string_view body) -> response &;

  auto set_body(std::string body) -> response &;

  auto set_body_preserve_headers(std::string body) -> response &;

  /// Set cookie (simplified interface)
  auto set_cookie(std::string_view name, std::string_view value,
                  std::string_view domain = {}, std::string_view path = "/",
                  std::optional<std::chrono::seconds> max_age = std::nullopt,
                  bool secure = false, bool http_only = false) -> response &;

  /// Set cookie (full control)
  auto set_cookie(const cookie &c) -> response &;

  // --- Access ---

  [[nodiscard]] auto status_code() const noexcept -> int;
  [[nodiscard]] auto version() const noexcept -> http_version;
  [[nodiscard]] auto headers() const noexcept -> const header_map &;
  [[nodiscard]] auto trailers() const noexcept -> const header_map &;
  [[nodiscard]] auto body() const noexcept -> std::string_view;

  [[nodiscard]] auto take_body() noexcept -> std::string;

  [[nodiscard]] auto get_header(std::string_view key) const -> std::string_view;

  // --- Serialization ---

  /// Serialize to complete HTTP response string
  [[nodiscard]] auto serialize() const -> std::string;

private:
  int status_code_ = 200;
  std::string status_msg_;
  http_version version_ = http_version::http_1_1;
  header_map headers_;
  header_map trailers_;
  std::string body_;
};

} // namespace cnetmod::http
