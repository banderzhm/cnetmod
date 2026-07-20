export module cnetmod.protocol.http:multipart_parser;

import std;
import :multipart_form;

export namespace cnetmod::http {
class multipart_parser {
public:
  explicit multipart_parser(std::string boundary);
  [[nodiscard]] auto parse(std::string_view body)
      -> std::expected<form_data, std::error_code>;

private:
  static auto find_in(std::string_view data, std::string_view needle,
                      std::size_t offset) noexcept -> std::size_t;
  auto skip_transport_padding(std::string_view body, std::size_t &pos) const
      -> bool;
  auto parse_part(std::string_view data, form_data &result)
      -> std::expected<void, std::error_code>;
  static auto parse_part_headers(std::string_view block)
      -> std::expected<header_map, std::error_code>;
  static auto decode_body(std::string_view body, std::string_view encoding)
      -> std::expected<std::vector<std::byte>, std::error_code>;
  static auto raw_copy(std::string_view body) -> std::vector<std::byte>;
  std::string boundary_;
  std::string delimiter_;
  std::string close_delimiter_;
};
[[nodiscard]] auto parse_form_urlencoded(std::string_view body) -> form_data;
[[nodiscard]] auto parse_form(std::string_view content_type_header,
                              std::string_view body)
    -> std::expected<form_data, std::error_code>;
} // namespace cnetmod::http
