export module cnetmod.protocol.http:multipart_disposition;

import std;

export namespace cnetmod::http {
struct content_disposition {
  std::string type;
  std::string name;
  std::string filename;
  std::string filename_star;
  [[nodiscard]] auto effective_filename() const noexcept -> std::string_view;
  [[nodiscard]] auto has_filename() const noexcept -> bool;
};
[[nodiscard]] auto parse_content_disposition(std::string_view header)
    -> content_disposition;
} // namespace cnetmod::http
