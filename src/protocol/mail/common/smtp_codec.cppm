/// SMTP command and reply wire codec.
export module cnetmod.protocol.mail:codec;

import std;
import :types;

export namespace cnetmod::mail {

[[nodiscard]] auto parse_reply(std::string_view wire)
    -> std::expected<reply, std::string>;
[[nodiscard]] auto
serialize_command(std::string_view verb,
                  std::span<const std::string_view> arguments)
    -> std::expected<std::string, std::string>;
[[nodiscard]] auto dot_stuff(std::string_view body)
    -> std::expected<std::string, std::string>;
[[nodiscard]] auto dot_unstuff(std::string_view body)
    -> std::expected<std::string, std::string>;

} // namespace cnetmod::mail
