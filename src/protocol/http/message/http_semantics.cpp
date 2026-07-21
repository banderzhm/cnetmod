module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.http;

import std;
import cnetmod.coro.task;
import :semantics;

namespace cnetmod::http {

request_body_stream::request_body_stream(std::size_t capacity)
    : chunks_(capacity) {}

auto request_body_stream::receive() -> task<std::optional<request_body_chunk>> {
  co_return co_await chunks_.receive();
}

auto request_body_stream::try_receive() -> std::optional<request_body_chunk> {
  return chunks_.try_receive();
}

auto request_body_stream::push(request_body_chunk chunk) -> bool {
  return chunks_.try_send(std::move(chunk));
}

void request_body_stream::close() noexcept { chunks_.close(); }

auto request_body_stream::is_closed() const noexcept -> bool {
  return chunks_.is_closed();
}

auto case_insensitive_less::operator()(std::string_view left,
                                       std::string_view right) const noexcept
    -> bool {
  const auto length = std::min(left.size(), right.size());
  for (std::size_t index = 0; index < length; ++index) {
    const auto lhs = static_cast<unsigned char>(left[index]);
    const auto rhs = static_cast<unsigned char>(right[index]);
    const auto normalized_lhs = lhs >= 'A' && lhs <= 'Z' ? lhs + 32 : lhs;
    const auto normalized_rhs = rhs >= 'A' && rhs <= 'Z' ? rhs + 32 : rhs;
    if (normalized_lhs != normalized_rhs)
      return normalized_lhs < normalized_rhs;
  }
  return left.size() < right.size();
}

auto url::parse(std::string_view input) -> std::expected<url, std::string> {
  url result;
  const auto scheme_end = input.find("://");
  if (scheme_end == std::string_view::npos)
    return std::unexpected("missing scheme");
  result.scheme = input.substr(0, scheme_end);
  auto remainder = input.substr(scheme_end + 3);

  if (const auto fragment = remainder.find('#');
      fragment != std::string_view::npos) {
    result.fragment = remainder.substr(fragment + 1);
    remainder = remainder.substr(0, fragment);
  }
  if (const auto query = remainder.find('?'); query != std::string_view::npos) {
    result.query = remainder.substr(query + 1);
    remainder = remainder.substr(0, query);
  }
  if (const auto path = remainder.find('/'); path != std::string_view::npos) {
    result.path = remainder.substr(path);
    remainder = remainder.substr(0, path);
  } else {
    result.path = "/";
  }

  std::string_view port_text;
  if (remainder.starts_with('[')) {
    const auto bracket = remainder.find(']');
    if (bracket == std::string_view::npos)
      return std::unexpected("unclosed IPv6 bracket");
    result.host = remainder.substr(1, bracket - 1);
    if (bracket + 1 < remainder.size() && remainder[bracket + 1] == ':')
      port_text = remainder.substr(bracket + 2);
  } else if (const auto colon = remainder.rfind(':');
             colon != std::string_view::npos) {
    result.host = remainder.substr(0, colon);
    port_text = remainder.substr(colon + 1);
  } else {
    result.host = remainder;
  }
  if (!port_text.empty()) {
    const auto [_, error] = std::from_chars(
        port_text.data(), port_text.data() + port_text.size(), result.port);
    if (error != std::errc{})
      return std::unexpected("invalid port");
  }
  if (result.port == 0)
    result.port = result.scheme == "https" || result.scheme == "wss" ? 443 : 80;
  return result;
}

auto format_authority_host(std::string_view host) -> std::string {
  if (host.empty())
    return {};
  if (host.front() == '[' || !host.contains(':'))
    return std::string(host);
  return std::format("[{}]", host);
}

auto format_authority(std::string_view host, std::uint16_t port, bool use_ssl)
    -> std::string {
  auto result = format_authority_host(host);
  if (!((use_ssl && port == 443) || (!use_ssl && port == 80)))
    result += std::format(":{}", port);
  return result;
}

namespace detail {

class http_error_category_impl final : public std::error_category {
public:
  auto name() const noexcept -> const char * override { return "http"; }
  auto message(int value) const -> std::string override {
    switch (static_cast<http_errc>(value)) {
    case http_errc::success:
      return "success";
    case http_errc::invalid_method:
      return "invalid HTTP method";
    case http_errc::invalid_uri:
      return "invalid URI";
    case http_errc::invalid_version:
      return "invalid HTTP version";
    case http_errc::invalid_status_line:
      return "invalid status line";
    case http_errc::invalid_header:
      return "invalid header";
    case http_errc::header_too_large:
      return "header too large";
    case http_errc::body_too_large:
      return "body too large";
    case http_errc::incomplete_message:
      return "incomplete message";
    case http_errc::need_more_data:
      return "need more data";
    case http_errc::invalid_chunk:
      return "invalid chunk encoding";
    case http_errc::invalid_multipart:
      return "invalid multipart data";
    case http_errc::missing_boundary:
      return "missing multipart boundary";
    case http_errc::invalid_content_disposition:
      return "invalid Content-Disposition";
    case http_errc::unsupported_encoding:
      return "unsupported Content-Transfer-Encoding";
    case http_errc::invalid_url_encoding:
      return "invalid URL encoding";
    }
    return "unknown http error";
  }
};

auto http_category_instance() -> const std::error_category & {
  static const http_error_category_impl instance;
  return instance;
}

} // namespace detail

auto make_error_code(http_errc value) noexcept -> std::error_code {
  return {static_cast<int>(value), detail::http_category_instance()};
}

} // namespace cnetmod::http
