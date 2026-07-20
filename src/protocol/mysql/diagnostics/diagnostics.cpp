module cnetmod.protocol.mysql;

import std;
import :diagnostics;

namespace cnetmod::mysql {
namespace {
auto next_char_utf8mb4(std::span<const unsigned char> input) noexcept
    -> std::size_t {
  if (input.empty()) {
    return 0;
  }

  const auto first = input.front();
  if (first < 0x80) {
    return 1;
  }
  if ((first & 0xe0) == 0xc0 && input.size() >= 2) {
    return 2;
  }
  if ((first & 0xf0) == 0xe0 && input.size() >= 3) {
    return 3;
  }
  if ((first & 0xf8) == 0xf0 && input.size() >= 4) {
    return 4;
  }
  return 0;
}

auto next_char_ascii(std::span<const unsigned char> input) noexcept
    -> std::size_t {
  return !input.empty() && input.front() < 0x80 ? 1 : 0;
}
} // namespace

const character_set utf8mb4_charset{"utf8mb4", next_char_utf8mb4};
const character_set ascii_charset{"ascii", next_char_ascii};

diagnostics::diagnostics() noexcept = default;

auto diagnostics::server_message() const noexcept -> std::string_view {
  return is_server_ ? std::string_view(msg_) : std::string_view{};
}

auto diagnostics::client_message() const noexcept -> std::string_view {
  return is_server_ ? std::string_view{} : std::string_view(msg_);
}

void diagnostics::assign_server(std::string message) {
  msg_ = std::move(message);
  is_server_ = true;
}

void diagnostics::assign_client(std::string message) {
  msg_ = std::move(message);
  is_server_ = false;
}

void diagnostics::clear() noexcept {
  msg_.clear();
  is_server_ = false;
}

auto diagnostics::empty() const noexcept -> bool { return msg_.empty(); }

auto operator==(const diagnostics &lhs, const diagnostics &rhs) noexcept
    -> bool {
  return lhs.is_server_ == rhs.is_server_ && lhs.msg_ == rhs.msg_;
}

auto operator!=(const diagnostics &lhs, const diagnostics &rhs) noexcept
    -> bool {
  return !(lhs == rhs);
}

auto escape_string(std::string_view input, const format_options &options,
                   quoting_context context, std::string &output) -> bool {
  output.clear();
  output.reserve(input.size() + input.size() / 8);

  const auto quote = static_cast<char>(context);
  for (const auto character : input) {
    if (character == quote) {
      output += quote;
      output += quote;
      continue;
    }
    if (context == quoting_context::backtick) {
      output += character;
      continue;
    }
    if (options.backslash_escapes) {
      switch (character) {
      case '\\':
        output += "\\\\";
        continue;
      case '\0':
        output += "\\0";
        continue;
      case '\n':
        output += "\\n";
        continue;
      case '\r':
        output += "\\r";
        continue;
      case '\x1a':
        output += "\\Z";
        continue;
      default:
        break;
      }
    }
    output += character;
  }
  return true;
}

auto escape_string(std::string_view input, const format_options &options,
                   quoting_context context) -> std::string {
  std::string result;
  escape_string(input, options, context, result);
  return result;
}
} // namespace cnetmod::mysql