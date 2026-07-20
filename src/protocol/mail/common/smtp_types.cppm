/// SMTP protocol data contracts.
export module cnetmod.protocol.mail:types;

import std;

export namespace cnetmod::mail {

enum class reply_class : std::uint8_t {
  invalid = 0,
  positive_preliminary = 1,
  positive_completion = 2,
  positive_intermediate = 3,
  transient_negative_completion = 4,
  permanent_negative_completion = 5,
};

enum class auth_mechanism : std::uint8_t {
  none,
  plain,
  login,
  cram_md5,
  xoauth2,
  oauthbearer,
  external,
};

struct capability {
  std::string name;
  std::string argument;

  [[nodiscard]] auto matches(std::string_view value) const noexcept -> bool;
  [[nodiscard]] auto has_argument() const noexcept -> bool;
};

struct envelope {
  std::string sender;
  std::vector<std::string> recipients;

  [[nodiscard]] auto empty() const noexcept -> bool;
  [[nodiscard]] auto valid() const noexcept -> bool;
  void add_recipient(std::string recipient);
};

struct message {
  using header = std::pair<std::string, std::string>;

  std::vector<header> headers;
  std::string body;

  void set_header(std::string name, std::string value);
  [[nodiscard]] auto header_value(std::string_view name) const
      -> std::optional<std::string_view>;
  [[nodiscard]] auto empty() const noexcept -> bool;
};

struct reply {
  std::uint16_t code = 0;
  reply_class classification = reply_class::invalid;
  std::optional<std::string> enhanced_code;
  std::vector<std::string> lines;

  [[nodiscard]] auto successful() const noexcept -> bool;
  [[nodiscard]] auto transient_failure() const noexcept -> bool;
  [[nodiscard]] auto permanent_failure() const noexcept -> bool;
  [[nodiscard]] auto text() const -> std::string;
};

[[nodiscard]] auto classify_reply(std::uint16_t code) noexcept -> reply_class;
[[nodiscard]] auto to_string(auth_mechanism mechanism) noexcept
    -> std::string_view;
[[nodiscard]] auto parse_auth_mechanism(std::string_view value) noexcept
    -> std::optional<auth_mechanism>;

} // namespace cnetmod::mail
