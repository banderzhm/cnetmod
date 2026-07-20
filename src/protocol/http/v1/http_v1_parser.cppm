module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.http:parser;
import std;
import :semantics;

namespace cnetmod::http {
namespace detail {
auto find_crlf(const char *data, std::size_t size) noexcept -> std::size_t;
auto trim(std::string_view value) noexcept -> std::string_view;
} // namespace detail

export class request_parser {
public:
  request_parser() = default;
  [[nodiscard]] auto consume(const char *, std::size_t)
      -> std::expected<std::size_t, std::error_code>;
  [[nodiscard]] auto ready() const noexcept -> bool;
  [[nodiscard]] auto method() const noexcept -> std::string_view;
  [[nodiscard]] auto method_enum() const noexcept -> std::optional<http_method>;
  [[nodiscard]] auto uri() const noexcept -> std::string_view;
  [[nodiscard]] auto version() const noexcept -> http_version;
  [[nodiscard]] auto headers() const noexcept -> const header_map &;
  [[nodiscard]] auto body() const noexcept -> std::string_view;
  [[nodiscard]] auto get_header(std::string_view) const -> std::string_view;
  void reset() noexcept;

private:
  enum class state { request_line, headers, body };

  auto parse_request_line(std::string_view)
      -> std::expected<void, std::error_code>;
  auto parse_header_line(std::string_view)
      -> std::expected<void, std::error_code>;
  auto prepare_body() -> bool;
  auto process_chunked_body() -> std::expected<bool, std::error_code>;
  std::string buf_, method_, uri_, body_;
  http_version version_ = http_version::http_1_1;
  header_map headers_;
  state state_ = state::request_line;
  std::size_t header_bytes_ = 0, body_bytes_remaining_ = 0;
  bool chunked_ = false, ready_ = false;
};

export class response_parser {
public:
  response_parser() = default;
  [[nodiscard]] auto consume(const char *, std::size_t)
      -> std::expected<std::size_t, std::error_code>;
  [[nodiscard]] auto ready() const noexcept -> bool;
  [[nodiscard]] auto version() const noexcept -> http_version;
  [[nodiscard]] auto status_code() const noexcept -> int;
  [[nodiscard]] auto status_message() const noexcept -> std::string_view;
  [[nodiscard]] auto headers() const noexcept -> const header_map &;
  [[nodiscard]] auto body() const noexcept -> std::string_view;
  [[nodiscard]] auto get_header(std::string_view) const -> std::string_view;
  void reset() noexcept;

private:
  enum class state { status_line, headers, body };

  auto parse_status_line(std::string_view)
      -> std::expected<void, std::error_code>;
  auto parse_header_line(std::string_view)
      -> std::expected<void, std::error_code>;
  auto prepare_body() -> bool;
  auto process_chunked_body() -> std::expected<bool, std::error_code>;
  std::string buf_, status_msg_, body_;
  http_version version_ = http_version::http_1_1;
  int status_code_ = 0;
  header_map headers_;
  state state_ = state::status_line;
  std::size_t header_bytes_ = 0, body_bytes_remaining_ = 0;
  bool chunked_ = false, ready_ = false;
};
} // namespace cnetmod::http
