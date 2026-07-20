module cnetmod.protocol.mail;

import :codec;
import :types;
import std;

namespace cnetmod::mail {
namespace {
constexpr std::string_view crlf = "\r\n";

auto is_command_atom(std::string_view value) noexcept -> bool {
  return !value.empty() && std::ranges::all_of(value, [](unsigned char ch) {
    return std::isalpha(ch) != 0 || std::isdigit(ch) != 0 || ch == '-';
  });
}
auto has_line_break(std::string_view value) noexcept -> bool {
  return value.find_first_of("\r\n") != std::string_view::npos;
}
auto has_only_crlf_line_endings(std::string_view value) noexcept -> bool {
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '\r') {
      if (index + 1 >= value.size() || value[index + 1] != '\n')
        return false;
      ++index;
    } else if (value[index] == '\n') {
      return false;
    }
  }
  return true;
}
auto parse_enhanced_code(std::string_view line) -> std::optional<std::string> {
  const auto separator = line.find(' ');
  const auto token = line.substr(0, separator);
  if (std::ranges::count(token, '.') != 2 || token.empty())
    return std::nullopt;
  return std::string(token);
}
} // namespace

auto parse_reply(std::string_view wire) -> std::expected<reply, std::string> {
  if (wire.empty() || !wire.ends_with(crlf))
    return std::unexpected("SMTP reply must end with CRLF");

  reply result;
  std::size_t offset = 0;
  bool terminated = false;
  while (offset < wire.size()) {
    const auto end = wire.find(crlf, offset);
    if (end == std::string_view::npos)
      return std::unexpected("SMTP reply contains an incomplete line");
    const auto line = wire.substr(offset, end - offset);
    offset = end + crlf.size();
    if (line.size() < 4 ||
        !std::ranges::all_of(line.substr(0, 3),
                             [](char ch) {
                               return std::isdigit(
                                          static_cast<unsigned char>(ch)) != 0;
                             }) ||
        (line[3] != ' ' && line[3] != '-'))
      return std::unexpected("invalid SMTP reply line");
    const auto code = static_cast<std::uint16_t>(
        (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0'));
    if (result.code == 0) {
      result.code = code;
      result.classification = classify_reply(code);
    } else if (result.code != code) {
      return std::unexpected("SMTP multiline reply changes status code");
    }
    const auto text = line.substr(4);
    if (!result.enhanced_code)
      result.enhanced_code = parse_enhanced_code(text);
    result.lines.emplace_back(text);
    if (line[3] == ' ') {
      terminated = true;
      break;
    }
  }
  if (!terminated || offset != wire.size())
    return std::unexpected("SMTP multiline reply is not properly terminated");
  return result;
}

auto serialize_command(std::string_view verb,
                       std::span<const std::string_view> arguments)
    -> std::expected<std::string, std::string> {
  if (!is_command_atom(verb))
    return std::unexpected(
        "SMTP command verb must contain only letters, digits, or hyphens");
  std::string wire;
  wire.reserve(verb.size() + 2);
  for (const auto argument : arguments) {
    if (has_line_break(argument))
      return std::unexpected("SMTP command argument contains a line break");
  }
  wire.append(verb);
  for (const auto argument : arguments) {
    wire.push_back(' ');
    wire.append(argument);
  }
  wire.append(crlf);
  return wire;
}

auto dot_stuff(std::string_view body)
    -> std::expected<std::string, std::string> {
  if (!has_only_crlf_line_endings(body))
    return std::unexpected("SMTP message body must use CRLF line endings");
  std::string result;
  result.reserve(body.size() + 8);
  bool at_line_start = true;
  for (std::size_t index = 0; index < body.size(); ++index) {
    if (at_line_start && body[index] == '.')
      result.push_back('.');
    result.push_back(body[index]);
    at_line_start = body[index] == '\n';
  }
  if (!body.empty() && !body.ends_with(crlf))
    return std::unexpected("SMTP DATA body must use CRLF line endings");
  return result;
}

auto dot_unstuff(std::string_view body)
    -> std::expected<std::string, std::string> {
  if (!has_only_crlf_line_endings(body))
    return std::unexpected("SMTP message body must use CRLF line endings");
  if (!body.empty() && !body.ends_with(crlf))
    return std::unexpected("SMTP DATA body must use CRLF line endings");
  std::string result;
  result.reserve(body.size());
  bool at_line_start = true;
  for (std::size_t index = 0; index < body.size(); ++index) {
    if (at_line_start && body[index] == '.' && index + 1 < body.size() &&
        body[index + 1] == '.')
      ++index;
    result.push_back(body[index]);
    at_line_start = body[index] == '\n';
  }
  return result;
}
} // namespace cnetmod::mail
