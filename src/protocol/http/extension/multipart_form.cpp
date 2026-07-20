module cnetmod.protocol.http;
import std;
import :multipart_form;
namespace cnetmod::http {
auto form_file::size() const noexcept -> std::size_t { return data.size(); }
auto form_file::data_as_string() const noexcept -> std::string_view {
  return {reinterpret_cast<const char *>(data.data()), data.size()};
}
auto form_data::field(std::string_view name) const
    -> std::optional<std::string_view> {
  for (const auto &f : fields_)
    if (f.name == name)
      return f.value;
  return std::nullopt;
}
auto form_data::fields(std::string_view name) const
    -> std::vector<std::string_view> {
  std::vector<std::string_view> r;
  for (const auto &f : fields_)
    if (f.name == name)
      r.push_back(f.value);
  return r;
}
auto form_data::has_field(std::string_view name) const noexcept -> bool {
  return std::ranges::any_of(fields_,
                             [&](const auto &f) { return f.name == name; });
}
auto form_data::file(std::string_view name) const -> const form_file * {
  for (const auto &f : files_)
    if (f.field_name == name)
      return &f;
  return nullptr;
}
auto form_data::files(std::string_view name) const
    -> std::vector<const form_file *> {
  std::vector<const form_file *> r;
  for (const auto &f : files_)
    if (f.field_name == name)
      r.push_back(&f);
  return r;
}
auto form_data::has_file(std::string_view name) const noexcept -> bool {
  return file(name) != nullptr;
}
auto form_data::all_fields() const noexcept -> const std::vector<form_field> & {
  return fields_;
}
auto form_data::all_files() const noexcept -> const std::vector<form_file> & {
  return files_;
}
auto form_data::field_count() const noexcept -> std::size_t {
  return fields_.size();
}
auto form_data::file_count() const noexcept -> std::size_t {
  return files_.size();
}
void form_data::add_field(form_field field) {
  fields_.push_back(std::move(field));
}
void form_data::add_file(form_file file) { files_.push_back(std::move(file)); }
} // namespace cnetmod::http
