export module cnetmod.protocol.http:multipart_content_type;

import std;

export namespace cnetmod::http {

struct content_type {
  std::string mime;
  std::unordered_map<std::string, std::string> params;

  [[nodiscard]] auto param(std::string_view key) const -> std::string_view;
};

[[nodiscard]] auto parse_content_type(std::string_view header) -> content_type;

} // namespace cnetmod::http
