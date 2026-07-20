module cnetmod.protocol.mail;

import :types;
import std;

namespace cnetmod::mail {
namespace {
auto equals_ignore_case(std::string_view left, std::string_view right) noexcept
    -> bool {
  return std::ranges::equal(
      left, right, {},
      [](unsigned char value) {
        return static_cast<char>(std::toupper(value));
      },
      [](unsigned char value) {
        return static_cast<char>(std::toupper(value));
      });
}
} // namespace

auto capability::matches(std::string_view value) const noexcept -> bool {
  return equals_ignore_case(name, value);
}
auto capability::has_argument() const noexcept -> bool {
  return !argument.empty();
}

auto envelope::empty() const noexcept -> bool {
  return sender.empty() && recipients.empty();
}
auto envelope::valid() const noexcept -> bool {
  return !sender.empty() && !recipients.empty() &&
         std::ranges::all_of(recipients, [](const auto &recipient) {
           return !recipient.empty();
         });
}
void envelope::add_recipient(std::string recipient) {
  if (!recipient.empty())
    recipients.push_back(std::move(recipient));
}

void message::set_header(std::string name, std::string value) {
  const auto existing = std::ranges::find_if(headers, [&](const auto &header) {
    return equals_ignore_case(header.first, name);
  });
  if (existing == headers.end()) {
    headers.emplace_back(std::move(name), std::move(value));
  } else {
    existing->second = std::move(value);
  }
}
auto message::header_value(std::string_view name) const
    -> std::optional<std::string_view> {
  const auto existing = std::ranges::find_if(headers, [&](const auto &header) {
    return equals_ignore_case(header.first, name);
  });
  if (existing == headers.end())
    return std::nullopt;
  return existing->second;
}
auto message::empty() const noexcept -> bool {
  return headers.empty() && body.empty();
}

auto reply::successful() const noexcept -> bool {
  return classification == reply_class::positive_preliminary ||
         classification == reply_class::positive_completion ||
         classification == reply_class::positive_intermediate;
}
auto reply::transient_failure() const noexcept -> bool {
  return classification == reply_class::transient_negative_completion;
}
auto reply::permanent_failure() const noexcept -> bool {
  return classification == reply_class::permanent_negative_completion;
}
auto reply::text() const -> std::string {
  std::string result;
  for (const auto &line : lines) {
    if (!result.empty())
      result.push_back('\n');
    result.append(line);
  }
  return result;
}

auto classify_reply(std::uint16_t code) noexcept -> reply_class {
  if (code < 100 || code > 599)
    return reply_class::invalid;
  return static_cast<reply_class>(code / 100);
}
auto to_string(auth_mechanism mechanism) noexcept -> std::string_view {
  switch (mechanism) {
  case auth_mechanism::plain:
    return "PLAIN";
  case auth_mechanism::login:
    return "LOGIN";
  case auth_mechanism::cram_md5:
    return "CRAM-MD5";
  case auth_mechanism::xoauth2:
    return "XOAUTH2";
  case auth_mechanism::oauthbearer:
    return "OAUTHBEARER";
  case auth_mechanism::external:
    return "EXTERNAL";
  case auth_mechanism::none:
    return "";
  }
  return "";
}
auto parse_auth_mechanism(std::string_view value) noexcept
    -> std::optional<auth_mechanism> {
  for (const auto candidate :
       {auth_mechanism::plain, auth_mechanism::login, auth_mechanism::cram_md5,
        auth_mechanism::xoauth2, auth_mechanism::oauthbearer,
        auth_mechanism::external}) {
    if (equals_ignore_case(value, to_string(candidate)))
      return candidate;
  }
  return std::nullopt;
}
} // namespace cnetmod::mail
