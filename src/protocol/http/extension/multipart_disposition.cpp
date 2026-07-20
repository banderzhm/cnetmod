module cnetmod.protocol.http;

import std;
import :multipart_disposition;
import :multipart_url;

namespace cnetmod::http {
namespace {
auto skip_ows(std::string_view s) noexcept -> std::string_view {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    s.remove_prefix(1);
  return s;
}
auto token(std::string_view s)
    -> std::pair<std::string_view, std::string_view> {
  std::size_t i{};
  while (i < s.size()) {
    auto c = s[i];
    if ((c >= '!' && c <= '~') && std::string_view{"\"(),/:;<=>?@[\\]{}"}.find(
                                      c) == std::string_view::npos)
      ++i;
    else
      break;
  }
  return {s.substr(0, i), s.substr(i)};
}
auto lower(std::string_view v) -> std::string {
  std::string r(v);
  for (auto &c : r)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c + 32);
  return r;
}
auto quoted(std::string_view s) -> std::pair<std::string, std::string_view> {
  if (s.empty() || s.front() != '\"')
    return {{}, s};
  s.remove_prefix(1);
  std::string r;
  while (!s.empty()) {
    if (s.front() == '\"') {
      s.remove_prefix(1);
      break;
    }
    if (s.front() == '\\' && s.size() > 1) {
      r += s[1];
      s.remove_prefix(2);
    } else {
      r += s.front();
      s.remove_prefix(1);
    }
  }
  return {std::move(r), s};
}
auto decode_ext(std::string_view value) -> std::string {
  const auto one = value.find('\'');
  if (one == std::string_view::npos)
    return std::string(value);
  const auto two = value.find('\'', one + 1);
  return two == std::string_view::npos
             ? std::string(value)
             : url_decode(value.substr(two + 1), false);
}
} // namespace
auto content_disposition::effective_filename() const noexcept
    -> std::string_view {
  return filename_star.empty() ? filename : filename_star;
}
auto content_disposition::has_filename() const noexcept -> bool {
  return !filename.empty() || !filename_star.empty();
}
auto parse_content_disposition(std::string_view header) -> content_disposition {
  content_disposition result;
  header = skip_ows(header);
  auto [kind, rest] = token(header);
  result.type = lower(kind);
  while (!rest.empty()) {
    rest = skip_ows(rest);
    if (rest.empty() || rest.front() != ';')
      break;
    rest.remove_prefix(1);
    rest = skip_ows(rest);
    auto [name, after] = token(rest);
    rest = after;
    if (name.empty() || rest.empty() || rest.front() != '=')
      continue;
    rest.remove_prefix(1);
    auto key = lower(name);
    if (key == "filename*") {
      const auto end = rest.find(';');
      auto raw = rest.substr(0, end);
      while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t'))
        raw.remove_suffix(1);
      result.filename_star = decode_ext(raw);
      rest =
          end == std::string_view::npos ? std::string_view{} : rest.substr(end);
      continue;
    }
    std::string value;
    if (!rest.empty() && rest.front() == '\"') {
      auto [q, next] = quoted(rest);
      value = std::move(q);
      rest = next;
    } else {
      auto [v, next] = token(rest);
      value = std::string(v);
      rest = next;
    }
    if (key == "name")
      result.name = std::move(value);
    else if (key == "filename")
      result.filename = std::move(value);
  }
  return result;
}
} // namespace cnetmod::http
