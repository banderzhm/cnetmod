export module cnetmod.protocol.http:multipart_form;

import std;
import :semantics;

export namespace cnetmod::http {
struct form_field {
  std::string name;
  std::string value;
};
struct form_file {
  std::string field_name;
  std::string filename;
  std::string content_type;
  header_map headers;
  std::vector<std::byte> data;
  [[nodiscard]] auto size() const noexcept -> std::size_t;
  [[nodiscard]] auto data_as_string() const noexcept -> std::string_view;
};
class form_data {
public:
  [[nodiscard]] auto field(std::string_view name) const
      -> std::optional<std::string_view>;
  [[nodiscard]] auto fields(std::string_view name) const
      -> std::vector<std::string_view>;
  [[nodiscard]] auto has_field(std::string_view name) const noexcept -> bool;
  [[nodiscard]] auto file(std::string_view name) const -> const form_file *;
  [[nodiscard]] auto files(std::string_view name) const
      -> std::vector<const form_file *>;
  [[nodiscard]] auto has_file(std::string_view name) const noexcept -> bool;
  [[nodiscard]] auto all_fields() const noexcept
      -> const std::vector<form_field> &;
  [[nodiscard]] auto all_files() const noexcept
      -> const std::vector<form_file> &;
  [[nodiscard]] auto field_count() const noexcept -> std::size_t;
  [[nodiscard]] auto file_count() const noexcept -> std::size_t;
  void add_field(form_field field);
  void add_file(form_file file);

private:
  std::vector<form_field> fields_;
  std::vector<form_file> files_;
};
} // namespace cnetmod::http
