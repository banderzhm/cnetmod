module cnetmod.protocol.http;

import std;
import :multipart_content_type;

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
    const char c = s[i];
    if ((c >= '!' && c <= '~') && std::string_view{"\"(),/:;<=>?@[\\]{}"}.find(
                                      c) == std::string_view::npos)
      ++i;
    else
      break;
  }
  return {s.substr(0, i), s.substr(i)};
}
auto lower(std::string_view input) -> std::string {
  std::string out(input);
  for (auto &c : out)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c + 32);
  return out;
}
auto quoted(std::string_view s) -> std::pair<std::string, std::string_view> {
  if (s.empty() || s.front() != '\"')
    return {{}, s};
  s.remove_prefix(1);
  std::string out;
  while (!s.empty()) {
    if (s.front() == '\"') {
      s.remove_prefix(1);
      break;
    }
    if (s.front() == '\\' && s.size() > 1) {
      out += s[1];
      s.remove_prefix(2);
    } else {
      out += s.front();
      s.remove_prefix(1);
    }
  }
  return {std::move(out), s};
}
} // namespace

auto content_type::param(std::string_view key) const -> std::string_view {
  for (const auto &[candidate, value] : params) {
    if (candidate.size() != key.size())
      continue;
    bool equal = true;
    for (std::size_t i{}; i < key.size(); ++i) {
      const auto a = static_cast<char>(
          candidate[i] >= 'A' && candidate[i] <= 'Z' ? candidate[i] + 32
                                                     : candidate[i]);
      const auto b = static_cast<char>(
          key[i] >= 'A' && key[i] <= 'Z' ? key[i] + 32 : key[i]);
      if (a != b) {
        equal = false;
        break;
      }
    }
    if (equal)
      return value;
  }
  return {};
}
auto parse_content_type(std::string_view header) -> content_type {
  content_type result;
  header = skip_ows(header);
  auto [type, rest] = token(header);
  if (!rest.empty() && rest.front() == '/') {
    rest.remove_prefix(1);
    auto [sub, after] = token(rest);
    result.mime = lower(type) + "/" + lower(sub);
    rest = after;
  } else
    result.mime = lower(type);
  while (!rest.empty()) {
    rest = skip_ows(rest);
    if (rest.empty() || rest.front() != ';')
      break;
    rest.remove_prefix(1);
    rest = skip_ows(rest);
    auto [name, after_name] = token(rest);
    rest = after_name;
    if (name.empty())
      break;
    auto key = lower(name);
    if (rest.empty() || rest.front() != '=') {
      result.params[std::move(key)] = {};
      continue;
    }
    rest.remove_prefix(1);
    std::string value;
    if (!rest.empty() && rest.front() == '\"') {
      auto [q, after] = quoted(rest);
      value = std::move(q);
      rest = after;
    } else {
      auto [v, after] = token(rest);
      value = std::string(v);
      rest = after;
    }
    result.params[std::move(key)] = std::move(value);
  }
  return result;
}
} // namespace cnetmod::http
